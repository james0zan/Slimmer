#include "SlimmerUtil.h"

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

const float LOAD_FACTOR = 0.2;

/// Init a buffer for buffering the trace.
///
/// \param name - the path to the trace file.
///
void EventBuffer::Init(const char *name) {
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  
  size = 16*1024*1024;//((uint64_t)(pages * LOAD_FACTOR)) * page_size;
  
  buffer = (char *)malloc(size);
  compressed = (char *)malloc(LZ4_compressBound(size));
  assert(buffer && compressed && "Failed to malloc the event bufffer!\n");

  fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0640u);
  assert(fd != -1 && "Failed to open tracing file!\n");
  
  DEBUG("[SLIMMER] Opened trace file: %s\n", name);

  // Initialize all of the other fields.
  offset = 0;

  pthread_spin_init(&lock, 0);

  inited = true;
}

/// Flush all the buffered log into the file,
/// and then close the file.
///
__attribute__((always_inline))
void EventBuffer::CloseBufferFile() {
  if (!inited) return;
  DEBUG("[SLIMMER] Writing buffered data to trace file and closing.\n");
  // Create an end event to terminate the log.
  Append(&EndEventLabel, sizeof(EndEventLabel));

  int after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
  write(fd, &after_compress, sizeof(after_compress));
  // fprintf(stderr, "1. Before %lu After %lu %lf\n", offset, after_compress, (double)offset/after_compress);
  size_t cur = 0;
  while (cur < after_compress) {
    size_t tmp = write(fd, compressed + cur, after_compress - cur);
    if (tmp > 0) cur += tmp;
  }
  
  // size_t cur = 0;
  // while (cur < offset) {
  //   size_t tmp = write(fd, buffer + cur, offset - cur);
  //   if (tmp > 0) cur += tmp;
  // }

  close(fd);
  pthread_spin_destroy(&lock);
  inited = false;
}

/// Append an event to the buffer.
///
/// \param event - the starting address of the event.
/// \param length - the length of the event.
///
__attribute__((always_inline))
void EventBuffer::Append(const char *event, size_t length) {
  if (offset + length > size) {
    int after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
    write(fd, &after_compress, sizeof(after_compress));
    // fprintf(stderr, "2. Before %lu After %lu %lf\n", offset, after_compress, (double)offset/after_compress);
    size_t cur = 0;
    while (cur < after_compress) {
      size_t tmp = write(fd, compressed + cur, after_compress - cur);
      if (tmp > 0) cur += tmp;
    }

    // size_t cur = 0;
    // while (cur < offset) {
    //   size_t tmp = write(fd, buffer + cur, offset - cur);
    //   if (tmp > 0) cur += tmp;
    // }

    offset = 0;
  }
  assert(offset + length <= size);

  memcpy(buffer + offset, event, length);
  offset += length;
}
