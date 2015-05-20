#ifndef SLIMMER_UTIL_H
#define SLIMMER_UTIL_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <iostream>
#include <sstream>

#define DEBUG_SLIMMER_UTILL
#ifdef DEBUG_SLIMMER_UTILL
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) do {} while (false)
#endif

#define ERROR(...) fprintf(stderr, __VA_ARGS__)

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
bool IsSlimmerFunction(Function *fun);

}

//===----------------------------------------------------------------------===//
//                        Trace Event Buffer
//===----------------------------------------------------------------------===//

class EventBuffer {
public:
  void Init(const char *name);
  void CloseBufferFile();
  void Append(const char *event, size_t length);
  ~EventBuffer() { CloseBufferFile(); }
  /// Lock the buffer buffer.
  inline void Lock() {
    pthread_spin_lock(&lock);
  }
  /// Unlock the buffer buffer.
  inline void Unlock() {
    pthread_spin_unlock(&lock);
  }

private:
  bool inited;
  char *buffer;
  int fd;
  size_t offset;
  size_t size; // Size of the event buffer in bytes
  // the mutex of modifying the EntryBuffer
  pthread_spinlock_t lock;
};

//===----------------------------------------------------------------------===//
//                           Constants
//===----------------------------------------------------------------------===//
const float LOAD_FACTOR = 0.1;
const static size_t size_of_ptr = sizeof(void*);
// The first byte of each event,
// representing the type of the event.
const static char BasicBlockEventLabel = 0;
const static char MemoryEventLabel = 1;
const static char CallEventLabel = 2;
const static char ReturnEventLabel = 3;
const static char SyscallEventLabel = 4;
const static char EndEventLabel = 5;

#endif // SLIMMER_UTIL_H
