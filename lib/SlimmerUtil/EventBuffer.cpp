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
  size = COMPRESS_BLOCK_SIZE;
  
  buffer = (char *)malloc(size);
  compressed = (char *)malloc(LZ4_compressBound(size));
  assert(buffer && compressed && "Failed to malloc the event bufffer!\n");

  stream = fopen(name, "wb");
  assert(stream && "Failed to open tracing file!\n");

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

  uint64_t after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
  fwrite(&after_compress, sizeof(after_compress), 1, stream);
  size_t cur = 0;
  while (cur < after_compress) {
    size_t tmp = fwrite(compressed + cur, 1, after_compress - cur, stream);
    if (tmp > 0) cur += tmp;
  }
  fwrite(&after_compress, sizeof(after_compress), 1, stream);
  
  // size_t cur = 0;
  // while (cur < offset) {
  //   size_t tmp = write(fd, buffer + cur, offset - cur);
  //   if (tmp > 0) cur += tmp;
  // }

  fclose(stream);
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
    uint64_t after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
    fwrite(&after_compress, sizeof(after_compress), 1, stream);
    size_t cur = 0;
    while (cur < after_compress) {
      size_t tmp = fwrite(compressed + cur, 1, after_compress - cur, stream);
      if (tmp > 0) cur += tmp;
    }
    fwrite(&after_compress, sizeof(after_compress), 1, stream);

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
