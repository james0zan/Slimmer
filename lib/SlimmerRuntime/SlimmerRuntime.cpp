#include "SlimmerRuntime.h"
#include "SlimmerUtil.h"

#include <atomic>
#include <semaphore.h>
#include <thread>
#include <vector>

//===----------------------------------------------------------------------===//
//                        Semaphore
//===----------------------------------------------------------------------===//

class Semaphore {
public:
  Semaphore() {}
  ~Semaphore() {
    sem_destroy(&m_sema);
  }

  inline void init(int initialCount = 0) {
    assert(initialCount >= 0);
    sem_init(&m_sema, 0, initialCount);
  }
    

  inline void wait() {
    int rc;
    do {
      rc = sem_wait(&m_sema);
    } while (rc == -1 && errno == EINTR);
  }

  inline void signal() {
    sem_post(&m_sema);
  }

  inline void signal(int count) {
    while (count-- > 0) {
      sem_post(&m_sema);
    }
  }

private:
  sem_t m_sema;

  Semaphore(const Semaphore& other) = delete;
  Semaphore& operator=(const Semaphore& other) = delete;
};

//===----------------------------------------------------------------------===//
//                        Trace Event Buffer
//===----------------------------------------------------------------------===//

class CircularBuffer {
public:
  void Init(const char *name);
  ~CircularBuffer() { CloseBufferFile(); }

  char* StartAppend(size_t length);
  void EndAppend();

  void Dump(const char *start, uint64_t length);
  void CloseBufferFile();

  size_t size; // Size of the each buffer in bytes
  char *buffer[COMPRESS_BLOCK_CNT], *compressed[COMPRESS_BLOCK_CNT];
  Semaphore empty_buffer[COMPRESS_BLOCK_CNT], empty_compressed[COMPRESS_BLOCK_CNT];
  Semaphore filled_buffer[COMPRESS_BLOCK_CNT], filled_compressed[COMPRESS_BLOCK_CNT];
  int after_compressed[COMPRESS_BLOCK_CNT];
  volatile bool dump_done, compress_done;

private:
  std::atomic_bool inited;
  FILE* stream;

  int cur_block; // The block ID that is currently writed
  size_t offset;

  std::thread *dump_thread, *compress_thread;
  
  std::atomic_flag append_lock = ATOMIC_FLAG_INIT;
};


/// Compressing the trace log
///
void CompressTrace(CircularBuffer *cb) {
  while (!cb->compress_done) {
    for (int i = 0; i < COMPRESS_BLOCK_CNT && !cb->compress_done; ++i) {
      cb->filled_buffer[i].wait();
      cb->empty_compressed[i].wait();

      cb->after_compressed[i] = 
        LZ4_compress_limitedOutput(
          (const char *)cb->buffer[i], 
          (char *)cb->compressed[i],
          cb->size, LZ4_compressBound(cb->size));
      
      cb->filled_compressed[i].signal();
      cb->empty_buffer[i].signal();
    }
  }
}

/// Dumping the compressed log
///
void DumpCompressed(CircularBuffer *cb) {
  while (!cb->dump_done) {
    for (int i = 0; i < COMPRESS_BLOCK_CNT && !cb->dump_done; ++i) {
      cb->filled_compressed[i].wait();

      cb->Dump(cb->compressed[i], cb->after_compressed[i]);

      cb->empty_compressed[i].signal();
    }
  }
}

/// Init a buffer for buffering the trace.
///
/// \param name - the path to the trace file.
///
void CircularBuffer::Init(const char *name) {
  size = COMPRESS_BLOCK_SIZE;
  
  for (int i = 0; i < COMPRESS_BLOCK_CNT; ++i) {
    buffer[i] = (char *)malloc(size);
    compressed[i] = (char *)malloc(LZ4_compressBound(size));
    assert(buffer[i] && compressed[i] && "Failed to malloc the event bufffer!\n");

    empty_buffer[i].init(i != 0);
    empty_compressed[i].init(1);

    filled_buffer[i].init();
    filled_compressed[i].init();
  }

  dump_thread = new std::thread(DumpCompressed, this);
  compress_thread = new std::thread(CompressTrace, this);
  
  stream = fopen(name, "wb");
  assert(stream && "Failed to open tracing file!\n");
  
  DEBUG("[SLIMMER] Opened trace file: %s\n", name);

  // Initialize all of the other fields.
  cur_block = offset = 0;
  append_lock.clear(std::memory_order_release);
  dump_done = compress_done = false;
  inited = true;
}

/// Flush all the buffered log into the file,
/// and then close the file.
///
void CircularBuffer::CloseBufferFile() {
  if (!inited) return;

  DEBUG("[SLIMMER] Writing buffered data to trace file and closing.\n");
  
  *StartAppend(1) = EndEventLabel;
  memset(buffer[cur_block] + offset, PlaceHolderLabel, size - offset);
  EndAppend();

  dump_done = compress_done = true;
  filled_buffer[cur_block].signal();
 
  
  compress_thread->join();
  dump_thread->join(); 

  fclose(stream);
  inited = false;
  append_lock.clear(std::memory_order_release);
  for (int i = 0; i < COMPRESS_BLOCK_CNT; ++i) {
    free(buffer[i]);
    free(compressed[i]);
  }
}

