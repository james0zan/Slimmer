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
  cl::init("/scratch1/zhangmx/SlimmerTrace"));
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
    std::fstream fInstrumentedFun;
    std::set<std::string> instrumentedFun;

    // Map a basic block to its ID
    std::map<BasicBlock*, uint32_t> bb2ID;
    // Map an instruction to its ID
    std::map<Instruction*, uint32_t> ins2ID;

    // Get a printable representation of the Value V
    std::string value2String(Value *v);
    // Return the common information of an instruction.
    std::string CommonInfo(Instruction *ins);

    // The instrumentation functions
    // void instrumentAddLock(Instruction *ins_ptr);
    void instrumentBasicBlock(BasicBlock *bb);
    void instrumentLoadInst(LoadInst *load_ins);
    void instrumentStoreInst(StoreInst *store_ptr);
    void instrumentCallInst(CallInst *call_ptr);

    // Functions for recording events during execution
    Function *recordInit;
    // Function *recordAddLock;
    Function *recordBasicBlockEvent;
    Function *recordMemoryEvent;
    // Function *recordCallEvent;
    Function *recordReturnEvent;
    Function *recordArgumentEvent;

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

/// Get the string representation of a LLVM Value.
///
/// \param v - the LLVM Value.
///
std::string SlimmerTrace::value2String(Value *v) {
  std::string s;
  raw_string_ostream rso(s);
  v->print(rso);
  return s;
}

/// Get the common part of an instruction's infomation,
/// no matter which type of instruction it is.
///
/// \param ins - the LLVM IR instruction.
///
std::string SlimmerTrace::CommonInfo(Instruction *ins) {
  std::string s;
  raw_string_ostream rso(s);
  
  // InstructionID:
  assert(ins2ID.count(ins) > 0);
  rso << ins2ID[ins] << "\n";

  // BasicBlockID,
  assert(bb2ID.count(ins->getParent()) > 0);
  rso << "\t" << bb2ID[ins->getParent()] << "\n";

  // Is pointer,
  rso << "\t" << ins->getType()->isPointerTy() << "\n";

  // Line of code, Path to the code file, 
  if (MDNode *dbg = ins->getMetadata("dbg")) {
    DILocation loc(dbg);
    rso << "\t" << loc.getLineNumber() << "\n";
    std::string path = loc.getDirectory().str() + "/" + loc.getFilename().str();
    rso << "\t" << base64_encode((unsigned char const*)path.c_str(), path.length()) << "\n";
  } else {
    rso << "\t-1\n\t[UNKNOWN]\n";
  }

  // The instruction's LLVM IR
  std::string ins_string = value2String(ins);
  // rso << "\t" << ins_string << ",\n"; // TODO: remove this line
  rso << "\t" << base64_encode((unsigned char const*)ins_string.c_str(), ins_string.length()) << "\n";
  
  // SSA dependencies
  rso << "\t" << ins->getNumOperands() << " ";
  for (unsigned index = 0; index < ins->getNumOperands(); ++index) {
    if (Instruction *tmp = dyn_cast<Instruction>(ins->getOperand(index))) {
      assert(ins2ID.count(tmp) > 0);
      rso << "Inst " << ins2ID[tmp] << " ";
    } else if (Argument *arg= dyn_cast<Argument>(ins->getOperand(index))) {
      rso << "Arg " << arg->getArgNo() << " ";
    } else { // A constant
      rso << "Constan 0 ";
    }
  }
  rso << "\n";
  return s;
}

