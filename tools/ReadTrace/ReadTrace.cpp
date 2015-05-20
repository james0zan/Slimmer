#include "SlimmerUtil.h"

void readLLVMTrace(char *trace_file_name) {
  int trace = open(trace_file_name, O_RDONLY);
  assert(trace != -1 && "Failed to open tracing file!\n");

  char event_label;
  const static size_t bbevent_length = 97 - 1; // except the label
  const static size_t mevent_length = 161 + size_of_ptr - 1;
  const static size_t fevent_length = 97 + size_of_ptr - 1;
  char bbevent[bbevent_length], mevent[mevent_length], fevent[fevent_length];
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
      printf("MemoryEvent:     %lu\t%u\t%p\t%lu\n", *tid, *id, *addr, *length);
      break;
    // case CallEventLabel:
    //   read(trace, fevent, fevent_length);
    //   tid = (uint64_t *)(&fevent[0]);
    //   id = (uint32_t *)(&fevent[64]);
    //   addr = (void **)(&fevent[96]);
    //   printf("CallEvent:    %lu\t%u\t%p\n", *tid, *id, *addr);
    //   break;
    case ReturnEventLabel:
      read(trace, fevent, fevent_length);
      tid = (uint64_t *)(&fevent[0]);
      id = (uint32_t *)(&fevent[64]);
      addr = (void **)(&fevent[96]);
      printf("ReturnEvent:     %lu\t%u\t%p\n", *tid, *id, *addr);
      break;
    }
  }
}

void readPinTrace(char *trace_file_name) {
  int trace = open(trace_file_name, O_RDONLY);
  assert(trace != -1 && "Failed to open tracing file!\n");

  char event_label;
  const static size_t scevent_length = 65 - 1; // except the label
  const static size_t fevent_length = 65 + size_of_ptr - 1;
  char scevent[scevent_length], fevent[fevent_length];
  uint64_t *tid;
  void **addr;

  while (read(trace, &event_label, sizeof(char))) {
    if (event_label == EndEventLabel) break;
    switch (event_label) {
    case SyscallEventLabel:
      read(trace, scevent, scevent_length);
      tid = (uint64_t *)(&scevent[0]);
      printf("SyscallEvent:   %lu\n", *tid);
      break;
    case CallEventLabel:
      read(trace, fevent, fevent_length);
      tid = (uint64_t *)(&fevent[0]);
      addr = (void **)(&fevent[64]);
      printf("CallEvent:      %lu\t%p\n", *tid, *addr);
      break;
    case ReturnEventLabel:
      read(trace, fevent, fevent_length);
      tid = (uint64_t *)(&fevent[0]);
      addr = (void **)(&fevent[64]);
      printf("ReturnEvent:    %lu\t%p\n", *tid, *addr);
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argv[1][0] == 'p')
    readPinTrace(argv[2]);
  else if (argv[1][0] == 'l')
    readLLVMTrace(argv[2]);
}