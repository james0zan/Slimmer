#ifndef SLIMMER_RUNTIME_H
#define SLIMMER_RUNTIME_H

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
extern "C" void recordBasicBlockEvent(uint32_t id);
extern "C" void recordMemoryEvent(uint32_t id, void *p, uint64_t length);

//===----------------------------------------------------------------------===//
//                           Constants
//===----------------------------------------------------------------------===//
const float LOAD_FACTOR = 0.1;
const static size_t size_of_ptr = sizeof(void*);
const static char BasicBlockEventLabel = 0;
const static char MemoryEventLabel = 1;
const static char EndEventLabel = 2;


#endif // SLIMMER_RUNTIME_H