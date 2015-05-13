#include "SlimmerRuntime.h"

//===----------------------------------------------------------------------===//
//                        Trace Event Cache
//===----------------------------------------------------------------------===//

class EventCache {
public:
  void Init(const char *name);
  void CloseCacheFile();
  void Append(const char *event, size_t length);
  ~EventCache() {
    CloseCacheFile();
  }
  inline void Lock() {
    pthread_spin_lock(&lock);
  }
  inline void Unlock() {
    pthread_spin_unlock(&lock);
  }

private:
  char *buffer;
  int fd;
  size_t offset;
  size_t size; // Size of the event cache in bytes
  // the mutex of modifying the EntryCache
  pthread_spinlock_t lock;
};

void EventCache::Init(const char *name) {
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  
  size = ((uint64_t)(pages * LOAD_FACTOR)) * page_size;
  
  buffer = (char *)malloc(size);
  assert(buffer && "Failed to malloc the event bufffer!\n");

  fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0640u);
  assert(fd != -1 && "Failed to open tracing file!\n");
  DEBUG("[SLIMMER] Opened trace file: %s\n", name);

  // Initialize all of the other fields.
  offset = 0;

  pthread_spin_init(&lock, 0);
}

void EventCache::CloseCacheFile() {
  DEBUG("[SLIMMER] Writing cache data to trace file and closing.\n");
  // Create an end event to terminate the log.
  Append(&EndEventLabel, sizeof(EndEventLabel));

  size_t cur = 0;
  while (cur < offset) {
    size_t tmp = write(fd, buffer + cur, offset - cur);
    if (tmp > 0) cur += tmp;
  }
  close(fd);
  pthread_spin_destroy(&lock);
}

void EventCache::Append(const char *event, size_t length) {
  if (offset + length > size) {
    size_t cur = 0;
    while (cur < offset) {
      size_t tmp = write(fd, buffer + cur, offset - cur);
      if (tmp > 0) cur += tmp;
    }
    offset = 0;
  }
  assert(offset + length <= size);

  memcpy(buffer + offset, event, length);
  offset += length;
}

//===----------------------------------------------------------------------===//
//                       Record and Helper Functions
//===----------------------------------------------------------------------===//

// This is the very event cache used by all record functions
// Call EventCache.Init(...) before usage
static EventCache event_cache;
// The thread id
static uint64_t __thread local_tid = 0;
static char __thread basic_block_event[97] = {BasicBlockEventLabel};
static char __thread memory_event[161 + size_of_ptr] = {MemoryEventLabel};
static char __thread call_event[97 + size_of_ptr] = {CallEventLabel};
static char __thread return_event[97 + size_of_ptr] = {ReturnEventLabel};

// helper function which is registered at atexit()
static void finish() {
  // Make sure that we flush the entry/value cache on exit.
  event_cache.CloseCacheFile();
}

// Signal handler to write only tracing data to file
static void cleanup_only_tracing(int signum) {
  DEBUG("[SLIMMER] Abnormal termination, signal number %d\n", signum);
  exit(signum);
}

void recordInit(const char *name) {
  // Initialize the event cache by giving the path to the trace file.
  event_cache.Init(name);

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

void recordAddLock() {
  event_cache.Lock();
}

void recordBasicBlockEvent(uint32_t id) {
  if (local_tid == 0) {
    // The first event of a thread will always be a BasicBlockEvent
    local_tid = syscall(SYS_gettid);
    memcpy(&basic_block_event[1], &local_tid, 64);
    memcpy(&memory_event[1], &local_tid, 64);
    memcpy(&call_event[1], &local_tid, 64);
    memcpy(&return_event[1], &local_tid, 64);
  }
  DEBUG("[BasicBlockEvent] id = %u\n", id);

  memcpy(&basic_block_event[65], &id, 32);
  event_cache.Lock();
  event_cache.Append(basic_block_event, 97);
  event_cache.Unlock();
}

void recordMemoryEvent(uint32_t id, void *addr, uint64_t length) {
  memcpy(&memory_event[65], &id, 32);
  memcpy(&memory_event[97], &addr, size_of_ptr);
  memcpy(&memory_event[97 + size_of_ptr], &length, 64);
  event_cache.Append(memory_event, 161 + size_of_ptr);
  event_cache.Unlock();

  DEBUG("[MemoryEvent] id = %u, addr = %p, len = %lu\n", id, addr, length);  
}

void recordCallEvent(uint32_t id, void *fun) {
  DEBUG("[CallEvent] id = %u, fun = %p\n", id, fun);

  memcpy(&call_event[65], &id, 32);
  memcpy(&call_event[97], &fun, size_of_ptr);
  event_cache.Lock();
  event_cache.Append(call_event, 97 + size_of_ptr);
  event_cache.Unlock();  
}

void recordReturnEvent(uint32_t id, void *fun) {
  DEBUG("[ReturnEvent] id = %u, fun = %p\n", id, fun);

  memcpy(&return_event[65], &id, 32);
  memcpy(&return_event[97], &fun, size_of_ptr);
  event_cache.Lock();
  event_cache.Append(return_event, 97 + size_of_ptr);
  event_cache.Unlock();  
}
