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

class BranchPredictor {

  public:
  BranchPredictor() { }

  virtual BOOL makePrediction(ADDRINT address) { return FALSE;};

  virtual void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {};

};

// myBranchPredictor implements a configurable 2-level PAp branch predictor.
// That is, a 2-level adaptive branch predictor with per-address branch history
// table and per-address pattern history table.
class myBranchPredictor: public BranchPredictor {
  public:
  myBranchPredictor(UINT32 _patternBits, UINT32 _bhtSize) : patternBits(_patternBits), bhtSize(_bhtSize) {
    UINT32 bitsUsed = bits();
    assert(bitsUsed <= 33000);
    printf("Total bits used: %d", bitsUsed);
    pht = std::vector<std::vector<cnt2bit> >(bhtSize, std::vector<cnt2bit>(1 << patternBits, cnt2bit()));
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
    bht.at(idx) = bht.at(idx) << 1 & takenActually;
  }

  UINT32 index(ADDRINT address) {
    return address % bht.size();
  }

  UINT32 pattern(UINT32 index) {
    return bht.at(index) & patternBitMask;;
  }

  UINT32 bits() {
    return patternBits * bhtSize + bhtSize * 2 * (1 << patternBits);
  }

  private:
  struct cnt2bit {
    char cnt;
    cnt2bit() : cnt(0) {}
    inline void inc() {
      cnt += cnt == 3? 0 : 1;
    }
    inline void dec() {
      cnt -= cnt == 0? 0 : 1;
    }
    inline void clear() {
      cnt = 0;
    }
    BOOL pred() {
      return cnt > 1;
    }
  };

  UINT32 patternBits;
  UINT32 bhtSize;
  UINT32 patternBitMask;
  std::vector<std::vector<cnt2bit> > pht;
  std::vector<UINT64> bht;
};

BranchPredictor* BP;


// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.out", "specify the output file name");


// In examining handle branch, refer to quesiton 1 on the homework
void handleBranch(ADDRINT ip, BOOL direction)
{
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
}


// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Make a new branch predictor
    BP = new myBranchPredictor(10, 16);

    // Initialize pin
    PIN_Init(argc, argv);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(instrumentBranch, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

