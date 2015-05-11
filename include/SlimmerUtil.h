#ifndef SLIMMER_UTIL_H
#define SLIMMER_UTIL_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"

#include <iostream>
#include <sstream>

//===----------------------------------------------------------------------===//
//                           Logging Function
//===----------------------------------------------------------------------===//
enum LogLevel {ERROR, INFO, DEBUG};
extern LogLevel _log_level;

class logIt {
public:
  logIt() {}
  void Bar();

  logIt(LogLevel log_level, const std::string lable) {
    _buffer << "[" 
      << (log_level == ERROR
        ? "ERROR"
        : (log_level == INFO ? "INFO" : "DEBUG")) 
      << "::" << lable << "] ";
  }
  
  template <typename T>
  logIt & operator<<(T const &value) {
    _buffer << value;
    return *this;
  }
  
  ~logIt() {
    _buffer << std::endl;
    std::cerr << _buffer.str();
    
  }

private:
  std::ostringstream _buffer;
};

#define LOG(level, lable) \
if (level > _log_level) ; \
else logIt(level, lable)

//===----------------------------------------------------------------------===//
//                           Base64 Function
//===----------------------------------------------------------------------===//
std::string base64_encode(unsigned char const* , unsigned int len);
std::string base64_decode(std::string const& s);

//===----------------------------------------------------------------------===//
//                           LLVM Helpers
//===----------------------------------------------------------------------===//
namespace llvm {
/// make_vector - Helper function which is useful for building temporary vectors
/// to pass into type construction of CallInst ctors.  This turns a null
/// terminated list of pointers (or other value types) into a real live vector.
///
template<typename T>
inline std::vector<T> make_vector(T A, ...) {
  va_list Args;
  va_start(Args, A);
  std::vector<T> Result;
  Result.push_back(A);
  while (T Val = va_arg(Args, T))
    Result.push_back(Val);
  va_end(Args);
  return Result;
}

GlobalVariable *StringToGV(const std::string& s, Module& module);
Value *LLVMCastTo(Value *V, Type *Ty, Twine Name, Instruction *InsertPt);

/// Determines whether the function is a call to a function in one of the
/// Slimmer run-time libraries.
bool IsSlimmerFunction(Function *fun);
}



#endif // SLIMMER_UTIL_H
