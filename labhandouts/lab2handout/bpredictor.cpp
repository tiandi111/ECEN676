#include <iostream>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <fstream>
#include <vector>
#include "pin.H"

using namespace std;

static UINT64 takenCorrect = 0;
static UINT64 takenIncorrect = 0;
static UINT64 notTakenCorrect = 0;
static UINT64 notTakenIncorrect = 0;

PIN_LOCK lock;

class BranchPredictor {

  public:
  BranchPredictor() { }

  virtual BOOL makePrediction(ADDRINT address) { return FALSE;};

  virtual void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {};

  virtual UINT32 bits() { return 0; };

  virtual VOID printStat() {};

};

// satCounter implements configurable saturating counter
struct satCounter {
    UINT32 bits;
    char cnt;
    satCounter(UINT32 _bits) : bits(_bits), cnt(0) {}
    inline void inc() {
      cnt += cnt == ((1 << bits) - 1)? 0 : 1;
    }
    inline void dec() {
      cnt -= cnt == 0? 0 : 1;
    }
    inline void clear() {
      cnt = 0;
    }
    inline void reset(char _cnt) {
      cnt = _cnt;
    }
    inline char mid() {
      return 1 << (bits - 1);
    }
    inline BOOL weak() {
      return (cnt == mid()) || (cnt == mid()-1);
    }
    inline char val() {
      return cnt;
    }
    inline BOOL pred() {
      return cnt >= (1 << (bits - 1));
    }
};

// GlobalBranchPredictor implements a branch predictor with global branch history
// and global pattern history
class GlobalBranchPredictor : public BranchPredictor {
  public:
    GlobalBranchPredictor(UINT32 _patternBits, UINT32 _counterBits)
    : patternBits(_patternBits), counterBits(_counterBits) {
      UINT32 bitsUsed = bits();
      printf("GlobalBranchPredictor total bits used: %d\n", bitsUsed);
      assert(bitsUsed <= 33000);
      pht = std::vector<satCounter>(1 << patternBits, satCounter(counterBits));
      patternBitMask = (1 << patternBits) - 1;
    }

    BOOL makePrediction(ADDRINT address) {
      UINT32 index = history & patternBitMask;
      return pht.at(index).pred();
    }

    void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
      UINT32 index = history & patternBitMask;
      if (takenActually) {
        pht.at(index).inc();
      } else {
        pht.at(index).dec();
      }
      history = history << 1 | takenActually;
    }

    // bits calculates total bits used
    UINT32 bits() {
      return patternBits + counterBits * (1 << patternBits);
    }

  private:
    UINT32 patternBits;
    UINT32 counterBits;
    UINT32 patternBitMask;
    UINT64 history;
    std::vector<satCounter> pht;
};

// PApBranchPredictor implements a configurable 2-level PAp branch predictor.
// That is, a 2-level adaptive branch predictor with per-address branch history
// table and per-address pattern history table.
class PApBranchPredictor : public BranchPredictor {
  public:
    PApBranchPredictor(UINT32 _patternBits, UINT32 _bhtSize, UINT32 _counterBits)
    : patternBits(_patternBits), bhtSize(_bhtSize), counterBits(_counterBits) {
      UINT32 bitsUsed = bits();
      assert(bitsUsed <= 33000);
      printf("PApBranchPredictor total bits used: %d\n", bitsUsed);
      pht = std::vector<std::vector<satCounter> >(bhtSize, std::vector<satCounter>(1 << patternBits, satCounter(counterBits)));
      bht = std::vector<UINT64>(bhtSize, 0);
      patternBitMask = (1 << patternBits) - 1;
    }

    BOOL makePrediction(ADDRINT address) {
      UINT32 idx = index(address);
      UINT32 ptn = pattern(idx);
      return pht.at(idx).at(ptn).pred();
    }

