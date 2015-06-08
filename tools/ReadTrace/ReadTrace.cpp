#include "SlimmerUtil.h"

#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

namespace boost {
void throw_exception(std::exception const& e) {}
}

void readLLVMTrace(char *trace_file_name) {
  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  for (int64_t cur = 0; cur < trace.size();) {
    cur += GetEvent(false, &data[cur], event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    switch (event_label) {
      case BasicBlockEventLabel:
        printf("BasicBlockEvent: %lu\t%u\n", *tid_ptr, *id_ptr);
        break;
      case MemoryEventLabel:
        printf("MemoryEvent:     %lu\t%u\t%p\t%lu\n", *tid_ptr, *id_ptr, (void*)*addr_ptr, *length_ptr);
        break;
      case ReturnEventLabel:
        printf("ReturnEvent:     %lu\t%u\t%p\n", *tid_ptr, *id_ptr, (void*)*addr_ptr);
         break;
      case ArgumentEventLabel:
        printf("ArgumentEvent:   %lu\t%p\n", *tid_ptr, (void*)*addr_ptr);
         break;
    }
  }
}

void readPinTrace(char *trace_file_name) {
  // int trace = open(trace_file_name, O_RDONLY);
  // assert(trace != -1 && "Failed to open tracing file!\n");

  // char event_label;
  // const static size_t scevent_length = 65 - 1; // except the label
  // const static size_t fevent_length = 65 + size_of_ptr - 1;
  // char scevent[scevent_length], fevent[fevent_length];
  // uint64_t *tid;
  // void **addr;

  // while (read(trace, &event_label, sizeof(char))) {
  //   if (event_label == EndEventLabel) break;
  //   switch (event_label) {
  //   case SyscallEventLabel:
  //     read(trace, scevent, scevent_length);
  //     tid = (uint64_t *)(&scevent[0]);
  //     printf("SyscallEvent:   %lu\n", *tid);
  //     break;
  //   case CallEventLabel:
  //     read(trace, fevent, fevent_length);
  //     tid = (uint64_t *)(&fevent[0]);
  //     addr = (void **)(&fevent[64]);
  //     printf("CallEvent:      %lu\t%p\n", *tid, *addr);
  //     break;
  //   case ReturnEventLabel:
  //     read(trace, fevent, fevent_length);
  //     tid = (uint64_t *)(&fevent[0]);
  //     addr = (void **)(&fevent[64]);
  //     printf("ReturnEvent:    %lu\t%p\n", *tid, *addr);
  //     break;
  //   }
  // }
}

int main(int argc, char *argv[]) {
  if (argv[1][0] == 'p')
    readPinTrace(argv[2]);
  else if (argv[1][0] == 'l')
    readLLVMTrace(argv[2]);
}