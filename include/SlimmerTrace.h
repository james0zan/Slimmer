#ifndef SLIMMER_TRACE_H
#define SLIMMER_TRACE_H

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
  class ModulePass;
  ModulePass *createSlimmerTracePass ();
  void initializeSlimmerTracePass(PassRegistry&);
}

#endif // SLIMMER_TRACE_H