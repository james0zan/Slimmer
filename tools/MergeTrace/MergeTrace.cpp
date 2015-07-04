#include "SlimmerTools.h"

// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

struct StackInfo {
  uint32_t BBID;
  uint32_t CurIndex;
  StackInfo() {}
  StackInfo(uint32_t bb_id, int64_t cur_index)
    : BBID(bb_id), CurIndex(cur_index) {}
};

inline pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id) {
  return make_pair(tid, id);
}

set<tuple<uint64_t, uint64_t, int32_t> > ImpactfulFunCall;
void ExtractImpactfulFunCall(char *pin_trace_file_name) {
  boost::iostreams::mapped_file_source trace(pin_trace_file_name);
  auto data = trace.data();
  char event_label;
  uint64_t *tid_ptr, *fun_ptr;
  
  map<uint64_t, stack<pair<uint64_t, uint32_t> > > fun_stack;
  map<pair<uint64_t, uint64_t>, uint32_t> FunCount;

  bool ended = false;
  char *buffer = (char *)malloc(COMPRESS_BLOCK_SIZE);

  for (size_t _ = 0; !ended && _ < trace.size();) {
    uint64_t length = (*(uint64_t *)(&data[_]));
    _ += sizeof(uint64_t);

    uint64_t decoded = LZ4_decompress_safe((const char*) &data[_], buffer, length, COMPRESS_BLOCK_SIZE);
    for (uint64_t cur = 0; !ended && cur < decoded;) {
      event_label = buffer[cur];
      switch (event_label) {
        case EndEventLabel: ++cur; ended = true; break;
        case CallEventLabel:
          tid_ptr = (uint64_t *)(&buffer[cur + 1]);
          fun_ptr = (uint64_t *)(&buffer[cur + 65]);
          cur += 130;
          // printf("CallEvent %lu %p\n", *tid_ptr, (void*)*fun_ptr);
          fun_stack[*tid_ptr].push(I(*fun_ptr, FunCount[I(*tid_ptr, *fun_ptr)]++));
          break;
        case ReturnEventLabel:
          tid_ptr = (uint64_t *)(&buffer[cur + 1]);
          fun_ptr = (uint64_t *)(&buffer[cur + 65]);
          cur += 130;
          // printf("ReturnEvent %lu %p\n", *tid_ptr, (void*)*fun_ptr);
          assert(fun_stack[*tid_ptr].top().first == (*fun_ptr));
          fun_stack[*tid_ptr].pop();
          break;
        case SyscallEventLabel:
          tid_ptr = (uint64_t *)(&buffer[cur + 1]);
          // printf("SyscallEvent %lu\n", *tid_ptr);
          cur += 66;

          uint64_t fun = fun_stack[*tid_ptr].top().first;
          uint32_t cnt = fun_stack[*tid_ptr].top().second;
          // printf("The %d-th execution of function %p of thread %lu is impactful\n",
          //   cnt, (void*)fun, *tid_ptr);
          ImpactfulFunCall.insert(make_tuple(*tid_ptr, fun, cnt));
          break;
      }
    }
    _ += length + sizeof(uint64_t);
  }
}

