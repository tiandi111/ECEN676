#include <map>
#include <stack>
#include <cstdint>
#include <iostream>
#include <fstream> 
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <stack>
#include <set>
#include <list>
#include "pin.H"

using namespace std;

// This knob will set the outfile name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
                            "o", "results.out", "specify optional output file name");

// This knob will set the param logPageSize
KNOB<UINT32> KnobLogPageSize(KNOB_MODE_WRITEONCE, "pintool",
                             "p", "12", "specify the log of page size in bytes");

// This knob will set the cache param logNumRows
KNOB<UINT32> KnobLogNumRows(KNOB_MODE_WRITEONCE, "pintool",
                            "r", "10", "specify the log of number of rows in the cache");

// This knob will set the cache param associativity
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
                               "a", "2", "specify the associativity of the cache");

// This knob will set the param logPoolSize
KNOB<UINT32> KnobLogPoolSize(KNOB_MODE_WRITEONCE, "pintool",
                               "s", "5", "specify the size of the frame pool");

// Replacement policy interface
// All replacement advisor must inherit this class.
class ReplAdvisor {
public:
    ReplAdvisor() {};

    virtual VOID visit(UINT32 row, UINT32 key) = 0;

    virtual UINT32 victim(UINT32 row) = 0;
};

// Random replacement advisor
class RandomReplAdvisor : public ReplAdvisor {
private:
    vector<set<UINT32> >    sets;
    vector<vector<UINT32> > vecs;

public:
    RandomReplAdvisor(UINT32 numRows) {
        srand(time(NULL));
        for (INT32 i = 0; i < INT64(numRows); ++i) {
            sets.push_back(std::set<UINT32> ());
            vecs.push_back(std::vector<UINT32> ())
        }
    };

    ~RandomPageTableReplAdvisor() {};

    VOID visit(UINT32 row, UINT32 key) {
        set<UINT32>& set = sets.at(row);
        vector<UINT32>& vec = vecs.at(row);
        if (set.find(key) != set.end()) return;
        set.insert(key);
        vec.push_back(key);
    }

    UINT32 victim(UINT32 row) {
        vector<UINT32>& vec = vecs.at(row);
        return vec[rand() % vec.size()];
    }
};

// Least Recently Used replacement advisor
class LruReplAdvisor : public ReplAdvisor {
protected:
    vector<list<UINT32> >                           lists;
    vector<map<UINT32, list<UINT32>::iterator> >    maps;

public:
    LruReplAdvisor(UINT32 rowNums) {
        for (INT32 i = 0; i < INT64(numRows); ++i) {
            lists.push_back(list<UINT32> ());
            maps.push_back(map<UINT32, list<UINT32>::iterator> ())
        }
    }

    VOID visit(UINT32 row, UINT32 key) {
        list<UINT32>& list = lists.at(row);
        map<UINT32, list<UINT32>::iterator>& map = maps.at(row);
        if (map.find(key)) != map.end()) {
            list.erase(map[key]);
        }
        list.push_front(key);
        map[key] = list.begin();
    }

    UINT32 victim(UINT32 row) {
        return lists.at(row).back();
    }
};

// Static Re-Reference Interval Prediction
class SrripAdvisor : public ReplAdvisor {
protected:
    UINT32                   maxRpv;
    vector<vector<UINT32> >  vecs;

public:
    LruReplAdvisor(UINT32 numRows, UINT32 associativity, UINT32 _maxRpv)
    : maxRpv(_maxRpv) {
        assert(associativity > 0 && );
        for (INT32 i = 0; i < INT64(numRows); ++i) {
            vecs.push_back(vector<UINT32> ());
            for (INT32 j = 0; j < INT64(associativity); ++j) {
                vecs.at(i).push_back(maxRpv);
            }
        }
    }

    VOID visit(UINT32 row, UINT32 idx) {
        vector<UINT32>& vec = vecs.at(row);
        vec.at(idx) = vec.at(idx) >= maxRpv ? maxRpv - 1 : 0;
    }

    UINT32 victim(UINT32 row) {
        vector<UINT32>& vec = vecs.at(row);
        UINT32 curMaxRpv = 0;
        UINT32 maxRpvIdx = 0;
        for (INT32 i = 0; i < INT64(associativity); ++i) {
            UINT32 rpv = vec.at(i);
            if (rpv >= maxRpv) return i;
            if (rpv > curMaxRpv) {
                curMaxRpv = rpv;
                maxRpvIdx = i;
            }
        }
        for (INT32 i = 0; i < INT64(associativity); ++i) {
            vec.at(i) += maxRpv - curMaxRpv;
        }
        return maxRpvIdx;
    }
};

