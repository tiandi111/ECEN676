#include <iostream>
#include <fstream> 
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <list>
#include <vector>
#include <map>
#include "pin.H"

using namespace std;

UINT32 logPageSize;
UINT32 logPhysicalMemSize;
UINT32 pageOffsetMask;

//Function to obtain physical page number given a virtual page number
UINT64 getPhysicalPageNumber(UINT64 virtualPageNumber)
{
    INT32 key = (INT32) virtualPageNumber;
    key = ~key + (key << 15); // key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057; // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 16);
    return (UINT32) (key&(((UINT32)(~0))>>(32-logPhysicalMemSize)));
}

class CacheModel
{
    protected:
        UINT32   logNumRows;
        UINT32   logBlockSize;
        UINT32   associativity;
        UINT64   readReqs;
        UINT64   writeReqs;
        UINT64   readHits;
        UINT64   writeHits;
        UINT32** tag;
        bool**   validBit;
        UINT32   tagMask;

    public:
        //Constructor for a cache
        CacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
        {
            logNumRows = logNumRowsParam;
            logBlockSize = logBlockSizeParam;
            associativity = associativityParam;
            readReqs = 0;
            writeReqs = 0;
            readHits = 0;
            writeHits = 0;
            tag = new UINT32*[1u<<logNumRows];
            validBit = new bool*[1u<<logNumRows];
            for(UINT32 i = 0; i < 1u<<logNumRows; i++)
            {
                tag[i] = new UINT32[associativity];
                validBit[i] = new bool[associativity];
                for(UINT32 j = 0; j < associativity; j++)
                    validBit[i][j] = false;
            }
        }

        //Call this function to update the cache state whenever data is read
        virtual void readReq(UINT32 virtualAddr) = 0;

        //Call this function to update the cache state whenever data is written
        virtual void writeReq(UINT32 virtualAddr) = 0;

        //Do not modify this function
        void dumpResults(ofstream *outfile)
        {
        	*outfile << readReqs <<","<< writeReqs <<","<< readHits <<","<< writeHits <<"\n";
        }
};

CacheModel* cachePP;
CacheModel* cacheVP;
CacheModel* cacheVV;

class LruTable {
    public:
        LruList(UINT32 rows, UINT32 associativity) {
            assert(("LruTable rows must be > 0", rows > 0));
            assert(("LruTable associativity must be > 0", associativity > 0));
            for (INT64 i = 0; i < INT64(rows); ++i) {
                table.emplace_back({});
                for (INT64 j = 0; j < INT64(associativity); ++j) {
                    table.at(i).emplace_back(j);
                }
            }
        }

        ~LruList() = default ;

        void touch(UINT32 row, UINT32 way) {
            auto& entry = table.at(row);
            for (auto it = entry.begin(); it != entry.end(); ++it) {
                if (*it == way) {
                    entry.erase(it);
                    break;
                }
            }
            entry.emplace_front(way);
        }

        UINT32 front(UINT32 row) {
            return table.at(row).front;
        }

    private:
        std::vector<std::list<UINT32>> table;
};

Uint32 getPageNum(UINT32 addr) {
    return addr >> logPageSize;
}

UINT32 getPhysicalAddr(UINT32 virtualAddr) {
    UINT32 offset = virtualAddr & pageOffsetMask;
    return (getPhysicalPageNumber(getPageNum(virtualAddr)) << logPageSize) + offset;
}

class LruAliasingFreeCacheModel : public CacheModel {
    public:
        LruAliasingFreeCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
            lruTable = LruTable(1u << logNumRows, associativity);
            tagMask = ~( (1u << (logNumRows + logBlockSize)) - 1);
        }

        virtual void readReq(UINT32 virtualAddr) = 0;

        virtual void writeReq(UINT32 virtualAddr) = 0;

    private:
        UINT32 getRow(UINT32 addr) {
            return (addr >> logBlockSize) & ((1u << logNumRows) - 1);
        }

        UINT32 getTag(UINT32 addr) {
            return addr & tagMask;
        }

        void incReq(BOOL readReq) {
            readReqs += readReq ? 1 : 0;
            writeReqs += readReq ? 0 : 1;
        }

        void incHit(BOOL readReq) {
            readHits += readReq ? 1 : 0;
            writeHits += readReq ? 0 : 1;
        }

        void evict(UINT32 row, UINR32 way, UINT32 addrTag) {
            validBit[row][way] = true;
            tag[row][way] = addrTag;
            lruTable(row, way).touch();
        }

        void handleReq(BOOL readReq, BOOL disallowAliasing, UINT32 row, UINT32 addrTag)
        {
            incReq(readReq);
            for (int i = 0; i < associativity; ++i) {
                if (validBit[row][i] && addrTag == tag[row][i]) {
                    incHit(readReq);
                    lruTable(row, i).touch();
                    return;
                }
            }
            for (int i = 0; i < associativity; ++i) {
                if ( !validBit[row][i] ) {
                    evict(row, i, addrTag);
                    return;
                }
            }
            evict(row, lruTable.front(row), addrTag);
        }

        LruTable lruTable;