bool SlimmerTrace::doInitialization(Module& module)  {
  LOG(DEBUG, "SlimmerTrace::doInitialization") << "Start";

  // Reserve the infomation directory and the files
  LOG(DEBUG, "SlimmerTrace::InfoDir") << InfoDir;
  system(("mkdir -p " + InfoDir).c_str());
  fInst.open(InfoDir + "/Inst", std::fstream::out);
  fInstrumentedFun.open(InfoDir + "/InstrumentedFun", std::fstream::out);
  
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

  // Lock the trace file
  // recordAddLock = cast<Function>(
  //   module.getOrInsertFunction("recordAddLock",
  //     VoidType, nullptr));

  // Recording a BasicBlockEvent.
  recordBasicBlockEvent = cast<Function>(
    module.getOrInsertFunction("recordBasicBlockEvent",
      VoidType, Int32Type, nullptr));

  // Recording a MemoryEvent
  recordMemoryEvent = cast<Function>(
    module.getOrInsertFunction("recordMemoryEvent",
      VoidType, Int32Type, VoidPtrType, Int64Type, nullptr));

  // // Recording the call to an uninstrumented function
  // recordCallEvent = cast<Function>(
  //   module.getOrInsertFunction("recordCallEvent",
  //     VoidType, Int32Type, VoidPtrType, nullptr));

  // Recording the return of an uninstrumented function
  recordReturnEvent = cast<Function>(
    module.getOrInsertFunction("recordReturnEvent",
      VoidType, Int32Type, VoidPtrType, nullptr));

  // Recording a pointer argument of an uninstrumented function
  recordArgumentEvent = cast<Function>(
    module.getOrInsertFunction("recordArgumentEvent",
      VoidType, VoidPtrType, nullptr));


  // Create the constructor
  appendCtor(module);
  LOG(DEBUG, "SlimmerTrace::doInitialization") << "End";
  return true;
}

/// Append a creator function for the tracing functions.
///
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

bool notTraced(Instruction *ins) {
  if (CallInst *call_ptr = dyn_cast<CallInst>(ins)) {
    Function *called_fun = call_ptr->getCalledFunction();
    if (called_fun->isIntrinsic()) {
      std::string fun_name = called_fun->stripPointerCasts()->getName().str();
      if (fun_name.substr(0, 9) == "llvm.dbg.") return true;
    }
  }
  return false;
}

