#include "SlimmerUtil.h"
#include "pin.H"
#include <set>
#include <map>
#include <iostream>
#include <fstream>
#include <pthread.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
using namespace std;

// #define DEBUG_SLIMMER_PIN
#ifdef DEBUG_SLIMMER_PIN
#define PINDEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define PINDEBUG(...)                                                          \
  do {                                                                         \
  } while (false)
#endif

KNOB<string> KnobTraceFile(KNOB_MODE_WRITEONCE, "pintool", "p",
                           "SlimmerPinTrace",
                           "specify the path to the trace file");
KNOB<string>
KnobInstrumentedFun(KNOB_MODE_WRITEONCE, "pintool", "i", "Slimmer",
                    "specify the path to the InstrumentedFun file");
set<string> instrumentedFun;

/// Identify the functions that are not needed to be traced.
///
/// \param name - the name of the function.
/// \return - return true if the function do not need to be traced.
///
inline bool notTrace(string name) {
  return (name == "slimmerCtor" || name == "recordInit" ||
          name == "recordAddLock" || name == "recordBasicBlockEvent" ||
          name == "recordMemoryEvent" || name == "recordStoreEvent" ||
          name == "recordCallEvent" || name == "recordReturnEvent" ||
          name == "recordArgumentEvent" || name == "recordMemset" ||
          name == "recordMemmove" || name == "memcpy" || name == "memmove‘" ||
          name == "memset" || name == "sqrt" || name == "powi" ||
          name == "sin" || name == "cos" || name == "pow" || name == "exp" ||
          name == "exp2" || name == "log" || name == "log10" ||
          name == "log2" || name == "fma" || name == "fabs" ||
          name == "copysign" || name == "floor" || name == "ceil" ||
          name == "trunc" || name == "rint" || name == "nearbyint" ||
          name == "round" || name == "memset");
}

/// Get the starting address of all the loaded functions.
///
/// \param img_name - the path to the loaded image.
/// \param start - the sstart address of the loaded image.
///
map<uint64_t, string> Symbols;
void getSymbols(string img_name, uint64_t start) {
  PINDEBUG("Load IMG %s %p\n", img_name.c_str(), (void *)start);
  char readelf_commad[300];
  sprintf(readelf_commad, "readelf -Ws %s | awk '{if ($4 == \"FUNC\") print $2 "
                          " $8}'",
          img_name.c_str());
  FILE *fres = popen(readelf_commad, "r");

  uint64_t addr;
  char fun_name[300];
  while (fscanf(fres, "%lx%s", &addr, fun_name) != EOF) {
    Symbols[addr + start] = fun_name;
    PINDEBUG("Load Symbol: %lx %s\n", addr + start, fun_name);
  }
  fclose(fres);
}

// Maintain the depth of call stack for each thread.
// Only uninstrumented functions are counted.
map<uint64_t, int> pdepth;
// This is the very event buffer used by all record functions
// Call EventBuffer::Init(...) before usage
EventBuffer pin_event_buffer;
char syscall_event[66];
char call_event[130];
char return_event[130];

/// Append a CallEvent before the first layer of external functions.
///
/// \param addr - the starting address of the function.
///
VOID BeforeCall(ADDRINT fun) {
  uint64_t tid = PIN_GetTid();

  pin_event_buffer.Lock();
  if ((++pdepth[tid]) == 1) {
    PINDEBUG("BeforeCall %lu %p %s\n", tid, (void *)fun, Symbols[fun].c_str());

    call_event[0] = call_event[129] = CallEventLabel;
    (*(uint64_t *)(call_event + 1)) = tid;
    (*(uint64_t *)(call_event + 65)) = (uint64_t)fun;
    pin_event_buffer.Append(call_event, 130);
  }
  pin_event_buffer.Unlock();
}

/// Append a ReturnEvent after the first layer of external functions.
///
/// \param addr - the starting address of the function.
///
VOID AfterCall(ADDRINT fun) {
  uint64_t tid = PIN_GetTid();

  pin_event_buffer.Lock();
  if ((--pdepth[tid]) == 0) {
    PINDEBUG("AfterCall %lu %p %s\n", tid, (void *)fun, Symbols[fun].c_str());

    return_event[0] = return_event[129] = ReturnEventLabel;
    (*(uint64_t *)(return_event + 1)) = tid;
    (*(uint64_t *)(return_event + 65)) = (uint64_t)fun;
    pin_event_buffer.Append(return_event, 130);
  }
  pin_event_buffer.Unlock();
}

/// Append a SyscallEvent for each output syscall.
///
VOID SyscallEntry(THREADID t, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) {
  uint64_t tid = PIN_GetTid();

  pin_event_buffer.Lock();
  if (pdepth[tid] == 0) {
    pin_event_buffer.Unlock();
    return;
  }

  PINDEBUG("SysCall: %lu %d\n", tid, syscall_num);
  syscall_event[0] = syscall_event[65] = SyscallEventLabel;
  (*(uint64_t *)(syscall_event + 1)) = tid;
  pin_event_buffer.Append(syscall_event, 66);

  pin_event_buffer.Unlock();
}

/// Instrument all the calling instructions that call an external function.
///
VOID ImageLoad(IMG img, VOID *v) {
  getSymbols(IMG_Name(img), IMG_LoadOffset(img));
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      RTN_Open(rtn);
      string name = RTN_Name(rtn);
      if (notTrace(name) || instrumentedFun.count(name) == 0)
        goto close_rtn;

      for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        if (INS_IsCall(ins)) {
          if (INS_IsDirectBranchOrCall(ins)) {
            ADDRINT target = INS_DirectBranchOrCallTargetAddress(ins);
            string called_fun = Symbols[target];
            if (notTrace(called_fun) || instrumentedFun.count(called_fun) != 0)
              continue;

            INS next = INS_Next(ins);
            if (INS_Valid(next)) {
              INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BeforeCall,
                             IARG_ADDRINT, target, IARG_END);
              INS_InsertCall(next, IPOINT_BEFORE, (AFUNPTR)AfterCall,
                             IARG_ADDRINT, target, IARG_END);
            }
          }
        }
      }
    close_rtn:
      RTN_Close(rtn);
    }
  }
}

/// Flush the trace buffer.
///
VOID Fini(INT32 code, VOID *p) { pin_event_buffer.CloseBufferFile(); }

int main(int argc, char *argv[]) {
  PIN_InitSymbols();
  if (PIN_Init(argc, argv))
    return 1;
  LoadInstrumentedFun(KnobInstrumentedFun.Value(), instrumentedFun);

  pin_event_buffer.Init(KnobTraceFile.Value().c_str());
  IMG_AddInstrumentFunction(ImageLoad, 0);
  PIN_AddSyscallEntryFunction(SyscallEntry, 0);
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
  return 0;
}
