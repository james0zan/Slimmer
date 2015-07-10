#ifndef SLIMMER_RUNTIME_H
#define SLIMMER_RUNTIME_H

#include <stdint.h>

//===----------------------------------------------------------------------===//
//                           Forward declearation
//===----------------------------------------------------------------------===//
extern "C" void recordInit(const char *name);
extern "C" void recordBasicBlockEvent(uint32_t id);
extern "C" void recordMemoryEvent(uint32_t id, void *addr, uint64_t length);
extern "C" void recordStoreEvent(uint32_t id, void *addr, uint64_t length, int64_t value);
extern "C" void recordCallEvent(uint32_t id, void *fun);
extern "C" void recordReturnEvent(uint32_t id, void *fun);
extern "C" void recordArgumentEvent(void *arg);

#endif // SLIMMER_RUNTIME_H