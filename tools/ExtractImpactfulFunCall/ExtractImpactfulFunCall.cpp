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
  FILE *output_file = fopen(output_file_name, "w");
  boost::iostreams::mapped_file_source trace(pin_trace_file_name);
  auto data = trace.data();
  char event_label;
  uint64_t *tid_ptr, *fun_ptr;
  
  map<uint64_t, stack<pair<uint64_t, uint32_t> > > fun_stack;
  map<pair<uint64_t, uint64_t>, uint32_t> FunCount;
  set<tuple<uint64_t, uint64_t, int32_t> > ImpactfulFunCall;
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

        uint64_t fun = fun_stack[*tid_ptr].top().first;
        uint32_t cnt = fun_stack[*tid_ptr].top().second;
        printf("The %d-th execution of function %p of thread %lu is impactful\n",
          cnt, (void*)fun, *tid_ptr);
        ImpactfulFunCall.insert(make_tuple(*tid_ptr, fun, cnt));
        break;
    }
  }
  for (auto i: ImpactfulFunCall) {
    fprintf(output_file, "%lu %lu %d\n", get<0>(i), get<1>(i), get<2>(i) - FunCount[I(get<0>(i), get<1>(i))] + 1);
  }
  fclose(output_file);
}

int main(int argc, char *argv[]) {
  if (argc != 2 && argc != 3) {
    printf("Usage: extract-impactful-fun-call pin-trace-file [output-file]\n");
    exit(1);
  }
  if (argc == 2) {
    ExtractImpactfulFunCall(argv[1], "SlimmerImpactFunCall");
  } else {
    ExtractImpactfulFunCall(argv[1], argv[2]);
  }
}