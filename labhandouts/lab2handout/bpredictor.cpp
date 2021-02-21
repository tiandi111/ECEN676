#include <iostream>
#include <stdio.h>
#include <assert.h>
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
    BOOL pred() {
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
      assert(bitsUsed <= 33000);
      printf("GlobalBranchPredictor total bits used: %d\n", bitsUsed);
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

BranchPredictor* BP;


// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.out", "specify the output file name");


// In examining handle branch, refer to quesiton 1 on the homework
void handleBranch(ADDRINT ip, BOOL direction)
{
  BOOL prediction = BP->makePrediction(ip);
  BP->makeUpdate(direction, prediction, ip);
  PIN_GetLock(&lock, 0);
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
    BranchPredictor* gbp = new GlobalBranchPredictor(10, 2);
    BranchPredictor* papbp = new PApBranchPredictor(2, 1990, 3);
//    BP = gbp;
//    BP = papbp;
    BP = new TournamentBranchPredictor(papbp, gbp, 1024, 3);

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

