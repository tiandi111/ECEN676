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
        LruTable(UINT32 rows, UINT32 associativity) {
            assert( rows > 0 );
            assert( associativity > 0 );
            for (INT64 i = 0; i < INT64(rows); ++i) {
                table.push_back(std::list<UINT32> ());
                for (INT64 j = 0; j < INT64(associativity); ++j) {
                    table.at(i).push_back(j);
                }
            }
        } 

        void touch(UINT32 row, UINT32 way) {
            std::list<UINT32>& entry = table.at(row);
            for (std::list<UINT32>::iterator it = entry.begin(); it != entry.end(); ++it) {
                if (*it == way) {
                    entry.erase(it);
                    break;
                }
            }
            entry.push_front(way);
        }

        UINT32 back(UINT32 row) {
            return table.at(row).back();
        }

    private:
        std::vector<std::list<UINT32> > table;
}; 

class InvertedCache {
    public:
        InvertedCache(UINT32 logPhyMemSizeParam, UINT32 logBlockSizeParam)
        : logPhyMemSize(logPhyMemSizeParam),
          logBlockSize(logBlockSizeParam),
          addrMask(~((1u << logBlockSize) - 1 )),
          phyBlkSize(1u << (logPhyMemSize - logBlockSize)),
          table(phyBlkSize, entry())
        { 
            for(UINT32 i = 0; i < phyBlkSize; i ++)
                table.at(i).valid = false;
        }

        void set(UINT32 physicalAddr, UINT32 row, UINT32 way) {
            UINT32 idx = getIdx(physicalAddr);
            table.at(idx).row = row;
            table.at(idx).way = way;
            table.at(idx).valid = true;
        }

        bool get(UINT32 physicalAddr, UINT32* row, UINT32* way) {
            UINT32 idx = getRow(physicalAddr);
            if (table.at(idx).valid) {
                *row = table.at(idx).row;
                *way = table.at(idx).way;
            }
	          return table.at(idx).valid;
        }

    private:
        UINT32 getIdx(UINT32 physicalAddr) {
	          UINT32 idx = (physicalAddr & addrMask) >> logBlockSize;
            assert (idx < phyBlkSize);
            return idx;
        }

        struct entry {
            UINT32 row;
            UINT32 way;
            BOOL   valid;
        };

        UINT32 logPhyMemSize;
        UINT32 logBlockSize;
        UINT32 addrMask;
        UINT32 phyBlkSize;
        std::vector<entry> table;
};

UINT32 getPageNum(UINT32 addr) {
    return addr >> logPageSize;
}

UINT32 getPhysicalAddr(UINT32 virtualAddr) {
    UINT32 offset = virtualAddr & pageOffsetMask;
    UINT32 phyAddr = (offset + (getPhysicalPageNumber(getPageNum(virtualAddr)) << logPageSize) ) & ((1u << logPhysicalMemSize) - 1);
    assert(phyAddr < (1u << logPhysicalMemSize));
    return phyAddr;
}

class LruAliasingFreeCacheModel : public CacheModel {
    public:
        LruAliasingFreeCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
        : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam),
          lruTable(1u << logNumRows, associativity),
          invrtCache(logPhysicalMemSize, logBlockSize),
          numRows(1u << logNumRows)
        { 
            tagMask = ~( (1u << (logNumRows + logBlockSize)) - 1);
        }

        virtual void readReq(UINT32 virtualAddr) = 0;

        virtual void writeReq(UINT32 virtualAddr) = 0;

    protected:
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

        void evict(UINT32 row, UINT32 way, UINT32 addrTag, UINT32 physicalAddr) {
            assert(row < numRows && way < associativity);
            validBit[row][way] = true;
            tag[row][way] = addrTag;
            lruTable.touch(row, way);
            invrtCache.set(physicalAddr, row, way);
        }

        void invalidateAliasing(UINT32 physicalAddr) {
            UINT32 row = 0;
            UINT32 way = 0;
            if ( invrtCache.get(physicalAddr, &row, &way) ) {
                assert(row < numRows && way < associativity);
                validBit[row][way] = false;
            }
        }

        void handleReq(BOOL readReq, BOOL invalAlias, UINT32 row, UINT32 addrTag, UINT32 physicalAddr)
        {
            assert(row < numRows);
            incReq(readReq);
            for (INT64 i = 0; i < INT64(associativity); ++i) {
                if (validBit[row][i] && addrTag == tag[row][i]) {
                    incHit(readReq);
                    lruTable.touch(row, i);
                    return;
                }
            }
            if (invalAlias) {  
                invalidateAliasing(physicalAddr);
            }
            for (INT64 i = 0; i < INT64(associativity); ++i) {
                if ( !validBit[row][i] ) {
                    evict(row, i, addrTag, physicalAddr);
                    return;
                }
            }
            evict(row, lruTable.back(row), addrTag, physicalAddr);
        }

        LruTable      lruTable;
        InvertedCache invrtCache;

        UINT32 numRows;
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
            handleReq(true, false, getRow(physicalAddr), getTag(physicalAddr), physicalAddr);
        }

        void writeReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleReq(false, false, getRow(physicalAddr), getTag(physicalAddr), physicalAddr);
        }
};

class LruVirIndexPhysTagCacheModel: public LruAliasingFreeCacheModel
{
    public:
        LruVirIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
        : LruAliasingFreeCacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
            if (logNumRows + logBlockSize > logPageSize)
                tagMask = ~((1u << logPageSize) - 1);
        }

        void readReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleReq(true, true, getRow(virtualAddr), getTag(physicalAddr), physicalAddr);
        }

        void writeReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleReq(false, true, getRow(virtualAddr), getTag(physicalAddr), physicalAddr);
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
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr); 
            handleReq(true, true, getRow(virtualAddr), getTag(virtualAddr), physicalAddr);
        }

        void writeReq(UINT32 virtualAddr)
        {
            UINT32 physicalAddr = getPhysicalAddr(virtualAddr);
            handleReq(false, true, getRow(virtualAddr), getTag(virtualAddr), physicalAddr);
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
                "r", "11", "specify the log of number of rows in the cache");

// This knob will set the cache param logBlockSize
KNOB<UINT32> KnobLogBlockSize(KNOB_MODE_WRITEONCE, "pintool",
                "b", "2", "specify the log of block size of the cache in bytes");

// This knob will set the cache param associativity
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
                "a", "1", "specify the associativity of the cache");

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
