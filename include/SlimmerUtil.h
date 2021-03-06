#ifndef SLIMMER_UTIL_H
#define SLIMMER_UTIL_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"

#include "lz4.h"

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

// #define SLIMMER_PRINT_CODE
// #define DEBUG_SLIMMER_UTILL
#ifdef DEBUG_SLIMMER_UTILL
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...)                                                             \
  do {                                                                         \
  } while (false)
#endif

#define ERROR(...) fprintf(stderr, __VA_ARGS__)

//===----------------------------------------------------------------------===//
//                           Logging Function
//===----------------------------------------------------------------------===//
enum LogLevel {
  ERROR,
  INFO,
  DEBUG
};
extern LogLevel _log_level;

class logIt {
public:
  logIt() {}
  void Bar();

  logIt(LogLevel log_level, const std::string lable) {
    _buffer << "["
            << (log_level == ERROR ? "ERROR"
                                   : (log_level == INFO ? "INFO" : "DEBUG"))
            << "::" << lable << "] ";
  }

  template <typename T> logIt &operator<<(T const &value) {
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

#define LOG(level, lable)                                                      \
  if (level > _log_level)                                                      \
    ;                                                                          \
  else                                                                         \
  logIt(level, lable)

//===----------------------------------------------------------------------===//
//                           Base64 Function
//===----------------------------------------------------------------------===//
std::string base64_encode(unsigned char const *, unsigned int len);
std::string base64_decode(std::string const &s);

//===----------------------------------------------------------------------===//
//                           LLVM Helpers
//===----------------------------------------------------------------------===//
namespace llvm {

/// make_vector - Helper function which is useful for building temporary vectors
/// to pass into type construction of CallInst ctors.  This turns a null
/// terminated list of pointers (or other value types) into a real live vector.
///
template <typename T> inline std::vector<T> make_vector(T A, ...) {
  va_list Args;
  va_start(Args, A);
  std::vector<T> Result;
  Result.push_back(A);
  while (T Val = va_arg(Args, T))
    Result.push_back(Val);
  va_end(Args);
  return Result;
}

GlobalVariable *StringToGV(const std::string &s, Module &module);
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
  inline void Lock() { pthread_spin_lock(&lock); }
  /// Unlock the buffer buffer.
  inline void Unlock() { pthread_spin_unlock(&lock); }

private:
  bool inited;
  char *buffer, *compressed;
  FILE *stream;
  size_t offset;
  size_t size; // Size of the event buffer in bytes
  // the mutex of modifying the EntryBuffer
  pthread_spinlock_t lock;
};

//===----------------------------------------------------------------------===//
//                           Constants
//===----------------------------------------------------------------------===//
// The first byte of each event,
// representing the type of the event.
const static char BasicBlockEventLabel = 0;
const static char MemoryEventLabel = 1;
const static char CallEventLabel = 2;
const static char ReturnEventLabel = 3;
const static char SyscallEventLabel = 4;
const static char ArgumentEventLabel = 5;
const static char MemsetEventLabel = 6;
const static char MemmoveEventLabel = 7;
const static char EndEventLabel = 125;
const static char PlaceHolderLabel = 126;

// The size of each evet
// Common part of each event: 2 label + Thread ID + ID
const static size_t SizeOfEventCommon = 2 + 8 + 4;
const static size_t SizeOfBasicBlockEvent = SizeOfEventCommon;
const static size_t SizeOfMemoryEvent = SizeOfEventCommon + 2 * 8;
const static size_t SizeOfReturnEvent = SizeOfEventCommon + 8;
const static size_t SizeOfArgumentEvent = 2 + 8 + 8;
const static size_t SizeOfMemsetEvent = SizeOfMemoryEvent;
const static size_t SizeOfMemmoveEvent = SizeOfEventCommon + 3 * 8;

#define COMPRESS_BLOCK_CNT 150
#define COMPRESS_BLOCK_SIZE 33554432lu

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
    PointerArg,
    Constant
  };
  std::vector<std::pair<DepType, uint32_t> > SSADependencies;

  // Instruction type
  enum InstType {
    NormalInst,
    LoadInst,
    StoreInst,
    CallInst,
    ExternalCallInst,
    ReturnInst,
    TerminatorInst,
    PhiNode,
    VarArg,
    AtomicInst,
    AllocaInst
  };
  InstType Type;

  // The called function name or [UNKNOWN] for CallInst.
  std::string Fun;
  // The basic block ID of the successors for TerminatorInst.
  std::vector<uint32_t> Successors;
  // The income basic block ID, income value type, incame value ID for PhiNode.
  std::vector<std::tuple<uint32_t, DepType, uint32_t> > PhiDependencies;
};

int GetEvent(bool backward, const char *cur, char &event_label,
             const uint64_t *&tid_ptr, const uint32_t *&id_ptr,
             const uint64_t *&addr_ptr, const uint64_t *&length_ptr,
             const uint64_t *&addr2_ptr);
void LoadInstrumentedFun(std::string path, std::set<std::string> &instrumented);
void LoadInstInfo(std::string path, std::vector<InstInfo> &info,
                  std::vector<std::vector<uint32_t> > &bb2ins);
bool IsImpactfulFunction(std::string name);

//===----------------------------------------------------------------------===//
//                           Dynamic Instruction
//===----------------------------------------------------------------------===//

struct DynamicInst {
  uint64_t TID;
  int32_t ID, Cnt;
  DynamicInst() {}
  DynamicInst(uint64_t tid, int32_t id, int32_t cnt)
      : TID(tid), ID(id), Cnt(cnt) {}
  bool operator==(const DynamicInst &rhs) {
    return TID == rhs.TID && ID == rhs.ID && Cnt == rhs.Cnt;
  }
  bool operator<(const DynamicInst &rhs) const {
    if (TID != rhs.TID)
      return TID < rhs.TID;
    if (ID != rhs.ID)
      return ID < rhs.ID;
    return Cnt < rhs.Cnt;
  }
};

//===----------------------------------------------------------------------===//
//                           CompressBuffer
//===----------------------------------------------------------------------===//

/// Stream buffer for appending compressed data
class CompressBuffer {
public:
  CompressBuffer(const char *path);

  ~CompressBuffer();

  /// Prepare a buffer larger than len
  void Prepare(size_t len);

  /// Append [ptr, ptr+len) to the buffer.
  void Append(void *ptr, size_t len);

private:
  char *buffer, *compressed;
  FILE *stream;
  size_t offset;
  size_t size; // Size of the event buffer in bytes
};

#endif // SLIMMER_UTIL_H