//        struct Pos {
//            UINT32 row;
//            UINT32 way;
//        };
//
//        std::map<UINT32, Pos> invertedTabel;

};

class LruPhysIndexPhysTagCacheModel: public LruAliasingFreeCacheModel
{
    public:
        LruPhysIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : LruAliasingFreeCacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        void readReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleReadReq(true, false, getRow(physicalAddr), getTag(physicalAddr));
        }

        void writeReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleWriteReq(false, false, getRow(physicalAddr), getTag(physicalAddr));
        }
};

class LruVirIndexPhysTagCacheModel: public LruAliasingFreeCacheModel
{
    public:
        LruVirIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : LruAliasingFreeCacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        void readReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleReadReq(true, true, getRow(virtualAddr), getTag(physicalAddr));
        }

        void writeReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleWriteReq(false, true, getRow(virtualAddr), getTag(physicalAddr));
        }
};

class LruVirIndexVirTagCacheModel: public LruAliasingFreeCacheModel
{
    public:
        LruVirIndexVirTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : LruAliasingFreeCacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        void readReq(UINT32 virtualAddr)
        {
            handleReadReq(true, true, getRow(virtualAddr), getTag(virtualAddr));
        }

        void writeReq(UINT32 virtualAddr)
        {
            handleWriteReq(false, true, getRow(virtualAddr), getTag(virtualAddr));
        }
};

//Cache analysis routine
void cacheLoad(UINT32 virtualAddr)
{
    //Here the virtual address is aligned to a word boundary
    virtualAddr = (virtualAddr >> 2) << 2;
    cachePP->readReq(virtualAddr);
    cacheVP->readReq(virtualAddr);
    cacheVV->readReq(virtualAddr);
}

//Cache analysis routine
void cacheStore(UINT32 virtualAddr)
{
    //Here the virtual address is aligned to a word boundary
    virtualAddr = (virtualAddr >> 2) << 2;
    cachePP->writeReq(virtualAddr);
    cacheVP->writeReq(virtualAddr);
    cacheVV->writeReq(virtualAddr);
}

// This knob will set the outfile name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
			    "o", "results.out", "specify optional output file name");

// This knob will set the param logPhysicalMemSize
KNOB<UINT32> KnobLogPhysicalMemSize(KNOB_MODE_WRITEONCE, "pintool",
                "m", "16", "specify the log of physical memory size in bytes");

// This knob will set the param logPageSize
KNOB<UINT32> KnobLogPageSize(KNOB_MODE_WRITEONCE, "pintool",
                "p", "12", "specify the log of page size in bytes");

// This knob will set the cache param logNumRows
KNOB<UINT32> KnobLogNumRows(KNOB_MODE_WRITEONCE, "pintool",
                "r", "10", "specify the log of number of rows in the cache");

// This knob will set the cache param logBlockSize
KNOB<UINT32> KnobLogBlockSize(KNOB_MODE_WRITEONCE, "pintool",
                "b", "5", "specify the log of block size of the cache in bytes");

// This knob will set the cache param associativity
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
                "a", "2", "specify the associativity of the cache");

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    if(INS_IsMemoryRead(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cacheLoad, IARG_MEMORYREAD_EA, IARG_END);
    if(INS_IsMemoryWrite(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cacheStore, IARG_MEMORYWRITE_EA, IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    ofstream outfile;
    outfile.open(KnobOutputFile.Value().c_str());
    outfile.setf(ios::showbase);
    outfile << "physical index physical tag: ";
    cachePP->dumpResults(&outfile);
     outfile << "virtual index physical tag: ";
    cacheVP->dumpResults(&outfile);
     outfile << "virtual index virtual tag: ";
    cacheVV->dumpResults(&outfile);
    outfile.close();
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);
	
    logPageSize = KnobLogPageSize.Value();
    pageOffsetMask = (1u << logPageSize) - 1;
    printf("pageOffsetMask: %u\n", pageOffsetMask);
    logPhysicalMemSize = KnobLogPhysicalMemSize.Value();

    cachePP = new LruPhysIndexPhysTagCacheModel(KnobLogNumRows.Value(), KnobLogBlockSize.Value(), KnobAssociativity.Value()); 
    cacheVP = new LruVirIndexPhysTagCacheModel(KnobLogNumRows.Value(), KnobLogBlockSize.Value(), KnobAssociativity.Value());
    cacheVV = new LruVirIndexVirTagCacheModel(KnobLogNumRows.Value(), KnobLogBlockSize.Value(), KnobAssociativity.Value());

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