void getInsFlow(char *inst_file, char *trace_file_name) {
  LoadInstInfo(inst_file, Ins, BB2Ins);
  
  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  map<pair<uint64_t, uint64_t>, uint32_t> FunCount;

  map<uint64_t, vector<StackInfo> > call_stack;
  map<uint64_t, set<uint64_t> > args;
  map<uint64_t, pair<uint8_t, uint32_t> > is_first;
  vector<SmallestBlock> trace;

  map<uint64_t, stack<int32_t> > this_bb_id, last_bb_id;
  TraceIter iter(trace_file_name);
  while (iter.NextEvent(event_label, tid_ptr, id_ptr, addr_ptr, length_ptr)) {
    if (event_label == ArgumentEventLabel) {
      args[*tid_ptr].insert(*addr_ptr);
    }
    
    if (event_label != BasicBlockEventLabel && event_label != MemoryEventLabel && event_label != ReturnEventLabel)  continue;

    if (event_label == BasicBlockEventLabel) {
      if (call_stack[*tid_ptr].empty()) {
        is_first[*tid_ptr] = make_pair((uint8_t)2, (uint32_t)0);
        this_bb_id[*tid_ptr].push(*id_ptr);
        last_bb_id[*tid_ptr].push(-1);
      }
      while (!call_stack[*tid_ptr].empty()) {
        StackInfo info = call_stack[*tid_ptr].back();
        if (info.CurIndex >= BB2Ins[info.BBID].size()) {
          call_stack[*tid_ptr].pop_back();
          is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);

          last_bb_id[*tid_ptr].top() = this_bb_id[*tid_ptr].top();
          this_bb_id[*tid_ptr].top() = (*id_ptr);
        } else {
          assert(Ins[BB2Ins[info.BBID][info.CurIndex - 1]].Type == InstInfo::CallInst);
          is_first[*tid_ptr] = make_pair((uint8_t)1, BB2Ins[info.BBID][info.CurIndex - 1]);
          break;
        }
      }
      call_stack[*tid_ptr].push_back(StackInfo(*id_ptr, 0));
    } else if (event_label == MemoryEventLabel) {
      StackInfo& info = call_stack[*tid_ptr].back();
      uint32_t ins_id = BB2Ins[info.BBID][info.CurIndex++];
      assert((*id_ptr) == ins_id);

      SmallestBlock b(SmallestBlock::MemoryAccessBlock, *tid_ptr, info.BBID, info.CurIndex - 1, info.CurIndex, is_first[*tid_ptr], last_bb_id[*tid_ptr].top());
      b.Addr.push_back(*addr_ptr);  b.Addr.push_back(*addr_ptr + *length_ptr);
      is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
      trace.push_back(b);
    } else if (event_label == ReturnEventLabel) {
      StackInfo& info = call_stack[*tid_ptr].back();
      uint32_t ins_id = BB2Ins[info.BBID][info.CurIndex++];
      assert((*id_ptr) == ins_id);
      
      SmallestBlock b(SmallestBlock::ExternalCallBlock, *tid_ptr, info.BBID, info.CurIndex - 1, info.CurIndex, is_first[*tid_ptr], last_bb_id[*tid_ptr].top());
      b.Addr.push_back(*addr_ptr);

      for (auto i: args[*tid_ptr]) b.Addr.push_back(i);
      args[*tid_ptr].clear();
      
      is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
      
      if (ImpactfulFunCall.count(make_tuple(*tid_ptr, *addr_ptr, FunCount[I(*tid_ptr, *addr_ptr)]))) {
        b.Type = SmallestBlock::ImpactfulCallBlock;
      }
      FunCount[I(*tid_ptr, *addr_ptr)]++;

      trace.push_back(b);
    }

    while (!call_stack[*tid_ptr].empty()) {
      StackInfo& info = call_stack[*tid_ptr].back();

      uint32_t start_index = info.CurIndex, end_index;
      for (end_index = start_index; end_index < BB2Ins[info.BBID].size(); ++end_index) {
        uint32_t ins_id = BB2Ins[info.BBID][end_index];
        if (
          Ins[ins_id].Type == InstInfo::CallInst ||
          Ins[ins_id].Type == InstInfo::ExternalCallInst ||
          Ins[ins_id].Type == InstInfo::LoadInst ||
          Ins[ins_id].Type == InstInfo::StoreInst) {
          break;
        }
      }
      if (end_index < BB2Ins[info.BBID].size() && Ins[BB2Ins[info.BBID][end_index]].Type == InstInfo::CallInst) ++end_index;
      info.CurIndex = end_index;

      bool last_bb = false;
      if (info.CurIndex == BB2Ins[info.BBID].size()) {
        uint32_t last_ins_id = BB2Ins[info.BBID].back();
        if (Ins[last_ins_id].Type == InstInfo::ReturnInst) {
          last_bb = true;
        }
      } 

      SmallestBlock b;
      if (end_index > start_index) {
        SmallestBlock b(SmallestBlock::NormalBlock, *tid_ptr, info.BBID, start_index, end_index, is_first[*tid_ptr], last_bb_id[*tid_ptr].top());
        if (last_bb) {
          call_stack[*tid_ptr].pop_back();
          this_bb_id[*tid_ptr].pop();
          last_bb_id[*tid_ptr].pop();

          if (call_stack[*tid_ptr].empty()) {
            b.IsLast = 2;
          } else {
            b.IsLast = 1;
            StackInfo last_info = call_stack[*tid_ptr].back();
            assert(last_info.CurIndex < BB2Ins[last_info.BBID].size());
            assert(Ins[BB2Ins[last_info.BBID][last_info.CurIndex - 1]].Type == InstInfo::CallInst);
            b.Caller = BB2Ins[last_info.BBID][last_info.CurIndex - 1];
          }
        }
        is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
        trace.push_back(b);
      }

      if (!last_bb) break;
    }
  }

  for (auto i: trace) {
    i.Print(Ins, BB2Ins);
  }

  FILE* dump = fopen("/scratch1/zhangmx/SlimmerMergedTrace", "wb");
  for (auto i: trace) {
    i.Dump(dump);
  }
  fclose(dump);

  puts("=========");

  boost::iostreams::mapped_file_source load("/scratch1/zhangmx/SlimmerMergedTrace");
  auto data = load.data();
  for (size_t cur = 0; cur < load.size();) {
    SmallestBlock b;
    cur += b.ReadFrom(data);
    b.Print(Ins, BB2Ins);
  }

  puts("=========");

  boost::iostreams::mapped_file_source load2("/scratch1/zhangmx/SlimmerMergedTrace");
  auto data2 = load2.data();
  for (int64_t cur = load2.size(); cur > 0;) {
    SmallestBlock b;
    b.ReadBack(data2, cur);
    b.Print(Ins, BB2Ins);
  }
}


int main(int argc, char *argv[]) {
  ExtractImpactfulFunCall(argv[1]);
  getInsFlow(argv[2], argv[3]);
}