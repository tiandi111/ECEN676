#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <map>
#include <vector>
#include <fstream>
#include <assert.h>
#include "pin.H"

using namespace std;

ofstream OutFile;

// The global array storing the spacing frequency between two dependant instructions
UINT64 *dependencySpacing;

// The global array counting the tail hits
UINT32 *dependencyTailCount;

// Full register to partial register array map
std::map<REG, std::vector<REG> > partialRegMap;

VOID initPartialRegMap() {
    for (INT32 i = 0; i < INT32(REG_LAST); ++i) {
        REG reg = REG(i);
        REG fullReg = REG_FullRegName(reg);
        if (partialRegMap.find(reg) == partialRegMap.end()) {
            partialRegMap.insert(std::pair<REG, std::vector<REG> >(fullReg, std::vector<REG>()));
        }
        partialRegMap[fullReg].push_back(reg);
    }
}

// Global data mutex
PIN_LOCK lock;

// key for accessing TLS storage in the threads
static TLS_KEY tls_key;

// Thread local data structure to record dependency distance info
struct threadLocalRecord {
    UINT64 timestamp;
    UINT64* regLastWrittenTimestamps;
};

// Function to access thread-specific record
threadLocalRecord* GetTLS(THREADID tid)
{
    threadLocalRecord* tlRecord = static_cast<threadLocalRecord*>(PIN_GetThreadData(tls_key, tid));
    return tlRecord;
}

// Output file name
INT32 maxSize;

// Whether to simulate with partial registers
BOOL usePartialReg = false;

// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.csv", "specify the output file name");

// This knob will set the maximum spacing between two dependant instructions in the program
KNOB<string> KnobMaxSpacing(KNOB_MODE_WRITEONCE, "pintool", "s", "100", "specify the maximum spacing between two dependant instructions in the program");

// This knob will set the whether to simulate with partial register
KNOB<string> KnobUsePartialReg(KNOB_MODE_WRITEONCE, "pintool", "useParReg", "0", "simulate with partial register");

// This function compute dependency distance for the given reg
UINT64 computeSpacing(UINT32 tid, REG reg) {
    UINT64 dist;

    threadLocalRecord* tlRecord = GetTLS(tid);
    UINT64* rlwt = tlRecord->regLastWrittenTimestamps;
    REG fullReg = REG_FullRegName(reg);

    if (usePartialReg) {
        if (REG_is_partialreg(reg)) { // for partial reg, only look at itself and the larger reg

            dist = tlRecord->timestamp - (rlwt[reg] > rlwt[fullReg] ? rlwt[reg] : rlwt[fullReg]);

        } else { // for large reg, look at itself and all partial regs

            UINT64 mostRecentWrite = 0;
            std::vector<REG> parRegs = partialRegMap[reg];
            for (std::vector<REG>::iterator it = parRegs.begin(); it < parRegs.end(); ++it) {
                if (rlwt[*it] > mostRecentWrite) {
                    mostRecentWrite = rlwt[*it];
                }
            }
            dist = tlRecord->timestamp - mostRecentWrite;

        }
    } else {
        dist = tlRecord->timestamp - rlwt[fullReg];
    }

    return dist >= UINT64(maxSize) ? UINT64(maxSize) : dist;
}

// This function is called before every instruction is executed. Have to change
// the code to send in the register names from the Instrumentation function
VOID updateSpacingInfo(UINT32 tid, REG reg){
    UINT64 dist = computeSpacing(tid, reg);
    PIN_GetLock(&lock, tid);
    dependencySpacing[dist]++;
    dependencyTailCount[reg] += (dist > UINT64(maxSize/5 * 4)) ? 1 : 0; // always use real reg to count
    PIN_ReleaseLock(&lock);
}

// This function update the register last written timestamp
VOID updateRegLastWrittenTimestamp(UINT32 tid, REG reg) {
    if (!usePartialReg) {
        reg = REG_FullRegName(reg); // take full register
    }
    threadLocalRecord* tlRecord = GetTLS(tid);
    tlRecord->regLastWrittenTimestamps[reg] = tlRecord->timestamp;
}

// This function update the instruction timestamp
VOID updateTimestamp(UINT32 tid) {
    threadLocalRecord* tlRecord = GetTLS(tid);
    tlRecord->timestamp++;
}

// This function prepare thread local data structure for each thread.
VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    threadLocalRecord* tlRecord = new threadLocalRecord();

    tlRecord->regLastWrittenTimestamps = new UINT64[REG_LAST];
    for (INT32 i = 0; UINT32(i) < REG_LAST; ++i) {
        tlRecord->regLastWrittenTimestamps[i] = 0;
    }

    PIN_SetThreadData(tls_key, tlRecord, tid);
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    // Insert a call to updateSpacingInfo before every instruction.
    // You may need to add arguments to the call.
    std::set<REG> seen;
    UINT32 maxNumRReg = INS_MaxNumRRegs(ins);
    UINT32 maxNumWReg = INS_MaxNumWRegs(ins);
    // updateSpacingInfo
    for (INT32 i = 0; i < INT32(maxNumRReg); ++i) {
        REG reg = INS_RegR(ins, UINT32(i));
        if (seen.find(reg) != seen.end()) {
            continue;
        }
        seen.insert(reg);
        INS_InsertCall(ins,
                IPOINT_BEFORE,
                (AFUNPTR)updateSpacingInfo,
                IARG_THREAD_ID,
                IARG_UINT32, UINT32(reg),
                IARG_END);
    }
    // update regLastWrittenTimestamp
    for (INT32 i = 0; i < INT32(maxNumWReg); ++i) {
        REG reg = INS_RegW(ins, UINT32(i));
        INS_InsertCall(ins,
                IPOINT_BEFORE,
                (AFUNPTR)updateRegLastWrittenTimestamp,
                IARG_THREAD_ID,
                IARG_UINT32, UINT32(reg),
                IARG_END);
    }
    // update timestamp
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)updateTimestamp, IARG_THREAD_ID, IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    // Write to a file since cout and cerr maybe closed by the application
    OutFile.open(KnobOutputFile.Value().c_str());
    OutFile.setf(ios::showbase);
    for (INT32 i = 0; i < maxSize; i++)
        OutFile << dependencySpacing[i]<<",";
    OutFile.close();
    // Find the register that counts the most in the long-tail part
    UINT32 maxTailHits = 0, maxTailHitReg = 0;
    for (INT32 j = 0; UINT32(j) < REG_LAST; ++j) {
        if (dependencyTailCount[j] >= maxTailHits) {
            maxTailHitReg = j;
            maxTailHits = dependencyTailCount[j];
        }
    }
    printf("Register that counts the most in the long-tail part: %s, total hits: %d\n", REG_StringShort(REG(maxTailHitReg)).c_str(), maxTailHits);
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    // Initialize the pin lock
    PIN_InitLock(&lock);

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(0);

//    printf("Warning: Pin Tool not implemented\n");

    maxSize = atoi(KnobMaxSpacing.Value().c_str());
    usePartialReg = BOOL(atoi(KnobUsePartialReg.Value().c_str()));

    // Initializing data structures
    initPartialRegMap();
    dependencySpacing = new UINT64[maxSize];
    dependencyTailCount = new UINT32[REG_LAST];
    memset(dependencySpacing, 0, sizeof(UINT64) * maxSize);
    memset(dependencyTailCount, 0, sizeof(UINT32) * REG_LAST);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Analysis routines to be called when a thread begins/ends
    PIN_AddThreadStartFunction(ThreadStart, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

