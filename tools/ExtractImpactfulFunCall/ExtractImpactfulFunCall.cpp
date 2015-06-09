#include "SlimmerUtil.h"

#include "stdio.h"

#include <stack>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

namespace boost {
void throw_exception(std::exception const& e) {}
}

inline pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id) {
  return make_pair(tid, id);
}

void ExtractImpactfulFunCall(char *pin_trace_file_name, char *output_file_name) {
  boost::iostreams::mapped_file_source trace(pin_trace_file_name);
  auto data = trace.data();
  char event_label;
  uint64_t *tid_ptr, *fun_ptr;
  
  map<uint64_t, stack<pair<uint64_t, uint32_t> > > fun_stack;
  map<pair<uint64_t, uint32_t>, uint32_t> FunCount;
  for (size_t cur = 0; cur < trace.size();) {
    event_label = data[cur];
    switch (event_label) {
      case EndEventLabel: ++cur; break;
      case CallEventLabel:
        tid_ptr = (uint64_t *)(&data[cur + 1]);
        fun_ptr = (uint64_t *)(&data[cur + 65]);
        cur += 130;
        // printf("CallEvent %lu %p\n", *tid_ptr, (void*)*fun_ptr);
        fun_stack[*tid_ptr].push(I(*fun_ptr, FunCount[I(*tid_ptr, *fun_ptr)]++));
        break;
      case ReturnEventLabel:
        tid_ptr = (uint64_t *)(&data[cur + 1]);
        fun_ptr = (uint64_t *)(&data[cur + 65]);
        cur += 130;
        // printf("ReturnEvent %lu %p\n", *tid_ptr, (void*)*fun_ptr);
        assert(fun_stack[*tid_ptr].top().first == (*fun_ptr));
        fun_stack[*tid_ptr].pop();
        break;
      case SyscallEventLabel:
        tid_ptr = (uint64_t *)(&data[cur + 1]);
        // printf("SyscallEvent %lu\n", *tid_ptr);
        cur += 66;
        printf("The %d-th execution of function %p of thread %d is impactful\n",
          fun_stack[*tid_ptr].top().second, (void*)fun_stack[*tid_ptr].top().first, *tid_ptr);
        break;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2 && argc != 3) {
    printf("Usage: extract-impactful-fun-call pin-trace-file [output-file]\n");
    exit(1);
  }
  if (argc == 3) {
    ExtractImpactfulFunCall(argv[1], "SlimmerImpactFunCall");
  } else {
    ExtractImpactfulFunCall(argv[1], argv[2]);
  }
}