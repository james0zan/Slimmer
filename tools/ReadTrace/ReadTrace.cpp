#include "SlimmerUtil.h"

#include <stack>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

namespace boost {
void throw_exception(std::exception const& e) {}
}

struct TraceIter {
  boost::iostreams::mapped_file_source trace;
  const char* data;
  char *decoded;
  size_t data_iter, decoded_iter, decoded_size;
  bool ended;

  TraceIter(char *trace_file_name) : trace(trace_file_name) {
    ended = false;
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = decoded_iter = decoded_size = 0;
  }

  bool NextEvent(
    char& event_label, const uint64_t*& tid_ptr, 
    const uint32_t*& id_ptr, const uint64_t*& addr_ptr, 
    const uint64_t*& length_ptr) {
    
    if (decoded_iter >= decoded_size) {
      if (ended || data_iter >= trace.size()) return false; // Trace is ended
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter += sizeof(uint64_t);

      decoded_size = LZ4_decompress_safe ((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      assert(decoded_size > 0);
      decoded_iter = 0;

      data_iter += length + sizeof(uint64_t);
    }
    decoded_iter += GetEvent(false, decoded + decoded_iter, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    if (event_label == EndEventLabel) ended = true;
    return true;
  }
};

struct TraceBackwardIter {
  boost::iostreams::mapped_file_source trace;
  const char* data;
  char *decoded;
  int64_t data_iter, decoded_iter, decoded_size;

  TraceBackwardIter(char *trace_file_name) : trace(trace_file_name) {
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = trace.size();
    decoded_iter = -1;
    decoded_size = 0;
  }

  bool FormerEvent(
    char& event_label, const uint64_t*& tid_ptr, 
    const uint32_t*& id_ptr, const uint64_t*& addr_ptr, 
    const uint64_t*& length_ptr) {
    
    if (decoded_iter <= 0) {
      if (data_iter <= 0) return false; // Trace is ended
      data_iter -= sizeof(uint64_t);
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter -= length;

      decoded_size = LZ4_decompress_safe((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      decoded_iter = decoded_size - 1;

      data_iter -= sizeof(uint64_t);
    }

    decoded_iter -= GetEvent(true, decoded + decoded_iter, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    return true;
  }
};

void readLLVMTrace(char *trace_file_name) {
  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  TraceIter iter(trace_file_name);
  while (iter.NextEvent(event_label, tid_ptr, id_ptr, addr_ptr, length_ptr)) {
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

  puts("++++++");
  
  TraceBackwardIter biter(trace_file_name);
  while (biter.FormerEvent(event_label, tid_ptr, id_ptr, addr_ptr, length_ptr)) {
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

// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

struct StackInfo {
  uint32_t BBID;
  int64_t CurIndex;
  StackInfo() {}
  StackInfo(uint32_t bb_id, int64_t cur_index)
    : BBID(bb_id), CurIndex(cur_index) {}
};

void getInsFlow(char *inst_file, char *trace_file_name) {
  LoadInstInfo(inst_file, Ins, BB2Ins);
  
  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  map<uint64_t, stack<StackInfo> > call_stack;

  TraceIter iter(trace_file_name);
  while (iter.NextEvent(event_label, tid_ptr, id_ptr, addr_ptr, length_ptr)) {
    if (event_label != BasicBlockEventLabel && event_label != MemoryEventLabel && event_label != ReturnEventLabel)  continue;
    if (event_label == BasicBlockEventLabel) {
      while (!call_stack[*tid_ptr].empty()) {
        StackInfo info = call_stack[*tid_ptr].top();
        if (info.CurIndex >= BB2Ins[info.BBID].size()) {
          call_stack[*tid_ptr].pop();
        } else {
          assert(Ins[BB2Ins[info.BBID][info.CurIndex]].Type == InstInfo::CallInst);
          break;
        }
      }
      call_stack[*tid_ptr].push(StackInfo(*id_ptr, -1));
    } else if (event_label == MemoryEventLabel || event_label == ReturnEventLabel) {
      StackInfo& info = call_stack[*tid_ptr].top();
      // Pass the memory/return event
      assert((*id_ptr) == BB2Ins[info.BBID][info.CurIndex]);
    }

    while (!call_stack[*tid_ptr].empty()) {
      StackInfo& info = call_stack[*tid_ptr].top();
      for (++info.CurIndex; info.CurIndex < BB2Ins[info.BBID].size(); ++info.CurIndex) {
        uint32_t ins_id = BB2Ins[info.BBID][info.CurIndex];
        printf("Thread %lu\tBB %u\tInst %d: \t%s\n", *tid_ptr, info.BBID, ins_id, Ins[ins_id].Code.c_str());
        if (
          Ins[ins_id].Type == InstInfo::CallInst ||
          Ins[ins_id].Type == InstInfo::LoadInst ||
          Ins[ins_id].Type == InstInfo::StoreInst) {
          break;
        }
      }
      
      bool last_bb = false;
      if (info.CurIndex == BB2Ins[info.BBID].size()) {
        uint32_t last_ins_id = BB2Ins[info.BBID].back();
        size_t i = 0; for (; iswspace(Ins[last_ins_id].Code[i]); ++i);
        if (Ins[last_ins_id].Code.substr(i, 4) == "ret ") {
          
          last_bb = true;
        }
      } 
      if (last_bb) { call_stack[*tid_ptr].pop();}
      else break;
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
  else if (argv[1][0] == 'b')
    getInsFlow(argv[2], argv[3]);
}