bool SlimmerTrace::runOnModule(Module& module) { 
  LOG(DEBUG, "SlimmerTrace::runOnModule") << "Start";

  dataLayout = &getAnalysis<DataLayout>();

  // The basic block (instruction) ID is started from 0
  uint32_t bb_id = 0, ins_id = 0;
  std::vector<Instruction *> ins_list;
  for (Module::iterator fun_ptr = module.begin(), fun_end = module.end(); fun_ptr != fun_end; ++fun_ptr) {
    if (fun_ptr->isDeclaration() || IsSlimmerFunction(fun_ptr)) continue;
    std::string fun_name = fun_ptr->stripPointerCasts()->getName().str();
    instrumentedFun.insert(fun_name);
    fInstrumentedFun << fun_name << "\n";
    for (Function::iterator bb_ptr = fun_ptr->begin(), bb_end = fun_ptr->end(); bb_ptr != bb_end; ++bb_ptr) {
      bb2ID[bb_ptr] = bb_id++;
      for (BasicBlock::iterator ins_ptr = bb_ptr->begin(), ins_end = bb_ptr->end(); ins_ptr != ins_end; ++ins_ptr) {
        if (notTraced(ins_ptr)) continue;

        ins2ID[ins_ptr] = ins_id++;
        ins_list.push_back(ins_ptr);
      }
      instrumentBasicBlock(bb_ptr);
    }
  }

  for (auto &ins_ptr: ins_list) {
    fInst << CommonInfo(ins_ptr);
    if (LoadInst *load_ptr = dyn_cast<LoadInst>(ins_ptr)) {
      fInst << "\tLoadInst\n";
      instrumentLoadInst(load_ptr);
    } else if (StoreInst *store_ptr = dyn_cast<StoreInst>(ins_ptr)) {
      fInst << "\tStoreInst\n";
      instrumentStoreInst(store_ptr);
    } else if (TerminatorInst *terminator_ptr = dyn_cast<TerminatorInst>(ins_ptr)) {
      if (ReturnInst *return_ptr = dyn_cast<ReturnInst>(ins_ptr)) {
        fInst << "\tReturnInst\n\t" << terminator_ptr->getNumSuccessors() << " ";
      } else {
        fInst << "\tTerminatorInst\n\t" << terminator_ptr->getNumSuccessors() << " ";
      }
          
      // BasicBlockID of successor 1, BasicBlockID of successor 2, ...,
      for (unsigned index = 0; index < terminator_ptr->getNumSuccessors(); ++index) {
        BasicBlock *succ = terminator_ptr->getSuccessor(index);
        assert(bb2ID.count(succ) > 0);
        fInst << bb2ID[succ] << " ";
      }

      fInst << "\n";
    } else if (PHINode *phi_ptr = dyn_cast<PHINode>(ins_ptr)) {
      fInst << "\tPhiNode\n\t" << phi_ptr->getNumIncomingValues() << " ";

      // <Income BasicBlockID 1, Income value>, <Income BasicBlockID 2, Income value>, ...,
      for (unsigned index = 0; index < phi_ptr->getNumIncomingValues(); ++index) {
        BasicBlock *bb = phi_ptr->getIncomingBlock(index);
        assert(bb2ID.count(bb) > 0);
        fInst << " " << bb2ID[bb] << " ";

        if (Instruction *tmp = dyn_cast<Instruction>(phi_ptr->getIncomingValue(index))) {
          assert(ins2ID.count(tmp) > 0);
          fInst << "Inst " << ins2ID[tmp] << " ";
        } else if (Argument *arg= dyn_cast<Argument>(phi_ptr->getIncomingValue(index))) {
          fInst << "Arg " << arg->getArgNo() << " ";
        } else { // A constant
          fInst << "Constant 0 ";
        }
      }

      fInst << "\n";
    } else if (CallInst *call_ptr = dyn_cast<CallInst>(ins_ptr)) {
      Function *called_fun = call_ptr->getCalledFunction();
      if (!called_fun) {
        fInst << "\tCallInst\n\t[UNKNOWN]\n";
        // TODO
      } else if (called_fun->isIntrinsic()) {
        std::string fun_name = called_fun->stripPointerCasts()->getName().str();
        fInst << "\tCallInst\n\t" << fun_name << "\n";
        // TODO
      } else {
        std::string fun_name = called_fun->stripPointerCasts()->getName().str();
        
        if (instrumentedFun.count(fun_name) == 0) {
          fInst << "\tExternalCallInst\n\t" << fun_name << "\n";
          instrumentCallInst(call_ptr);
        } else {
          fInst << "\tCallInst\n\t" << fun_name << "\n";
        }
      }
    } else if (InvokeInst *invoke_ptr = dyn_cast<InvokeInst>(ins_ptr)) {
      Function *called_fun = invoke_ptr->getCalledFunction();
      if (!called_fun) {
        fInst << "\tCallInst\n\t[UNKNOWN]\n";
        // TODO
      } else {
        std::string fun_name = called_fun->stripPointerCasts()->getName().str();
        if (instrumentedFun.count(fun_name) == 0) {
          fInst << "\tExternalCallInst\n\t" << fun_name << "\n";
          // TODO
        } else {
          fInst << "\tCallInst\n\t" << fun_name << "\n";
        }
      }
    } else { // Normal Instruction
      fInst << "\tNormalInst\n";
    }
  }
  LOG(DEBUG, "SlimmerTrace::runOnModule") << "End";
  return false; 
}

/// Add a call to the recordAddLock function before an instruction.
///
/// \param ins_ptr - the LLVM IR instruction.
///
// void SlimmerTrace::instrumentAddLock(Instruction *ins_ptr) {
//   CallInst::Create(recordAddLock)->insertBefore(ins_ptr);
// }