    void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
      UINT32 idx = index(address);
      UINT32 ptn = pattern(idx);
      if (takenActually) {
        pht.at(idx).at(ptn).inc();
      } else {
        pht.at(idx).at(ptn).dec();
      }
      bht.at(idx) = bht.at(idx) << 1 | takenActually;
    }

    // index returns the entry to branch history table and branch pattern table
    // given the instruction address
    UINT32 index(ADDRINT address) {
      return address % bht.size();
    }

    // pattern returns the history pattern given the index
    // note that the index must be computed by the index function
    UINT32 pattern(UINT32 index) {
      return bht.at(index) & patternBitMask;
    }

    // bits calculates total bits used
    UINT32 bits() {
      return patternBits * bhtSize + bhtSize * counterBits * (1 << patternBits);
    }

  private:
    UINT32 patternBits;
    UINT32 bhtSize;
    UINT32 counterBits;
    UINT32 patternBitMask;
    std::vector<std::vector<satCounter> > pht; // pattern history table
    std::vector<UINT64> bht;                // branch history table
};

// TournamentBranchPredictor implements a 2-entry tournament branch predictor
// with a per-address selector.
class TournamentBranchPredictor : public BranchPredictor {
  public:
    TournamentBranchPredictor(BranchPredictor* _bp1, BranchPredictor* _bp2, UINT32 _selectorSize, UINT32 _counterBits)
     : bp1(_bp1), bp2(_bp2), counterBits(_counterBits), selectorSize(_selectorSize)
    {
      UINT32 bitsUsed = bits();
      assert(bitsUsed <= 33000);
      printf("TournamentBranchPredictor total bits used: %d\n", bitsUsed);
      selector = std::vector<satCounter>(selectorSize, satCounter(counterBits));
    }

    BOOL makePrediction(ADDRINT address) {
      if (selector.at(address % selector.size()).pred()) {
        use1++;
        return bp1->makePrediction(address);
      } else {
        use2++;
        return bp2->makePrediction(address);
      }
      //return selector.at(address % selector.size()).pred() ?
      //        bp1->makePrediction(address) : bp2->makePrediction(address);
    }

    void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
      BOOL pred1 = bp1->makePrediction(address);
      BOOL pred2 = bp2->makePrediction(address);
      if (pred1 == takenActually && pred2 != takenActually) {
        selector.at(address % selector.size()).inc();
      }
      if (pred2 == takenActually && pred1 != takenActually) {
        selector.at(address % selector.size()).dec();
      }
      bp1->makeUpdate(takenActually, takenPredicted, address);
      bp2->makeUpdate(takenActually, takenPredicted, address);
    }

    UINT32 bits() {
      return bp1->bits() + bp2->bits() + selectorSize * counterBits;
    }

    VOID printStat() {
      float total = use1 + use2;
      printf("Use1: %f(%f)\nUse2: %f(%f)\nTotal: %f\n", use1, use1/total, use2, use2/total, total);
    }

  private:
    BranchPredictor* bp1;
    BranchPredictor* bp2;
    float use1;
    float use2;
    UINT32 counterBits;
    UINT32 selectorSize;
    std::vector<satCounter> selector;
};

// TAGEBranchPredictor implements a configurable TAgged GEometric history length predictor.
// More details see paper: https://www.jilp.org/vol8/v8paper1.pdf by Andre Seznec, Pierre Michaud
class TAGEBranchPredictor : public BranchPredictor {
  public:
    // hashFunc hash the branch address, branch history with a given history length
    // this function is used to index tagged branch prediction table
    typedef UINT64 (*hashFunc)(UINT64 address, std::vector<UINT64>& hist, std::vector<UINT64>& histMask, UINT32 indexBits);

    static UINT64 defaultHash(UINT64 address, std::vector<UINT64>& hist, std::vector<UINT64>& histMask, UINT32 indexBits) { 
      for (int i = 0; UINT64(i) < hist.size(); ++i) {
        address ^= hist.at(i) & histMask.at(i);
      }
      UINT64 indexBitMask = (1 << indexBits) - 1;
      UINT64 index = 0;
      for (int i = 0; i < 64; i += indexBits) {
        index ^= address & indexBitMask;
        address >>= indexBits;
      } 
      return index;
    }

    // TAGEBranchPredictor constructor
    TAGEBranchPredictor(
            UINT32 _alpha,
            UINT32 _T,
            UINT32 _totalHistLen,
            UINT32 _cntBits,
            UINT32 _tagBits,
            std::vector<UINT32> compIndexBits,
            BranchPredictor* _gbp,
            hashFunc _h)