UINT32 optReplCnt = 0;

// This is an page table alternative of LruReplAdvisor
// Instead of always evicting the least recently used page, its descendant pages are evicted firs
// Note: this class can only be used by page table
class LruPageTableReplAdvisor1 : public LruPageTableReplAdvisor {
private:
    UINT32 logPageSize;
    PageAllocator* pageAllocator;

    UINT32 getLeafPageAddr(UINT32 pageAddr) {
        Page* page = pageAllocator->pageAtAddress(pageAddr);
        assert(page);
        for (int i = 0; i < (1U << logPageSize) / sizeof(UINT32); ++i) {
            UINT32 childPageAddr = page->wordAt(i);
            if (childPageAddr != 0)
                return getLeafPageAddr(childPageAddr);
        }
        return pageAddr;
    }

public:
    LruPageTableReplAdvisor1(UINT32 numRows, UINT32 _logPageSize, PageAllocator* _pageAllocator)
    : LruPageTableReplAdvisor(numRows),
      logPageSize(_logPageSize),
      pageAllocator(_pageAllocator) {}

    UINT32 victim(UINT32 row) {
        UINT32 victimPageAddr = getLeafPageAddr(lists.at(row).back());
        optReplCnt += victimPageAddr != lists.at(row).back() ? 1 : 0;
        return victimPageAddr;
    }
};

class LruTLB {
private:
    UINT32        missCount;
    UINT32        hitCount;
    UINT32        flushCount;
    UINT32        logNumRows;
    UINT32        associativity;
    UINT32        logPageSize;
    UINT32        numRows;
    UINT32        rowMask;
    UINT32        pageNoMask;
    UINT32**      virtPageNo;
    UINT32**      phyPageAddr;
    BOOL**        validBits;
    ReplAdvisor*  repl;

    UINT32 getRow(UINT32 virtualAddr) {
        UINT32 row = (virtualAddr & rowMask) >> logPageSize;
        assert(row < numRows);
        return row;
    }

public:
    LruTLB(UINT32 logNumRowsParam, UINT32 associativityParam, UINT32 logPageSizeParam, ReplAdvisor* _repl )
    : logNumRows(logNumRowsParam),
      associativity(associativityParam),
      logPageSize(logPageSizeParam),
      numRows(1u << logNumRowsParam),
      rowMask( ((1u << logNumRowsParam) - 1) << logPageSize ),
      pageNoMask( ~((1u << logPageSize) - 1) ),
      repl(_repl) {
        assert(logNumRowsParam + logPageSize < 32);

        virtPageNo = new UINT32*[numRows];
        phyPageAddr = new UINT32*[numRows];
        validBits = new BOOL*[numRows];

        for (INT64 i = 0; i < INT64(numRows); ++i) {

            virtPageNo[i] = new UINT32[associativity];
            phyPageAddr[i] = new UINT32[associativity];
            validBits[i] = new BOOL[associativity];

            for (INT64 j = 0; j < INT64(associativity); ++j)
                validBits[i][j] = false;
        }

    }

    UINT32 physicalPage(UINT32 virtualAddr) {
        UINT32 row = getRow(virtualAddr);
        for (INT64 i = 0; i < INT64(associativity); ++i) {
            if (validBits[row][i] && (virtPageNo[row][i] == (virtualAddr & pageNoMask) ) ) {
                hitCount++;
                repl->visit(row, i);
                return phyPageAddr[row][i];
            }
        }
        missCount++;
        return 0;
    }

    void cacheTranslation(UINT32 virtualAddr, UINT32 translation) {
        UINT32 row = getRow(virtualAddr);
        UINT32 victim = 0;
        for (; ( victim < INT64(associativity) ) && validBits[row][victim]; ++victim) {}
        if (UINT32(victim) == UINT32(associativity)) { 
            victim = repl->victim(row);
	      }
	      assert(victim < associativity);
        virtPageNo[row][victim] = virtualAddr & pageNoMask;
        phyPageAddr[row][victim] = translation;
        validBits[row][victim] = true;
        repl->visit(row, victim);
    }

    UINT32 numMisses() { return missCount; }

    UINT32 numHits() { return hitCount; }

    UINT32 numFlushes() { return flushCount; }

