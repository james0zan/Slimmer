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
// extern "C" void recordLock();
// extern "C" void recordUnlock();
// extern "C" void recordBBStart(unsigned id);
// extern "C" void recordLoad(unsigned id, void *p, uint64_t length);
// extern "C" void recordStore(unsigned id, void *p, uint64_t length);
// extern "C" void recordCall(unsigned id, void *fp);
// extern "C" void recordReturn(unsigned id, void *fp, uint64_t ret);

//===----------------------------------------------------------------------===//
//                       Record and Helper Functions
//===----------------------------------------------------------------------===//

void recordInit(const char *name) {
  DEBUG("Trace file: %s\n", name);
}