    : alpha(_alpha), totalHistLen(_totalHistLen), gbp(_gbp) {

      if (!_h) _h = defaultHash; 
      h = _h;

      UINT32 histArrLen = _totalHistLen/64 + (_totalHistLen % 64 == 0 ? 0 : 1);
      hist = std::vector<UINT64>(histArrLen, 0);

      compStats = std::vector<stats>(compIndexBits.size()+1);

      for (int i = 0; UINT64(i) < compIndexBits.size(); ++i, _T *= alpha) {

        // the used history length(the first argument below) is calculated by:
        // used history length = alpha^(i-1) * _T 
        assert(_T <= _totalHistLen);
        comps.push_back(taggedBranchPredictor(compIndexBits.at(i), _cntBits, _tagBits));

        // initialize history mask
        std::vector<UINT64> histMask(histArrLen, 0);
        UINT64 setMask = 1;
        for (int j = 0; UINT64(j) < _T; ++j) {
          setMask = j % 64 == 0 ? 1 : setMask << 1;
          histMask.at(j / 64) |= setMask;
        }
        histMasks.push_back(histMask);
      }

      UINT32 bitsUsed = bits(); 
      std::cout<< "TAGEBranchPredictor total bits used: " << bitsUsed << std::endl;
      assert(bitsUsed <= 33000);

    }

    // makePrediction makes a prediction
    // the prediction is choose as the component predictor with the longest history length
    // whose tag matched with address, if no such predictor, the fallback predictor is used.
    BOOL makePrediction(ADDRINT address) {
      lastProvider = 0;
      lastProviderIndex = 0;
      lastAlter = 0;
      lastAlterIndex = 0;

      for (int i = comps.size()-1; i >= 0; --i) {
 
        UINT64 index = h(address, hist, histMasks.at(i), comps.at(i).getIndexBits()); 
        if (comps.at(i).match(index, address)) {
    
          if (lastProvider == 0) {
            lastProvider = i+1;
            lastProviderIndex = index;
          } else if (lastAlter == 0) {
            lastAlter = i+1;
            lastAlterIndex = index;
            break;
          }

        }

      } 

      if (lastProvider > 0) {
        taggedBranchPredictor& comp = comps.at(lastProvider-1);
        // use provider only when the entry is not newly allocated
        if (!comp.isWeak(lastProviderIndex) || (comp.useValue(lastProviderIndex) > 0)) {
          return comp.makePrediction(lastProviderIndex);
        }
      }
      return lastAlter > 0 ? comps.at(lastAlter-1).makePrediction(lastAlterIndex) : gbp->makePrediction(address);
    }

    // makeUpdate makes an update to the predictor
    // this function must be preceded with a call to makePrediction since it relies
    // on some internal states
    void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
      if (lastProvider > 0) { // provider is tagged component

        BOOL altPred = lastAlter > 0 ? comps.at(lastAlter-1).makePrediction(lastAlterIndex) : gbp->makePrediction(address);

        if (takenPredicted != altPred) {
          comps.at(lastProvider - 1).updateUse(lastProviderIndex, takenActually == takenPredicted);
        }

        comps.at(lastProvider - 1).updatePred(lastProviderIndex, takenActually); 

      }

      gbp->makeUpdate(takenActually, takenPredicted, address); // update fallback predictor anyway

      // statistics
      compStats.at(lastProvider).pred(takenActually == takenPredicted);
      overall.pred(takenActually == takenPredicted);

