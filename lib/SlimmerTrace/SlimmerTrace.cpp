#include "SlimmerTrace.h"

#include <iostream>
using namespace llvm;

namespace {
  struct SlimmerTrace : public ModulePass {
    static char ID;
    SlimmerTrace() : ModulePass(ID)  {
      initializeSlimmerTracePass(*PassRegistry::getPassRegistry());
    }
    bool doInitialization(Module & M)  {
      std::cout << "[SlimmerTrace::doInitialization::Start]" <<std::endl;
      std::cout << "[SlimmerTrace::doInitialization::End]" <<std::endl;
      return false;
    }

    bool runOnModule(Module &M) { 
      std::cout << "[SlimmerTrace::runOnModule::Start]" <<std::endl;
      for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) {
        if (!MI->isDeclaration()) {
          std::cout << "[SlimmerTrace] " << MI->stripPointerCasts()->getName().str() << std::endl;
        }
      }
      std::cout << "[SlimmerTrace::runOnModule::End]" <<std::endl;
      return false; 
    }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }
  };
}

char SlimmerTrace::ID = 0;
INITIALIZE_PASS(SlimmerTrace, "slimmer-trace", "The Instrumentation Pass for Slimmer", false, false)
ModulePass *llvm::createSlimmerTracePass() { return new SlimmerTrace(); }