    // full flush
    void flush() {
        flushCount++;
        for (INT64 i = 0; i < INT64(numRows); ++i) {
            for (INT64 j = 0; j < INT64(associativity); ++j)
                validBits[i][j] = false;
        }
    }

    // selective flush
    void flush(UINT32 virtualAddr) {
	      flushCount++;
        UINT32 row = getRow(virtualAddr);
        for (INT64 i = 0; i < INT64(associativity); ++i) {
            if (validBits[row][i] && (virtPageNo[row][i] == (virtualAddr & pageNoMask)) ) {
                validBits[row][i] = false;
                return;
            }
        }
    }
};

class Page {
private:
    bool _is_free;
    UINT32 _size;
    UINT32* _data;
    UINT32 _address;
    friend class PageAllocator;

public:

    Page(UINT32 size, UINT32 address) {
        _size = size;
        _is_free = true;
        _address = address;
        _data = new uint32_t[size];
    }

    UINT32 wordAt(UINT32 index) {
        assert(index < _size);
        assert(!_is_free);
        return _data[index];
    }

    VOID setWordAt(UINT32 word, UINT32 index) {
        assert(!_is_free);
        assert(index < _size);
        _data[index] = word;
    }

    UINT32 address() const { return _address; }
};

class PageAllocator {
private:
    UINT32 _pool_size;
    UINT32 _frame_size;
    std::map<UINT32, Page*> _page_map;
    std::stack<UINT32> _frame_stack;
    UINT32 _start_address;
public:

    PageAllocator(UINT32 logFrameSize, UINT32 logPoolSize) {
        Page* temp = NULL;
        _pool_size = 1U << logPoolSize;
        _frame_size = 1U << logFrameSize;
        _start_address = _frame_size;
        UINT32 frame_size_in_words = _frame_size / sizeof(UINT32);
        UINT32 address_ptr = _start_address;
        for (UINT32 i = 0; i < _pool_size; i++) {
            temp = new Page(frame_size_in_words, address_ptr);
            _frame_stack.push(address_ptr);
            _page_map[address_ptr] = temp;
            _page_map[address_ptr]->_is_free = true;
            address_ptr += _frame_size;
        }
    }

    UINT32 requestPage() {
        if (_frame_stack.empty())
            return 0;
        UINT32 page_addr = _frame_stack.top();
        _frame_stack.pop();
        _page_map[page_addr]->_is_free = false;
        return page_addr;
    }

    Page* pageAtAddress(UINT32 address) {
        if (_page_map.find(address) != _page_map.end())
            return _page_map[address];
//        return nullptr;
        return NULL;
    }

    void freePage(UINT32 page_addr) {
        if (_page_map.find(page_addr) != _page_map.end())
            _page_map[page_addr]->_is_free = true;
    }
};

struct PageInfo {
    UINT32 virtualAddr;
    UINT32 parentAddr;
    UINT32 row;

    PageInfo() {}

    PageInfo(UINT32 _virtualAddr, UINT32 _parentAddr, UINT32 _row)
    : virtualAddr(_virtualAddr),
      parentAddr(_parentAddr),
      row(_row) {}
};

class PageInfoTable {
public:
    PageInfoTable() {}

    PageInfo* get(UINT32 pageAddr) {
        if (table.find(pageAddr) != table.end()) {
            return &(table[pageAddr]);
        }
        return NULL;
    }

    VOID set(UINT32 pageAddr, UINT32 virtualAddr, UINT32 parentAddr, UINT32 row) {
        table[pageAddr] = PageInfo (virtualAddr, parentAddr, row);
        assert(table[pageAddr].virtualAddr == virtualAddr &&
                table[pageAddr].parentAddr == parentAddr &&
                table[pageAddr].row == row);
    }

    UINT32 erase(UINT32 pageAddr) {
        return table.erase(pageAddr);
    }

private:
    std::map<UINT32, PageInfo> table;
};

LruTLB*                 tlb;
Page*                   rootPage;
UINT32                  logPoolSize;
UINT32                  logPageSize;
UINT32                  frameSize;
PageAllocator*          pageAllocator;
PageInfoTable*          pageInfoTable;
ReplAdvisor*            pageTableReplAdvisor;

VOID flushPage(Page* page) {
    if (!page) return;
    for (INT64 i = 0; i < (1U << logPageSize) / sizeof(UINT32); ++i) {
        page->setWordAt(0, i);
    }
}