      // allocate an entry from a predictor with longer history length
      if (takenActually != takenPredicted && lastProvider < comps.size()) {

        // find two candidates
        UINT64 cand1 = 0;
        UINT64 cand2 = 0;
        UINT64 minProviderCand = lastProvider == 0 ? 0 : lastProvider;
	
        // compute indices for later use
        std::vector<UINT64> indices;
        for (int i = 0; UINT64(i) < comps.size(); ++i) {
          indices.push_back((*h)(address, hist, histMasks.at(i), comps.at(i).getIndexBits()));
        }
	
        for (int i = minProviderCand; UINT64(i) < comps.size(); ++i) {

          if (comps.at(i).useValue(indices.at(i)) > 0) continue;

          if (cand1 == 0) {
            cand1 = i;
          } else if (cand2 == 0) {
            cand2 = i;
            break;
          }

        }

        if (cand1 == 0 && cand2 == 0) { // no candidates, decrement all use counters

          for (int i = 0; UINT64(i) < comps.size(); ++i) {
            comps.at(i).updateUse(indices.at(i), false);
          }

        } else { // at least one candidates

          // if two candidates, choose the longer one with probability of 33.3%
          if (cand1 > 0 && cand2 > 0 && (rand() % 3 == 0)) {
            cand1 = cand2;
          }
          comps.at(cand1).allocate(indices.at(cand1), address);

        }

      }

      updateHist(takenActually);
    }

    // updateHist updates the branch history
    VOID updateHist(BOOL taken) {
      UINT64 lsb = taken ? 1 : 0;
      for (int i = 0; UINT64(i) < hist.size(); ++i) {
        UINT64 msb = (hist.at(i) & 0x8000000000000000) == 0 ? 0 : 1;
        hist.at(i) <<= 1;
        hist.at(i) |= lsb;
        lsb = msb;
      }
    }

    // bits returns total bits used
    UINT32 bits() {
      UINT32 total = gbp->bits() + totalHistLen;
      for (int i = 0; UINT64(i) < comps.size(); ++i) {
        total += comps.at(i).bits();
      }
      return total;
    }

    VOID printStat() {
        for (int i = 0; UINT64(i) < compStats.size(); ++i) {
          stats& st = compStats.at(i);
          std::cout<< "Component " << i << ", total: " << st.total << "("<< 100*float(st.total)/float(overall.total)  << "%)" << ", correct: " << st.correct << "(" << 100*float(st.correct)/float(st.total)  << "%)" <<std::endl;
        } 
    }

  private:
    // taggedBranchPredictor is an internal class that implements a tagged branch predictor
    class taggedBranchPredictor {
      public:
        taggedBranchPredictor(UINT32 _indexBits, UINT32 _cntBits, UINT32 _tagBits)
        : indexBits(_indexBits),
        cntBits(_cntBits),
        tagBits(_tagBits),
        useBits(2),
	indexBitMask((1 << indexBits) -1),
        entries(1 << indexBits, entry(cntBits, tagBits, useBits)) {}

        // match does a tag match
        BOOL match(UINT64 index, UINT64 target) {
          return entries.at(index).match(target);
        }

        BOOL makePrediction(UINT64 index) {
          return entries.at(index).pred();
        }

        BOOL isWeak(UINT64 index) {
          return entries.at(index).cnt.weak(); 
        }

        // allocate allocates an entry for the given target
        VOID allocate(UINT64 index, UINT64 target) {
          entries.at(index).alloc(target);
        }

        // update use counter from the last used entry, muse be preceded with a call
        // to makePrediction since it relies on lastUsedEntry
        VOID updateUse(UINT64 index, BOOL inc) {
          if (inc) {
            entries.at(index).use.inc();
          } else {
            entries.at(index).use.dec();
          }
        }

        // update prediction counter from the last used entry, muse be preceded with a call
        // to makePrediction since it relies on lastUsedEntry
        VOID updatePred(UINT64 index, BOOL takenActually) {
          if (takenActually) {
            entries.at(index).cnt.inc();
          } else {
            entries.at(index).cnt.dec();
          }
        }

        inline UINT64 size() {
          return entries.size();
        }

        inline char useValue(UINT64 index) {
          return entries.at(index).use.cnt;
        }

        inline UINT32 bits() {
          return entries.size() * (tagBits + cntBits + useBits);
        }

        inline UINT32 getIndexBits() { return indexBits; }

      private:
        // entry is the tagged prediction table entry
        struct entry {
            UINT64 tag;
            UINT64 tagMask;
            satCounter cnt;
            satCounter use;

            entry(UINT32 _cntBits, UINT32 _tagBits, UINT32 _useBits)
            : tagMask((1 << _tagBits) - 1),
	    cnt(_cntBits),
            use(_useBits) {}

            inline BOOL match(UINT64 target) { return (target & tagMask) == (tag & tagMask); }

            inline BOOL pred() { return cnt.pred(); }

            inline VOID alloc(UINT64 _tag) {
              tag = _tag;
              cnt.reset(cnt.mid());
              use.clear();
            }
        };

        UINT32 indexBits;
        UINT32 cntBits;
        UINT32 tagBits;
        UINT32 useBits;
        UINT32 indexBitMask;
        std::vector<entry> entries;
    };

    UINT32 alpha;                               // alpha factor used to calculate component history length
    UINT32 numComp;                             // number of component branch predictors
    UINT32 totalHistLen;                        // total number of branch history bits
    std::vector<UINT64> hist;                   // branch history bits
    std::vector<std::vector<UINT64> > histMasks; // branch history mask
    BranchPredictor* gbp;                 // fallback predictor
    std::vector<taggedBranchPredictor> comps;   // component predictors
    hashFunc h;                                // the function used to index tagged predication table

    // lastProvider and lastAlter are states from the last prediction(after calling makePrediction)
    // if > 0, lastProvider-1 (or lastAlter-1) is the component index
    UINT64 lastProvider;                      // the last predictor used
    UINT64 lastProviderIndex;
    UINT64 lastAlter;                         // the last alternate predictor
    UINT64 lastAlterIndex;

    // statistics
    struct stats {
      UINT64 total;
      UINT64 correct;

      VOID pred(BOOL res) {
        total++;
        correct += res ? 1 : 0;
      }
    };
    std::vector<stats> compStats;
    stats overall;
};

