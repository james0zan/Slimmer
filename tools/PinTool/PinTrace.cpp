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

#define DEBUG_SLIMMER_PIN
#ifdef DEBUG_SLIMMER_PIN
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) do {} while (false)
#endif

#define ERROR(...) fprintf(stderr, __VA_ARGS__)

KNOB<string> KnobTraceFile(KNOB_MODE_WRITEONCE, "pintool",
    "p", "SlimmerPinTrace", "specify the path to the trace file");
KNOB<string> KnobInstrumentedFun(KNOB_MODE_WRITEONCE, "pintool",
    "i", "Slimmer", "specify the path to the InstrumentedFun file");
set<string> instrumentedFun;

inline bool notTrace(string name) {
  return (name == "slimmerCtor" ||
          name == "recordInit" ||
          name == "recordAddLock" ||
          name == "recordBasicBlockEvent" ||
          name == "recordMemoryEvent" ||
          name == "recordCallEvent" ||
          name == "recordReturnEvent" ||
          name == "memcpy" ||
          name == "memmove‘" ||
          name == "memset" ||
          name == "sqrt" ||
          name == "powi" ||
          name == "sin" ||
          name == "cos" ||
          name == "pow" ||
          name == "exp" ||
          name == "exp2" ||
          name == "log" ||
          name == "log10" ||
          name == "log2" ||
          name == "fma" ||
          name == "fabs" ||
          name == "copysign" ||
          name == "floor" ||
          name == "ceil" ||
          name == "trunc" ||
          name == "rint" ||
          name == "nearbyint" ||
          name == "round" ||
          name == "memset");

}

void readInstrumentedFun(string path, set<string> &instrumentedFun) {
  ifstream fInstrumentedFun(path);
  string name;
  while (fInstrumentedFun >> name) {
    instrumentedFun.insert(name);
  }
}

map<uint64_t, string> Symbols;
void getSymbols(string img_name, uint64_t start) {
  // DEBUG("Load IMG %s %p\n", img_name.c_str(), (void*)start);
  char readelf_commad[300];
  sprintf(readelf_commad,
    "readelf -Ws %s | awk '{if ($4 == \"FUNC\") print $2 " " $8}'",
    img_name.c_str());
  FILE *fres = popen(readelf_commad, "r");

  uint64_t addr; char fun_name[300];
  while (fscanf(fres, "%lx%s", &addr, fun_name) != EOF) {
    Symbols[addr + start] = fun_name;
    // DEBUG("Load Symbol: %lx %s\n", addr + start, fun_name);
  }
  fclose(fres);
}

static map<uint64_t, int> pdepth;
VOID BeforeCall(ADDRINT addr){
  uint64_t tid = PIN_GetTid();
  if ((++pdepth[tid]) == 1)
  DEBUG("BeforeCall %lu %p %s\n", tid, (void*)addr, Symbols[addr].c_str());
}
VOID AfterCall(ADDRINT addr){
  uint64_t tid = PIN_GetTid();
  if ((--pdepth[tid]) == 0)
    DEBUG("AfterCall %lu %p %s\n", tid, (void*)addr, Symbols[addr].c_str());
}

VOID SyscallEntry(THREADID t, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) {
  uint64_t tid = PIN_GetTid();
  int syscall_num = PIN_GetSyscallNumber(ctxt, std);
  
  if (pdepth[tid] == 0) return;

  if (syscall_num == 0 || syscall_num == 1
    || syscall_num == 9
    || syscall_num == 17 || syscall_num == 18
    || syscall_num == 20
    || (syscall_num >= 42 && syscall_num <= 50)
    || syscall_num == 54 || syscall_num == 62
    || syscall_num == 68 || syscall_num == 69
    || (syscall_num >= 82 && syscall_num <=95)
    || syscall_num == 202 || syscall_num == 295 || syscall_num == 296)
    DEBUG("SysCall: %lu %d\n", tid, syscall_num);
}

VOID ImageLoad(IMG img, VOID *v) {
  getSymbols(IMG_Name(img), IMG_LoadOffset(img));
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      RTN_Open(rtn);
      string name = RTN_Name(rtn);
      if (notTrace(name) || instrumentedFun.count(name) == 0) goto close_rtn;

      for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        if(INS_IsCall(ins)) {
          if (INS_IsDirectBranchOrCall(ins)) {
            ADDRINT target = INS_DirectBranchOrCallTargetAddress(ins);
            string called_fun = Symbols[target];
            if (notTrace(called_fun) || instrumentedFun.count(called_fun) != 0) continue;

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

int main(int argc, char *argv[]) {
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) return 1;
    readInstrumentedFun(KnobInstrumentedFun.Value(), instrumentedFun);

    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    PIN_StartProgram();
    return 0;
}