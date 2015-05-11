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
#include <sys/syscall.h>
#include <unistd.h>

#include <stack>
#include <unordered_map>

using namespace std;

#define DEBUG_SLIMMER_RUNTIME
#ifdef DEBUG_SLIMMER_RUNTIME
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) do {} while (false)
#endif

#define ERROR(...) fprintf(stderr, __VA_ARGS__)

//===----------------------------------------------------------------------===//
//                           Forward declearation
//===----------------------------------------------------------------------===//
extern "C" void recordInit(const char *name);
extern "C" void recordAddLock();
extern "C" void recordBasicBlockEvent(unsigned id);
extern "C" void recordMemoryEvent(unsigned id, void *p, uint64_t length);

//===----------------------------------------------------------------------===//
//                       Record and Helper Functions
//===----------------------------------------------------------------------===//

void recordInit(const char *name) {
  DEBUG("Trace file: %s\n", name);
}

void recordAddLock() {
  DEBUG("Lock the trace file\n");
}

void recordBasicBlockEvent(unsigned id) {
  DEBUG("BasicBlockEvent: %u\n", id);
}

void recordMemoryEvent(unsigned id, void *p, uint64_t length) {
  DEBUG("MemoryEvent: %u %p %llu\n", id, p, length);
}

