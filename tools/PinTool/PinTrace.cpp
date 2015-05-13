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

VOID BeforeCall(ADDRINT addr){
  uint64_t tid = PIN_GetTid();
  DEBUG("BeforeCall %lu %p\n", tid, (void*)addr);
}

VOID AfterCall(ADDRINT addr){
  uint64_t tid = PIN_GetTid();
  DEBUG("AfterCall %lu %p\n", tid, (void*)addr);
}

VOID ImageLoad(IMG img, VOID *v) {
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      RTN_Open(rtn);
      string name = RTN_Name(rtn);
      if (notTrace(name) || instrumentedFun.count(name) == 0) goto close_rtn;

      for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        if(INS_IsCall(ins)) {
          if (INS_IsDirectBranchOrCall(ins)) {
            ADDRINT target = INS_DirectBranchOrCallTargetAddress(ins);
            
            // string callName = Symbols[target];
            // if (notTrace(name) || instrumentedFun.count(name) != 0) continue;
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

    PIN_StartProgram();
    return 0;
}