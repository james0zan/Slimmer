#include "SlimmerTrace.h"
#include "SlimmerUtil.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/DebugInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <cstdint>
#include <fstream>
#include <map>
using namespace llvm;

LogLevel _log_level = DEBUG;
static cl::opt<std::string> TraceFilename(
  "trace-file",
  cl::desc("Name of the trace file"),
  cl::init("SlimmerTrace"));
static cl::opt<std::string> InfoDir(
  "slimmer-info-dir",
  cl::desc("The directory that reserves all the generated code infomation"),
  cl::init("Slimmer"));

namespace {
  struct SlimmerTrace : public ModulePass {
    static char ID;
    SlimmerTrace() : ModulePass(ID)  {
      PassRegistry& registry = (*PassRegistry::getPassRegistry());
      initializeDataLayoutPass(registry);
      initializeSlimmerTracePass(registry);
    }
    virtual void getAnalysisUsage(AnalysisUsage &au) const {
      au.addRequired<DataLayout>();
      // au.addRequired<PostDominatorTree>();
      // au.addRequired<DominatorTree>();
    }
    bool doInitialization(Module& module);
    bool runOnModule(Module& module);

    // Append the llvm.global_ctors for adding the initializing function
    void appendCtor(Module& module);

    // Pointers to other passes
    const DataLayout *dataLayout;
    
    // The output files
    std::fstream fInst;

    // Map a basic block to its ID
    std::map<BasicBlock*, uint32_t> bb2ID;
    // Map an instruction to its ID
    std::map<Instruction*, uint32_t> ins2ID;

    // Get a printable representation of the Value V
    std::string value2String(Value *v);
    // Return the info string of an instruction.
    std::string ins2InfoString(Instruction *ins);

    // The instrumentation functions
    void instrumentLoadInst(LoadInst *load_ins);
    // TODO

    // Functions for recording events during execution
    Function *recordInit;
    // Function *recordLoad;
    // TODO

    // Integer types
    Type *Int8Type;
    Type *Int32Type;
    Type *Int64Type;
    Type *VoidType;
    Type *VoidPtrType;
  };
}

char SlimmerTrace::ID = 0;
INITIALIZE_PASS(SlimmerTrace, "slimmer-trace", "The Instrumentation Pass for Slimmer", false, false)
ModulePass *llvm::createSlimmerTracePass() { return new SlimmerTrace(); }

std::string SlimmerTrace::value2String(Value *v) {
  std::string s;
  raw_string_ostream rso(s);
  v->print(rso);
  return s;
}

std::string SlimmerTrace::ins2InfoString(Instruction *ins) {
  std::string s;
  raw_string_ostream rso(s);
  
  // InstructionID:
  assert(ins2ID.count(ins) > 0);
  rso << ins2ID[ins] << ": ";

  // BasicBlockID,
  assert(bb2ID.count(ins->getParent()) > 0);
  rso << bb2ID[ins->getParent()] << ", ";

  // Path to the code file, Line of code,
  if (MDNode *dbg = ins->getMetadata("dbg")) {
    DILocation loc(dbg);
    std::string path = loc.getDirectory().str() + "/" + loc.getFilename().str();
    rso << base64_encode((unsigned char const*)path.c_str(), path.length()) << ", ";
    rso << loc.getLineNumber() << ", ";
  } else {
    rso << "[UNKNOWN], -1, ";
  }

  // The instruction's LLVM IR
  std::string ins_string = value2String(ins);
  rso << base64_encode((unsigned char const*)ins_string.c_str(), ins_string.length());
  return s;
}

