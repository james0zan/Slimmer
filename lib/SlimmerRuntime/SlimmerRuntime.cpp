#include "SlimmerRuntime.h"
#include "SlimmerUtil.h"

#include <atomic>

//===----------------------------------------------------------------------===//
//                        Trace Event Buffer
//===----------------------------------------------------------------------===//

class CircularBuffer {
public:
  void Init(const char *name);
  void CloseBufferFile();
  ~CircularBuffer() { CloseBufferFile(); }
  char* StartAppend(size_t length);
  void EndAppend();

private:
  bool inited;
  char *buffer, *compressed;
  int fd;

  size_t size; // Size of the event buffer in bytes
  size_t offset;
  std::atomic_flag append_lock = ATOMIC_FLAG_INIT;

  void Dump(const char *start, uint64_t length);
};

/// Init a buffer for buffering the trace.
///
/// \param name - the path to the trace file.
///
void CircularBuffer::Init(const char *name) {
  size = 16lu*1024lu*1024;
  
  buffer = (char *)malloc(size);
  compressed = (char *)malloc(LZ4_compressBound(size));
  assert(buffer && compressed && "Failed to malloc the event bufffer!\n");

  fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0640u);
  assert(fd != -1 && "Failed to open tracing file!\n");
  
  DEBUG("[SLIMMER] Opened trace file: %s\n", name);

  // Initialize all of the other fields.
  offset = 0;
  append_lock.clear(std::memory_order_release);
  inited = true;
}

/// Flush all the buffered log into the file,
/// and then close the file.
///
__attribute__((always_inline))
void CircularBuffer::CloseBufferFile() {
  if (!inited) return;
  DEBUG("[SLIMMER] Writing buffered data to trace file and closing.\n");
  
  int after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
  Dump(compressed, after_compress);
  
  // Dump(buffer, offset);

  // Create an end event to terminate the log.
  while (write(fd, &EndEventLabel, sizeof(EndEventLabel)) != 1);
  close(fd);
  inited = false;
}

__attribute__((always_inline))
char* CircularBuffer::StartAppend(size_t length) {
  while (append_lock.test_and_set(std::memory_order_acquire));

  if (offset + length > size) {
    int after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
    Dump(compressed, after_compress);

    // Dump(buffer, offset);
    offset = 0;
  }
  char *ret = buffer + offset;
  offset += length;
  return ret;
}

__attribute__((always_inline))
void CircularBuffer::EndAppend() {
  append_lock.clear(std::memory_order_release);
}

__attribute__((always_inline))
void CircularBuffer::Dump(const char *start, uint64_t length) {
  write(fd, &length, sizeof(length));
  uint64_t cur = 0;;
  while (cur < length) {
    size_t tmp = write(fd, start + cur, length - cur);
    if (tmp > 0) cur += tmp;
  }
}

//===----------------------------------------------------------------------===//
//                       Record and Helper Functions
//===----------------------------------------------------------------------===//

// This is the very event buffer used by all record functions
// Call EventBuffer::Init(...) before usage
static CircularBuffer event_buffer;
// The thread id
static uint64_t __thread local_tid = 0;

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

/// Append a BasicBlockEvent to the trace buffer.
///
/// \param id - the basic block ID.
///
__attribute__((always_inline))
void recordBasicBlockEvent(uint32_t id) {
  if (local_tid == 0) {
    // The first event of a thread will always be a BasicBlockEvent
    local_tid = syscall(SYS_gettid);
  }
  DEBUG("[BasicBlockEvent] id = %u\n", id);

  char *buffer = event_buffer.StartAppend(SizeOfBasicBlockEvent);
  
  *buffer = BasicBlockEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint32_t *)(buffer + 9)) = id;
  *(buffer + 13) = BasicBlockEventLabel;

  // memcpy(buffer, basic_block_event, SizeOfBasicBlockEvent);
  event_buffer.EndAppend();
}

/// Append a MemoryEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param addr - the starting address of the accessed memory.
/// \param length - the length of the accessed memory.
///
__attribute__((always_inline))
void recordMemoryEvent(uint32_t id, void *addr, uint64_t length) {
  char *buffer = event_buffer.StartAppend(SizeOfMemoryEvent);
  
  *buffer = MemoryEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint32_t *)(buffer + 9)) = id;
  (*(uint64_t *)(buffer + 13)) = (uint64_t)addr;
  (*(uint64_t *)(buffer + 21)) = length;
  *(buffer + 29) = MemoryEventLabel;

  event_buffer.EndAppend();
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

  char *buffer = event_buffer.StartAppend(SizeOfReturnEvent);

  *buffer = ReturnEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint32_t *)(buffer + 9)) = id;
  (*(uint64_t *)(buffer + 13)) = (uint64_t)fun;
  *(buffer + 21) = ReturnEventLabel;
  
  event_buffer.EndAppend(); 
}

/// Append an ArgumentEvent to the trace buffer.
///
/// \param arg - the pointer argument.
///
__attribute__((always_inline))
void recordArgumentEvent(void *arg) {
  DEBUG("[ArgumentEvent] arg = %p\n", arg);

  char *buffer = event_buffer.StartAppend(SizeOfArgumentEvent);

  *buffer = ArgumentEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint64_t *)(buffer + 9)) = (uint64_t)arg;
  *(buffer + 17) = ArgumentEventLabel;

  event_buffer.EndAppend();
}
