#include "SlimmerRuntime.h"

void readLLVMTrace(char *trace_file_name) {
  int trace = open(trace_file_name, O_RDONLY);
  assert(trace != -1 && "Failed to open tracing file!\n");

  char event_label;
  const static size_t bbevent_length = 97 - 1; // except the label
  const static size_t mevent_length = 161 + size_of_ptr - 1;
  char bbevent[bbevent_length], mevent[mevent_length];
  uint64_t *tid, *length;
  uint32_t *id;
  void **addr;

  while (read(trace, &event_label, sizeof(char))) {
    if (event_label == EndEventLabel) break;
    switch (event_label) {
    case BasicBlockEventLabel:
      read(trace, bbevent, bbevent_length);
      tid = (uint64_t *)(&bbevent[0]);
      id = (uint32_t *)(&bbevent[64]);
      printf("BasicBlockEvent: %lu\t%u\n", *tid, *id);
      break;
    case MemoryEventLabel:
      read(trace, mevent, mevent_length);
      tid = (uint64_t *)(&mevent[0]);
      id = (uint32_t *)(&mevent[64]);
      addr = (void **)(&mevent[96]);
      length = (uint64_t *)(&mevent[96 + size_of_ptr]);
      printf("MemoryEvent:    %lu\t%u\t%p\t%lu\n", *tid, *id, *addr, *length);
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  readLLVMTrace(argv[1]);
}