/// Declare an appending of event.
///
/// \param length - the length of the event.
/// \return - the starting address of the event.
///
inline char* CircularBuffer::StartAppend(size_t length) {
  while (append_lock.test_and_set(std::memory_order_acquire));

  // If the current block is full
  if (offset + length > size) {
    memset(buffer[cur_block] + offset, PlaceHolderLabel, size - offset);
    filled_buffer[cur_block++].signal();

    if (cur_block == COMPRESS_BLOCK_CNT) cur_block = 0;
    empty_buffer[cur_block].wait();
    offset = 0;
  }

  char *ret = buffer[cur_block] + offset;
  offset += length;
  return ret;
}

/// Declare an appending of event is ended.
///
inline void CircularBuffer::EndAppend() {
  append_lock.clear(std::memory_order_release);
}

/// Dump log to the file.
///
/// \param start - the starting address of the log.
/// \param length - the length of the log.
///
inline void CircularBuffer::Dump(const char *start, uint64_t length) {
  fwrite(&length, sizeof(length), 1, stream);
  uint64_t cur = 0;;
  while (cur < length) {
    size_t tmp = fwrite(start + cur, 1, length - cur, stream);
    if (tmp > 0) cur += tmp;
  }
  fwrite(&length, sizeof(length), 1, stream);
}

//===----------------------------------------------------------------------===//
//                       Record and Helper Functions
//===----------------------------------------------------------------------===//

// This is the very event buffer used by all record functions
// Call CircularBuffer::Init(...) before usage
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

/// Append a MemoryEvent for a store instruction to the trace buffer.
///
/// \param id - the instruction ID.
/// \param addr - the starting address of the accessed memory.
/// \param length - the length of the accessed memory.
/// \param value - the stored value
///
__attribute__((always_inline))
void recordStoreEvent(uint32_t id, void *addr, uint64_t length, int64_t value) {
  char *buffer = event_buffer.StartAppend(SizeOfMemoryEvent);
  // If it writes the same value as the original one,
  // it is an inefficacious write.
  if (*((int64_t*)addr) == value) {
    DEBUG("Inefficacious write!!!\n");
    length = 0;
  }

  *buffer = MemoryEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint32_t *)(buffer + 9)) = id;
  (*(uint64_t *)(buffer + 13)) = (uint64_t)addr;
  (*(uint64_t *)(buffer + 21)) = length;
  *(buffer + 29) = MemoryEventLabel;

  event_buffer.EndAppend();
  DEBUG("[MemoryEvent] id = %u, addr = %p, len = %lu, value = %lu\n", id, addr, length, value);  
}

/// Append a ReturnEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param fun - the address of the called function.
///
__attribute__((always_inline))
void recordReturnEvent(uint32_t id, void *fun) {
  DEBUG("[ReturnEvent] tid = %lu id = %u, fun = %p clock()=%lu\n", local_tid, id, fun, (uint64_t)clock());

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

/// Append an MemsetEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param addr - the starting address of the accessed memory.
/// \param length - the length of the accessed memory.
/// \param value - the stored value
///
__attribute__((always_inline))
void recordMemset(uint32_t id, void *addr, uint64_t length, uint8_t value) {
  DEBUG("[MemsetEvent] id = %u, addr = %p, len = %lu, value = %u\n", id, addr, length, value); 

  char *buffer = event_buffer.StartAppend(SizeOfMemsetEvent);
  // If it writes the same value as the original one,
  // it is an inefficacious write.
  bool changed = false;
  for (uint64_t i = 0; i < length; ++i) {
    if ((*((uint8_t*)addr + i)) != value) {
      changed = true;
      break;
    }
  }
  if (!changed) {
    DEBUG("Inefficacious write!!!\n");
    length = 0;
  }

  *buffer = MemsetEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint32_t *)(buffer + 9)) = id;
  (*(uint64_t *)(buffer + 13)) = (uint64_t)addr;
  (*(uint64_t *)(buffer + 21)) = length;
  *(buffer + 29) = MemsetEventLabel;

  event_buffer.EndAppend();
}

/// Append an MemmoveEvent to the trace buffer.
///
/// \param id - the instruction ID.
/// \param dest - the starting address of the destination memory.
/// \param src - the starting address of the source memory.
/// \param length - the length of the accessed memory.
///
__attribute__((always_inline))
void recordMemmove(uint32_t id, void *dest, void *src, uint64_t length) {
  DEBUG("[MemmoveEvent] id = %u, dest = %p, src = %p, len = %lu\n", id, dest, src, length); 

  char *buffer = event_buffer.StartAppend(SizeOfMemmoveEvent);
  // If it writes the same value as the original one,
  // it is an inefficacious write.
  if (memcmp(dest, src, length) == 0) {
    DEBUG("Inefficacious write!!!\n");
    length = 0;
  }

  *buffer = MemmoveEventLabel;
  (*(uint64_t *)(buffer + 1)) = local_tid;
  (*(uint32_t *)(buffer + 9)) = id;
  (*(uint64_t *)(buffer + 13)) = (uint64_t)dest;
  (*(uint64_t *)(buffer + 21)) = (uint64_t)src;
  (*(uint64_t *)(buffer + 29)) = length;
  *(buffer + 37) = MemmoveEventLabel;

  event_buffer.EndAppend();
}

