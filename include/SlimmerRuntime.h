#ifndef SLIMMER_RUNTIME_H
#define SLIMMER_RUNTIME_H

#include <stdint.h>

//===----------------------------------------------------------------------===//
//                           Forward declearation
//===----------------------------------------------------------------------===//
extern "C" void recordInit(const char *name);
extern "C" void recordAddLock();
extern "C" void recordBasicBlockEvent(uint32_t id);
extern "C" void recordMemoryEvent(uint32_t id, void *addr, uint64_t length);
extern "C" void recordCallEvent(uint32_t id, void *fun);
extern "C" void recordReturnEvent(uint32_t id, void *fun);


#endif // SLIMMER_RUNTIME_H