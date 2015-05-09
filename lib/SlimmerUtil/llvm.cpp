#include "SlimmerUtil.h"

namespace llvm {

GlobalVariable *StringToGV(const std::string& s, Module& module) {
  //create a constant string array and add a null terminator
  Constant *arr = ConstantDataArray::getString(
    module.getContext(), StringRef(s), true);
  return new GlobalVariable(
    module, arr->getType(), true,
    GlobalValue::InternalLinkage,
    arr, Twine("str"));
}

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