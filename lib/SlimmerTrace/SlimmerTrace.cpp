#include "Util.hpp"
#include "SlimmerTrace.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

LogLevel _log_level = INFO;

namespace {
  struct SlimmerTrace : public ModulePass {
    static char ID;
    SlimmerTrace() : ModulePass(ID)  {
      PassRegistry& registry = (*PassRegistry::getPassRegistry());
      initializeDataLayoutPass(registry);
      initializeSlimmerTracePass(registry);
    }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<DataLayout>();
      // AU.addRequired<PostDominatorTree>();
      // AU.addRequired<DominatorTree>();
    }
    bool doInitialization(Module & M);
    bool runOnModule(Module &M);

    // Pointers to other passes
    const DataLayout *TD;
    
    // Get a printable representation of the Value V
    std::string getPrint(Value *v);

    // The instrumentation functions
    void instrumentLoadInst(LoadInst *LI);
  };
}

char SlimmerTrace::ID = 0;
INITIALIZE_PASS(SlimmerTrace, "slimmer-trace", "The Instrumentation Pass for Slimmer", false, false)
ModulePass *llvm::createSlimmerTracePass() { return new SlimmerTrace(); }

std::string SlimmerTrace::getPrint(Value *v) {
  std::string s;
  raw_string_ostream rso(s);
  v->print(rso);
  return s;
}

bool SlimmerTrace::doInitialization(Module & M)  {
  LOG(INFO, "SlimmerTrace::doInitialization") << "Start";
  LOG(INFO, "SlimmerTrace::doInitialization") << "End";
  return false;
}

bool SlimmerTrace::runOnModule(Module &M) { 
  LOG(INFO, "SlimmerTrace::runOnModule") << "Start";
  TD = &getAnalysis<DataLayout>();

  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) {
    if (!MI->isDeclaration()) {
      LOG(INFO, "SlimmerTrace::Function") << MI->stripPointerCasts()->getName().str();
      for (Function::iterator FI = MI->begin(), FE = MI->end(); FI != FE; ++FI) {
        for (BasicBlock::iterator I = FI->begin(), IE = FI->end(); I != IE; ++I) {
          if (isa<LoadInst>(I))
            instrumentLoadInst(cast<LoadInst>(I));
        }
      }
    }
  }
  LOG(INFO, "SlimmerTrace::runOnModule") << "End";
  return false; 
}

void SlimmerTrace::instrumentLoadInst(LoadInst *LI) {
  // Get the size of the loaded data.
  uint64_t size = TD->getTypeStoreSize(LI->getType());

  LOG(INFO, "SlimmerTrace::instrumentLoadInst") << getPrint(LI);
  LOG(INFO, "SlimmerTrace::instrumentLoadInst") << "Size: " << size;
}
