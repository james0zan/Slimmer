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
static char __thread basic_block_event[SizeOfBasicBlockEvent];
static char __thread memory_event[SizeOfMemoryEvent];
static char __thread return_event[SizeOfReturnEvent];
static char __thread argument_event[SizeOfArgumentEvent];

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
__attribute__((always_inline))
void recordAddLock() {
  event_buffer.Lock();
}

/// Append a BasicBlockEvent to the trace buffer.
///
/// \param id - the basic block ID.
///
__attribute__((always_inline))
void recordBasicBlockEvent(uint32_t id) {
  if (local_tid == 0) {
    // The first event of a thread will always be a BasicBlockEvent
    local_tid = syscall(SYS_gettid);
    basic_block_event[0] = basic_block_event[SizeOfBasicBlockEvent - 1] = BasicBlockEventLabel;
    (*(uint64_t *)(basic_block_event + 1)) = local_tid;
    memory_event[0] = memory_event[SizeOfMemoryEvent - 1] = MemoryEventLabel;
    (*(uint64_t *)(memory_event + 1)) = local_tid;
    return_event[0] = return_event[SizeOfReturnEvent - 1] = ReturnEventLabel;
    (*(uint64_t *)(return_event + 1)) = local_tid;
    argument_event[0] = argument_event[SizeOfArgumentEvent - 1] = ArgumentEventLabel;
    (*(uint64_t *)(argument_event + 1)) = local_tid;
  }
  DEBUG("[BasicBlockEvent] id = %u\n", id);

  (*(uint32_t *)(basic_block_event + 65)) = id;
  event_buffer.Lock();
  event_buffer.Append(basic_block_event, SizeOfBasicBlockEvent);
  event_buffer.Unlock();
}

/// Append a MemoryEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param addr - the starting address of the accessed memory.
/// \param length - the length of the accessed memory.
///
__attribute__((always_inline))
void recordMemoryEvent(uint32_t id, void *addr, uint64_t length) {
  (*(uint32_t *)(memory_event + 65)) = id;
  (*(uint64_t *)(memory_event + 97)) = (uint64_t)addr;
  (*(uint64_t *)(memory_event + 161)) = length;
  event_buffer.Append(memory_event, SizeOfMemoryEvent);
  event_buffer.Unlock();

  DEBUG("[MemoryEvent] id = %u, addr = %p, len = %lu\n", id, addr, length);  
}

/// Append a ReturnEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param fun - the address of the called function.
///
__attribute__((always_inline))
void recordReturnEvent(uint32_t id, void *fun) {
  DEBUG("[ReturnEvent] id = %u, fun = %p\n", id, fun);

  (*(uint32_t *)(return_event + 65)) = id;
  (*(uint64_t *)(return_event + 97)) = (uint64_t)fun;

  event_buffer.Lock();
  event_buffer.Append(return_event, SizeOfReturnEvent);
  event_buffer.Unlock();  
}

/// Append an ArgumentEvent to the trace buffer.
///
/// \param arg - the pointer argument.
///
__attribute__((always_inline))
void recordArgumentEvent(void *arg) {
  DEBUG("[ArgumentEvent] arg = %p\n", arg);

  (*(uint64_t *)(argument_event + 65)) = (uint64_t)arg;

  event_buffer.Lock();
  event_buffer.Append(argument_event, SizeOfArgumentEvent);
  event_buffer.Unlock();  
}