/// Add a call to the recordBasicBlockEvent function
/// a the begining of a basic block.
///
/// \param bb - the basic block.
///
void SlimmerTrace::instrumentBasicBlock(BasicBlock *bb) {
  assert(bb2ID.count(bb) > 0);
  Value *bb_ID = ConstantInt::get(Int32Type, bb2ID[bb]);
  std::vector<Value *> args = make_vector<Value *>(bb_ID, 0);

  // Insert a call to recordBasicBlockEvent at the beginning of the basic block
  CallInst::Create(recordBasicBlockEvent, args, "", bb->getFirstInsertionPt());
}

/// Add a call to the recordAddLock function before a load,
/// and a call to the recordMemoryEvent function after it.
///
/// \param load_ptr - the load instruction.
///
void SlimmerTrace::instrumentLoadInst(LoadInst *load_ptr) {
  // instrumentAddLock(load_ptr);

  // Get the ID of the load instruction.
  assert(ins2ID.count(load_ptr) > 0);
  Value *load_id = ConstantInt::get(Int32Type, ins2ID[load_ptr]);
  // Cast the pointer into a void pointer type.
  Value *addr = load_ptr->getPointerOperand();
  addr = LLVMCastTo(addr, VoidPtrType, addr->getName(), load_ptr);
  // Get the size of the loaded data.
  uint64_t size = dataLayout->getTypeStoreSize(load_ptr->getType());
  Value *load_size = ConstantInt::get(Int64Type, size);
  
  std::vector<Value *> args = make_vector<Value *>(load_id, addr, load_size, 0);
  CallInst::Create(recordMemoryEvent, args)->insertAfter(load_ptr);
}

/// Add a call to the recordAddLock function before a store,
/// and a call to the recordMemoryEvent function after it.
///
/// \param store_ptr - the store instruction.
///
void SlimmerTrace::instrumentStoreInst(StoreInst *store_ptr) {
  // instrumentAddLock(store_ptr);

  // Get the ID of the load instruction.
  assert(ins2ID.count(store_ptr) > 0);
  Value *store_id = ConstantInt::get(Int32Type, ins2ID[store_ptr]);
  // Cast the pointer into a void pointer type.
  Value *addr = store_ptr->getPointerOperand();
  addr = LLVMCastTo(addr, VoidPtrType, addr->getName(), store_ptr);
  // Get the size of the loaded data.
  uint64_t size = dataLayout->getTypeStoreSize(store_ptr->getOperand(0)->getType());
  Value *store_size = ConstantInt::get(Int64Type, size);
  
  std::vector<Value *> args = make_vector<Value *>(store_id, addr, store_size, 0);
  CallInst::Create(recordMemoryEvent, args)->insertAfter(store_ptr);
}

/// Add a call to the recordReturnEvent function after the function call.
///
/// \param call_ptr - the call instruction.
///
void SlimmerTrace::instrumentCallInst(CallInst *call_ptr) {
  // Record pointer arguments
  for (unsigned index = 0; index < call_ptr->getNumArgOperands(); ++index) {
    Value *arg = call_ptr->getArgOperand(index);
    if (arg->getType()->isPointerTy()) {
      Value *arg_ptr = LLVMCastTo(arg, VoidPtrType, "", call_ptr);
      std::vector<Value *> args = make_vector<Value *>(arg_ptr, 0);
      CallInst::Create(recordArgumentEvent, args, "", call_ptr);
    }
  }

  // Get the ID of the call instruction.
  assert(ins2ID.count(call_ptr) > 0);
  Value *call_id = ConstantInt::get(Int32Type, ins2ID[call_ptr]);
  // Get the called function value and cast it to a void pointer.
  Value *fun_ptr = LLVMCastTo(call_ptr->getCalledValue(), VoidPtrType, "", call_ptr);

  std::vector<Value *> args = make_vector<Value *>(call_id, fun_ptr, 0);
  // CallInst::Create(recordCallEvent, args, "", call_ptr);  
  auto last_ins = CallInst::Create(recordReturnEvent, args);
  last_ins->insertAfter(call_ptr);
}



