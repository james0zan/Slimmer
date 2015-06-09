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
#include <set>
#include <string>
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
// The first byte of each event,
// representing the type of the event.
const static char BasicBlockEventLabel = 0;
const static char MemoryEventLabel = 1;
const static char CallEventLabel = 2;
const static char ReturnEventLabel = 3;
const static char SyscallEventLabel = 4;
const static char ArgumentEventLabel = 5;
const static char EndEventLabel = 6;
// The size of each evet
// Common part of each event: 2 label + Thread ID + ID
const static size_t SizeOfEventCommon = 2 + 64 + 32;
const static size_t SizeOfBasicBlockEvent = SizeOfEventCommon;
const static size_t SizeOfMemoryEvent = SizeOfEventCommon + 2 * 64;
const static size_t SizeOfReturnEvent = SizeOfEventCommon + 64;
const static size_t SizeOfArgumentEvent = 2 + 64 + 64;

//===----------------------------------------------------------------------===//
//                           Routines
//===----------------------------------------------------------------------===//

struct InstInfo {
  // The instruction ID and the basic block ID.
  uint32_t ID, BB;

  // Is it a pointer or not?
  bool IsPointer;
  
  // The code infomation.
  int LoC;
  std::string File, Code;
  
  // SSA dependencies 
  enum DepType {
    Inst,
    Arg,
    Constant
  };
  std::vector<std::pair<DepType, uint32_t> > SSADependencies;

  // Instruction type
  enum {
    NormalInst,
    LoadInst,
    StoreInst,
    CallInst,
    TerminatorInst,
    PhiNode,
    VarArg
  } Type;

  // The called function name or [UNKNOWN] for CallInst.
  std::string Fun;
  // The basic block ID of the successors for TerminatorInst.
  std::vector<uint32_t> Successors;
  // The income basic block ID, income value type, incame value ID for PhiNode.
  std::vector<std::tuple<uint32_t, DepType, uint32_t> > PhiDependencies;
};

int GetEvent(bool backward, const char *cur, char& event_label, const uint64_t*& tid_ptr, const uint32_t*& id_ptr, const uint64_t*& addr_ptr, const uint64_t*& length_ptr);
void LoadInstrumentedFun(std::string path, std::set<std::string>& instrumented);
void LoadInstInfo(std::string path, std::vector<InstInfo>& info, std::vector<std::vector<uint32_t> >& bb2ins);

#endif // SLIMMER_UTIL_H