bool SlimmerTrace::doInitialization(Module& module)  {
  LOG(DEBUG, "SlimmerTrace::doInitialization") << "Start";
  
  // Reserve the infomation directory and the files
  LOG(DEBUG, "SlimmerTrace::InfoDir") << InfoDir;
  system(("mkdir -p " + InfoDir).c_str());
  fInst.open(InfoDir + "/Inst", std::fstream::out);
  
  // Get references to the different types that we'll need.
  Int8Type  = IntegerType::getInt8Ty(module.getContext());
  Int32Type = IntegerType::getInt32Ty(module.getContext());
  Int64Type = IntegerType::getInt64Ty(module.getContext());
  VoidPtrType = PointerType::getUnqual(Int8Type);
  VoidType = Type::getVoidTy(module.getContext());

  // The initialization function for preparing the trace file
  recordInit = cast<Function>(
    module.getOrInsertFunction("recordInit",
      VoidType, VoidPtrType, nullptr));

  // // Lock the trace file
  // RecordLock = cast<Function>(module.getOrInsertFunction("recordLock",
  //                                                   VoidType,
  //                                                   nullptr));

  // // Unlock the trace file
  // RecordUnlock = cast<Function>(module.getOrInsertFunction("recordUnlock",
  //                                                     VoidType,
  //                                                     nullptr));

  // // Recording the start of a basic block in a critical section.
  // RecordBBStart = cast<Function>(module.getOrInsertFunction("recordBBStart",
  //                                                      VoidType,
  //                                                      Int32Type,
  //                                                      nullptr));

  // Recording the loads then unlock the trace file
  // RecordLoad = cast<Function>(module.getOrInsertFunction("recordLoad",
  //                                                   VoidType,
  //                                                   Int32Type,
  //                                                   VoidPtrType,
  //                                                   Int64Type,
  //                                                   nullptr));

  // // Recording the store then unlock the trace file
  // RecordStore = cast<Function>(module.getOrInsertFunction("recordStore",
  //                                                    VoidType,
  //                                                    Int32Type,
  //                                                    VoidPtrType,
  //                                                    Int64Type,
  //                                                    nullptr));

  // // Recording the start of a function in a critical section.
  // RecordCall = cast<Function>(module.getOrInsertFunction("recordCall",
  //                                                   VoidType,
  //                                                   Int32Type,
  //                                                   VoidPtrType,
  //                                                   nullptr));

  // // Recording the end of a function in a critical section.
  // RecordReturn = cast<Function>(module.getOrInsertFunction("recordReturn",
  //                                                     VoidType,
  //                                                     Int32Type,
  //                                                     VoidPtrType,
  //                                                     Int64Type,
  //                                                     nullptr));

  // Create the constructor
  appendCtor(module);
  LOG(DEBUG, "SlimmerTrace::doInitialization") << "End";
  return true;
}

void SlimmerTrace::appendCtor(Module& module) {
  // Create the ctor function.
  Function *ctor = cast<Function>(
    module.getOrInsertFunction("slimmerCtor",
        VoidType, nullptr));
  assert(ctor && "Somehow created a non-function ctor function!\n");

  // Make the ctor function internal and non-throwing.
  ctor->setDoesNotThrow();
  ctor->setLinkage(GlobalValue::InternalLinkage);

  // Add a call in the new constructor function to the initialization function.
  BasicBlock *entry = BasicBlock::Create(module.getContext(), "entry", ctor);
  Constant *trace_file = StringToGV(TraceFilename, module);
  trace_file = ConstantExpr::getZExtOrBitCast(trace_file, VoidPtrType);
  CallInst::Create(recordInit, trace_file, "", entry);

  // Add a return instruction at the end of the basic block.
  ReturnInst::Create(module.getContext(), entry);

  appendToGlobalCtors(module, ctor, 0);
}

bool SlimmerTrace::runOnModule(Module& module) { 
  LOG(DEBUG, "SlimmerTrace::runOnModule") << "Start";
  dataLayout = &getAnalysis<DataLayout>();

  // The basic block (instruction) ID is started from 0
  uint32_t bb_id = 0, ins_id = 0;
  for (Module::iterator fun_ptr = module.begin(), fun_end = module.end(); fun_ptr != fun_end; ++fun_ptr) {
    if (fun_ptr->isDeclaration() || IsSlimmerFunction(fun_ptr)) continue;
    LOG(INFO, "SlimmerTrace::FunctionName") << fun_ptr->stripPointerCasts()->getName().str();
    
    for (Function::iterator bb_ptr = fun_ptr->begin(), bb_end = fun_ptr->end(); bb_ptr != bb_end; ++bb_ptr) {
      bb2ID[bb_ptr] = bb_id++;
      for (BasicBlock::iterator ins_ptr = bb_ptr->begin(), ins_end = bb_ptr->end(); ins_ptr != ins_end; ++ins_ptr) {
        ins2ID[ins_ptr] = ins_id++;
        fInst << ins2InfoString(ins_ptr) << "\n";

        if (isa<LoadInst>(ins_ptr))
          instrumentLoadInst(cast<LoadInst>(ins_ptr));
      }
    }
  }
  LOG(DEBUG, "SlimmerTrace::runOnModule") << "End";
  return false; 
}

void SlimmerTrace::instrumentLoadInst(LoadInst *load_ins) {
  // Get the size of the loaded data.
  uint64_t size = dataLayout->getTypeStoreSize(load_ins->getType());

  LOG(INFO, "SlimmerTrace::instrumentLoadInst") << value2String(load_ins);
  LOG(INFO, "SlimmerTrace::instrumentLoadInst") << "Size: " << size;
}