UINT32 pageTableWalk(UINT32 virtualAddr, UINT32 frameSize) {
    // variable declarations
    UINT32 numPageEntry = logPageSize - 2;
    UINT32 numPageTableBits = 32 - logPageSize;
    UINT32 level = numPageTableBits / numPageEntry;
    UINT32 pageTableMask = (1U << (logPageSize - 2)) - 1;
    UINT32 shift = 32;

    // make sure level is 2
    assert((level == 2) && (numPageTableBits % numPageEntry == 0));

    Page* curPage = rootPage;

    for (INT64 i = 0; i < INT64(level); ++i) {
        // calculate row
        shift -= logPageSize - 2;
        UINT32 row = (virtualAddr & (pageTableMask << shift) ) >> shift;

        // find the next page
        UINT32 nextPageAddr = curPage->wordAt(row);

        // page fault
        if (nextPageAddr == 0) {
            // request new page
            nextPageAddr = pageAllocator->requestPage();
            Page* newPage = pageAllocator->pageAtAddress(nextPageAddr);

            // page eviction
            if (!newPage) {
                newPage = pageAllocator->pageAtAddress(pageTableReplAdvisor->victim(0));
                nextPageAddr = newPage->address();

                PageInfo * pageInfo = pageInfoTable->get(nextPageAddr);
                // invalidate entry from parent page
                if (pageInfo->parentAddr != 0)
                    pageAllocator->pageAtAddress(pageInfo->parentAddr)->setWordAt(0, pageInfo->row); 
                // clear parent-child relations for child pages
                for (int j = 0; j < (1U << logPageSize) / sizeof(UINT32); ++j) {
                    UINT32 childPageAddr = newPage->wordAt(j);
                    if (childPageAddr != 0) {
                        pageInfoTable->get(childPageAddr)->parentAddr = 0;
                    }
                }
                tlb->flush(pageInfo->virtualAddr);
//                tlb->flush();
            }

            curPage->setWordAt(nextPageAddr, row);
            pageInfoTable->set(nextPageAddr, virtualAddr, curPage->address(), row);
            flushPage(newPage);
        }

        curPage = pageAllocator->pageAtAddress(nextPageAddr);

        // rootPage is excluded from replacement
        pageTableReplAdvisor->visit(0, curPage->address());
    }

    UINT32 translation = curPage->address();

    return translation;
}


// Virtual Address translation routine
void translateAddress(UINT32 virtualAddr) {
    UINT32 translation = tlb->physicalPage(virtualAddr);
    if (translation == 0) {
        translation = pageTableWalk(virtualAddr, logPageSize);
        tlb->cacheTranslation(virtualAddr, translation); 
    }
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    if (INS_IsMemoryRead(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)translateAddress, IARG_MEMORYREAD_EA, IARG_END);
    else if (INS_IsMemoryWrite(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)translateAddress, IARG_MEMORYWRITE_EA, IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    ofstream outfile;
    outfile.open(KnobOutputFile.Value().c_str());
    outfile << "Number of TLB hits: " << tlb->numHits() <<" | Number of TLB misses: "
        << tlb->numMisses() << " | Number of TLB flushes: " << tlb->numFlushes() << std::endl;
    outfile << "Number of opt replacement: " << cnt << std::endl;
    outfile.close();
}


// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    // Initialize configuration parameters
    logPageSize = KnobLogPageSize.Value();
    logPoolSize = KnobLogPoolSize.Value();
    logNumRows = KnobLogNumRows.Value();
    associativity = KnobAssociativity.Value();

    // Initialize Page table
    pageAllocator = new PageAllocator(logPageSize, logPoolSize);
    pageInfoTable = new PageInfoTable();
    rootPage = pageAllocator->pageAtAddress(pageAllocator->requestPage());
    flushPage(rootPage);

    // Initialize tlb
//    ReplAdvisor* tlbReplAdvisor = new RandomReplAdvisor(logNumRows);
    ReplAdvisor* tlbReplAdvisor = new LruReplAdvisor(logNumRows);
//    ReplAdvisor* tlbReplAdvisor = new SrripReplAdvisor(logNumRows);
    tlb = new LruTLB(logNumRows, associativity, logPageSize, tlbReplAdvisor);

    // Initialize page table replacement policy
    pageTableReplAdvisor = new RandomReplAdvisor(1);
//    pageTableReplAdvisor = new LruReplAdvisor(1);
//    pageTableReplAdvisor = new LruPageTableReplAdvisor1(1, logPageSize, pageAllocator);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
