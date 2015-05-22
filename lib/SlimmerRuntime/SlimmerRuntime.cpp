#include "SlimmerRuntime.h"
#include "SlimmerUtil.h"

//===----------------------------------------------------------------------===//
//                       Record and Helper Functions
//===----------------------------------------------------------------------===//

// This is the very event buffer used by all record functions
// Call EventBuffer::Init(...) before usage
static EventBuffer event_buffer;
// The thread id
static uint64_t __thread local_tid = 0;
static char __thread basic_block_event[97];
static char __thread memory_event[161 + size_of_ptr];
// static char __thread call_event[97 + size_of_ptr] = {CallEventLabel};
static char __thread return_event[97 + size_of_ptr];

/// A helper function which is registered at atexit()
///
static void finish() {
  // Make sure that we flush the entry/value buffer on exit.
  event_buffer.CloseBufferFile();
}

/// Signal handler to write only tracing data to file
///
/// \param signum - the signal number.
///
static void cleanup_only_tracing(int signum) {
  DEBUG("[SLIMMER] Abnormal termination, signal number %d\n", signum);
  exit(signum);
}

/// The init function of the whole trcing process.
/// It should be called only once at the begining of the program.
///
/// \param name - the path to the trace file.
///
void recordInit(const char *name) {
  // Initialize the event buffer by giving the path to the trace file.
  event_buffer.Init(name);

  // Register the signal handlers for flushing the tracing data to file
  // atexit(finish); // TODO: error: undefined reference to 'atexit'
  signal(SIGINT, cleanup_only_tracing);
  signal(SIGQUIT, cleanup_only_tracing);
  signal(SIGSEGV, cleanup_only_tracing);
  signal(SIGABRT, cleanup_only_tracing);
  signal(SIGTERM, cleanup_only_tracing);
  signal(SIGKILL, cleanup_only_tracing);
  signal(SIGILL, cleanup_only_tracing);
  signal(SIGFPE, cleanup_only_tracing);
}

/// A wrapper of locking the trace buffer.
///
void recordAddLock() {
  event_buffer.Lock();
}

/// Append a BasicBlockEvent to the trace buffer.
///
/// \param id - the basic block ID.
///
void recordBasicBlockEvent(uint32_t id) {
  if (local_tid == 0) {
    // The first event of a thread will always be a BasicBlockEvent
    local_tid = syscall(SYS_gettid);
    basic_block_event[96] = BasicBlockEventLabel;
    memcpy(basic_block_event, &local_tid, 64);
    memory_event[160 + size_of_ptr] = MemoryEventLabel;
    memcpy(memory_event, &local_tid, 64);
    return_event[96 + size_of_ptr] = ReturnEventLabel;
    memcpy(return_event, &local_tid, 64);
  }
  DEBUG("[BasicBlockEvent] id = %u\n", id);

  memcpy(&basic_block_event[64], &id, 32);
  event_buffer.Lock();
  event_buffer.Append(basic_block_event, 97);
  event_buffer.Unlock();
}

/// Append a MemoryEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param addr - the starting address of the accessed memory.
/// \param length - the length of the accessed memory.
///
void recordMemoryEvent(uint32_t id, void *addr, uint64_t length) {
  memcpy(&memory_event[64], &id, 32);
  memcpy(&memory_event[96], &addr, size_of_ptr);
  memcpy(&memory_event[96 + size_of_ptr], &length, 64);
  event_buffer.Append(memory_event, 161 + size_of_ptr);
  event_buffer.Unlock();

  DEBUG("[MemoryEvent] id = %u, addr = %p, len = %lu\n", id, addr, length);  
}

/// Append a ReturnEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param fun - the address of the called function.
///
void recordReturnEvent(uint32_t id, void *fun) {
  DEBUG("[ReturnEvent] id = %u, fun = %p\n", id, fun);

  memcpy(&return_event[64], &id, 32);
  memcpy(&return_event[96], &fun, size_of_ptr);
  event_buffer.Lock();
  event_buffer.Append(return_event, 97 + size_of_ptr);
  event_buffer.Unlock();  
}
