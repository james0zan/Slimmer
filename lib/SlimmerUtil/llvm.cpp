#include "SlimmerUtil.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

/// Create a global value for the string.
///
GlobalVariable *StringToGV(const std::string& s, Module& module) {
  //create a constant string array and add a null terminator
  Constant *arr = ConstantDataArray::getString(
    module.getContext(), StringRef(s), true);
  return new GlobalVariable(
    module, arr->getType(), true,
    GlobalValue::InternalLinkage,
    arr, Twine("str"));
}

/// Given an LLVM value, insert a cast expressions or cast instructions as
/// necessary to make the value the specified type.
///
/// \param V - The value which needs to be of the specified type.
/// \param Ty - The type to which V should be casted (if necessary).
/// \param Name - The name to assign the new casted value (if one is created).
/// \param InsertPt - The point where a cast instruction should be inserted
/// \return An LLVM value of the desired type, which may be the original value
///         passed into the function, a constant cast expression of the passed
///         in value, or an LLVM cast instruction.
Value *LLVMCastTo(Value *V, Type *Ty, Twine Name, Instruction *InsertPt) {
  // Assert that we're not trying to cast a NULL value.
  assert (V && "castTo: trying to cast a NULL Value!\n");

  // Don't bother creating a cast if it's already the correct type.
  if (V->getType() == Ty)
    return V;

  // If it's a constant, just create a constant expression.
  if (Constant *C = dyn_cast<Constant>(V)) {
    Constant *CE = ConstantExpr::getZExtOrBitCast(C, Ty);
    return CE;
  }

  // Otherwise, insert a cast instruction.
  CastInst *ins;
  if (V->getType()->isPtrOrPtrVectorTy())
    ins = CastInst::CreatePointerCast(V, Ty, Name, InsertPt);
  else
    ins = CastInst::CreateZExtOrBitCast(V, Ty, Name, InsertPt);
    
  return ins;
}

/// Determines whether the function is a call to a function in one of the
/// Slimmer run-time libraries.
///
bool IsSlimmerFunction(Function *fun) {
  if (fun == nullptr) return false;

  std::string name = fun->stripPointerCasts()->getName().str();
  return (name == "slimmerCtor" ||
          name == "recordEndBB" ||
          name == "recordStartBB" ||
          name == "recordLoad" ||
          name == "recordStore" ||
          name == "recordLock" ||
          name == "recordUnlock" ||
          name == "recordCall" ||
          name == "recordReturn" ||
          name == "recordInit");
}

}