BranchPredictor* BP;


// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.out", "specify the output file name");


// In examining handle branch, refer to quesiton 1 on the homework
void handleBranch(ADDRINT ip, BOOL direction)
{
  PIN_GetLock(&lock, 0);
  BOOL prediction = BP->makePrediction(ip);
  BP->makeUpdate(direction, prediction, ip);
  if(prediction) {
    if(direction) {
      takenCorrect++;
    }
    else {
      takenIncorrect++;
    }
  } else {
    if(direction) {
      notTakenIncorrect++;
    }
    else {
      notTakenCorrect++;
    }
  }
  PIN_ReleaseLock(&lock);
}


void instrumentBranch(INS ins, void * v)
{   
  if(INS_IsBranch(ins) && INS_HasFallThrough(ins)) {
    INS_InsertCall(
      ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)handleBranch,
      IARG_INST_PTR,
      IARG_BOOL,
      TRUE,
      IARG_END); 

    INS_InsertCall(
      ins, IPOINT_AFTER, (AFUNPTR)handleBranch,
      IARG_INST_PTR,
      IARG_BOOL,
      FALSE,
      IARG_END);
  }
}


/* ===================================================================== */
VOID Fini(int, VOID * v)
{   
  ofstream outfile;
  outfile.open(KnobOutputFile.Value().c_str());
  outfile.setf(ios::showbase);
  outfile << "takenCorrect: "<< takenCorrect <<"  takenIncorrect: "<< takenIncorrect <<" notTakenCorrect: "<< notTakenCorrect <<" notTakenIncorrect: "<< notTakenIncorrect <<"\n";
  outfile.close();
  BP->printStat();
}


// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Make a new branch predictor
//    BranchPredictor* gbp = new GlobalBranchPredictor(10, 2);
    BranchPredictor* papbp = new PApBranchPredictor(2, 140, 3);
    UINT32 compIndexBitsArr[] = {9, 9, 9, 8, 8};
    BranchPredictor* tage = new TAGEBranchPredictor(
            2, 13,                     // alpha, T
            210, 3, 10,                // totalHistLen, cntBits, tagBits
            std::vector<UINT32>(compIndexBitsArr, compIndexBitsArr + sizeof(compIndexBitsArr) / sizeof(UINT32)), // numComp, compIndexBits
            papbp, NULL);               // gbp, hashFunc*/
//    BP = gbp;
//    BP = papbp;
    BP = tage;
//    BP = new TournamentBranchPredictor(papbp, gbp, 1024, 3);

    // Initialize pin
    PIN_Init(argc, argv);

    // Initialize lock
    PIN_InitLock(&lock);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(instrumentBranch, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

