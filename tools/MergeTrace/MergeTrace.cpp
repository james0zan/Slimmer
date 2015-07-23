#include "SlimmerTools.h"

/// Extract the function calls that impact the outside enviroment.
///
/// \param pin_trace_file_name - path to trace file generated by PIN tool.
/// \param impactful_fun_call - recording the function calls that impact the outside enviroment. 
///
void ExtractImpactfulFunCall(
  char *pin_trace_file_name, 
  set<tuple<uint64_t, uint64_t, int32_t> >& impactful_fun_call) {

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

          if (fun_stack[*tid_ptr].size() == 0) break;
          uint64_t fun = fun_stack[*tid_ptr].top().first;
          uint32_t cnt = fun_stack[*tid_ptr].top().second;
          // printf("The %d-th execution of function %p of thread %lu is impactful\n",
          //   cnt, (void*)fun, *tid_ptr);
          impactful_fun_call.insert(make_tuple(*tid_ptr, fun, cnt));
          break;
      }
    }
    _ += length + sizeof(uint64_t);
  }
}

// Recording the basic block stack.
// The last executed instruction is
// the CurIndex-th instruction of basic block BBID.
struct StackInfo {
  uint32_t BBID;
  uint32_t CurIndex; 
  StackInfo() {}
  StackInfo(uint32_t bb_id, int64_t cur_index)
    : BBID(bb_id), CurIndex(cur_index) {}
};

/// This function takes the trace generated by LLVM and PIN
/// and generated a list of SmallestBlocks that contain
/// all the information needed for analyzing.
///
/// \param inst_file - path to Inst file generated by SlimmerTrace pass.
/// \param trace_file_name - path to trace file generated by the instrumented application.
/// \param output_file_name - path to output file.
/// \param impactful_fun_call - recoded the function calls that impact the outside enviroment. 
/// 
void MergeTrace(
  char *inst_file, char *trace_file_name, 
  char *output_file_name, 
  set<tuple<uint64_t, uint64_t, int32_t> >& impactful_fun_call) {

  // Map an instruction ID to its instruction infomation
  vector<InstInfo> Ins;
  // Map a basic block ID to all the instructions that belong to it
  vector<vector<uint32_t> > BB2Ins;
  LoadInstInfo(inst_file, Ins, BB2Ins);
  
  FILE* dump = fopen(output_file_name, "wb");
  assert(dump && "Cannot open the output file!");

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr, *addr2_ptr;
  const uint32_t *id_ptr;
  
  // FunCount[<Thread ID tid, Function Address fun>] 
  //    = how many times that thread tid has executed function fun.
  map<pair<uint64_t, uint64_t>, uint32_t> FunCount;

  map<uint64_t, vector<StackInfo> > call_stack;
  map<uint64_t, set<uint64_t> > args;
  map<uint64_t, pair<uint8_t, uint32_t> > is_first;

  map<uint64_t, stack<int32_t> > this_bb_id, last_bb_id;
  TraceIter iter(trace_file_name);
  while (iter.NextEvent(event_label, tid_ptr, id_ptr, addr_ptr, length_ptr, addr2_ptr)) {
    // switch (event_label) {
    //   case BasicBlockEventLabel:
    //     printf("BasicBlockEvent:  %lu\t%u\n", *tid_ptr, *id_ptr);
    //     break;
    //   case MemoryEventLabel:
    //     printf("MemoryEvent:      %lu\t%u\t%p\t%lu\n", *tid_ptr, *id_ptr, (void*)*addr_ptr, *length_ptr);
    //     break;
    //   case ReturnEventLabel:
    //     printf("ReturnEvent:      %lu\t%u\t%p\n", *tid_ptr, *id_ptr, (void*)*addr_ptr);
    //     break;
    //   case ArgumentEventLabel:
    //     printf("ArgumentEvent:    %lu\t%p\n", *tid_ptr, (void*)*addr_ptr);
    //     break;
    //   case  MemsetEventLabel:
    //     printf("MemsetEvent:      %lu\t%u\t%p\t%lu\n", *tid_ptr, *id_ptr, (void*)*addr_ptr, *length_ptr);
    //     break;
    //   case  MemmoveEventLabel:
    //     printf("MemmoveEvent:     %lu\t%u\t%p\t%p\t%lu\n", *tid_ptr, *id_ptr, (void*)*addr_ptr, (void*)*addr2_ptr, *length_ptr);
    //     break;
    // }

    if (event_label == ArgumentEventLabel) {
      args[*tid_ptr].insert(*addr_ptr);
    }
    
    if (event_label != BasicBlockEventLabel 
        && event_label != MemoryEventLabel 
        && event_label != ReturnEventLabel
        && event_label != MemsetEventLabel
        && event_label != MemmoveEventLabel)  continue;

    if (event_label == BasicBlockEventLabel) {
      if (call_stack[*tid_ptr].empty()) {
        // This is the first basic block of a thread.
        is_first[*tid_ptr] = make_pair((uint8_t)2, (uint32_t)0);
        this_bb_id[*tid_ptr].push(*id_ptr);
        last_bb_id[*tid_ptr].push(-1);
      } else {
        StackInfo info = call_stack[*tid_ptr].back();
        if (info.CurIndex >= BB2Ins[info.BBID].size()) {
          // There is already a basic block executed by this function.
          call_stack[*tid_ptr].pop_back();
          is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);

          last_bb_id[*tid_ptr].top() = this_bb_id[*tid_ptr].top();
          this_bb_id[*tid_ptr].top() = (*id_ptr);
        } else {
          // This is the starting basic block of a called function.
          assert(Ins[BB2Ins[info.BBID][info.CurIndex - 1]].Type == InstInfo::CallInst);
          is_first[*tid_ptr] = make_pair((uint8_t)1, BB2Ins[info.BBID][info.CurIndex - 1]);
          this_bb_id[*tid_ptr].push(*id_ptr);
          last_bb_id[*tid_ptr].push(-1);
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
      // b.Print(Ins, BB2Ins);
      b.Dump(dump);
    } else if (event_label == ReturnEventLabel) {
      StackInfo& info = call_stack[*tid_ptr].back();
      uint32_t ins_id = BB2Ins[info.BBID][info.CurIndex++];
      assert((*id_ptr) == ins_id);
      
      SmallestBlock b(SmallestBlock::ExternalCallBlock, *tid_ptr, info.BBID, info.CurIndex - 1, info.CurIndex, is_first[*tid_ptr], last_bb_id[*tid_ptr].top());
      b.Addr.push_back(*addr_ptr);

      for (auto i: args[*tid_ptr]) b.Addr.push_back(i);
      args[*tid_ptr].clear();
      
      is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
      
      if (impactful_fun_call.count(make_tuple(*tid_ptr, *addr_ptr, FunCount[I(*tid_ptr, *addr_ptr)]))) {
        b.Type = SmallestBlock::ImpactfulCallBlock;
      }
      FunCount[I(*tid_ptr, *addr_ptr)]++;
      // b.Print(Ins, BB2Ins);
      b.Dump(dump);
    } else if (event_label == MemsetEventLabel) {
      StackInfo& info = call_stack[*tid_ptr].back();
      uint32_t ins_id = BB2Ins[info.BBID][info.CurIndex++];
      assert((*id_ptr) == ins_id);

      SmallestBlock b(SmallestBlock::MemsetBlock, *tid_ptr, info.BBID, info.CurIndex - 1, info.CurIndex, is_first[*tid_ptr], last_bb_id[*tid_ptr].top());
      b.Addr.push_back(*addr_ptr);  b.Addr.push_back(*addr_ptr + *length_ptr);
      is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
      // b.Print(Ins, BB2Ins);
      b.Dump(dump);
    } else if (event_label == MemmoveEventLabel) {
      StackInfo& info = call_stack[*tid_ptr].back();
      uint32_t ins_id = BB2Ins[info.BBID][info.CurIndex++];
      assert((*id_ptr) == ins_id);

      SmallestBlock b(SmallestBlock::MemmoveBlock, *tid_ptr, info.BBID, info.CurIndex - 1, info.CurIndex, is_first[*tid_ptr], last_bb_id[*tid_ptr].top());
      b.Addr.push_back(*addr_ptr);  b.Addr.push_back(*addr_ptr + *length_ptr);
      b.Addr.push_back(*addr2_ptr);  b.Addr.push_back(*addr2_ptr + *length_ptr);
      is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
      // b.Print(Ins, BB2Ins);
      b.Dump(dump);
    } 

    while (!call_stack[*tid_ptr].empty()) {
      StackInfo& info = call_stack[*tid_ptr].back();
      uint32_t start_index = info.CurIndex, end_index;

      // Get a continuous part of a basic block
      // that will always be executed continuously.
      for (end_index = start_index; end_index < BB2Ins[info.BBID].size(); ++end_index) {
        uint32_t ins_id = BB2Ins[info.BBID][end_index];
        if (
          Ins[ins_id].Type == InstInfo::CallInst ||
          Ins[ins_id].Type == InstInfo::ExternalCallInst ||
          Ins[ins_id].Type == InstInfo::LoadInst ||
          Ins[ins_id].Type == InstInfo::StoreInst ||
          Ins[ins_id].Type == InstInfo::AtomicInst) {
          break;
        }
      }
      if (end_index < BB2Ins[info.BBID].size() 
        && Ins[BB2Ins[info.BBID][end_index]].Type == InstInfo::CallInst
        && Ins[BB2Ins[info.BBID][end_index]].Fun.substr(0, 5) != "llvm.") ++end_index;
      info.CurIndex = end_index;

      bool last_bb = false;
      if (info.CurIndex == BB2Ins[info.BBID].size()) {
        uint32_t last_ins_id = BB2Ins[info.BBID].back();
        if (Ins[last_ins_id].Type == InstInfo::ReturnInst) {
          // It is the last SmallestBlock of a function.
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
            b.IsLast = 2; // The last SmallestBlock of a thread.
          } else {
            b.IsLast = 1;
            StackInfo last_info = call_stack[*tid_ptr].back();
            assert(last_info.CurIndex < BB2Ins[last_info.BBID].size());
            assert(Ins[BB2Ins[last_info.BBID][last_info.CurIndex - 1]].Type == InstInfo::CallInst);
            b.Caller = BB2Ins[last_info.BBID][last_info.CurIndex - 1];
          }
        }
        is_first[*tid_ptr] = make_pair((uint8_t)0, (uint32_t)0);
        // b.Print(Ins, BB2Ins);
        b.Dump(dump);
      }
      if (!last_bb) break;
    }
  }

  for (auto& i: call_stack) {
    auto tid = i.first;
    while (!i.second.empty()) {
      StackInfo& info = call_stack[tid].back();
      SmallestBlock b(SmallestBlock::NormalBlock, tid, info.BBID, info.CurIndex, info.CurIndex, make_pair((uint8_t)0, (uint32_t)0), last_bb_id[*tid_ptr].top());
      call_stack[tid].pop_back();
      this_bb_id[tid].pop();
      last_bb_id[tid].pop();

      if (call_stack[tid].empty()) {
        b.IsLast = 2; // The last SmallestBlock of a thread.
      } else {
        b.IsLast = 1;
        StackInfo last_info = call_stack[tid].back();
        assert(last_info.CurIndex < BB2Ins[last_info.BBID].size());
        assert(Ins[BB2Ins[last_info.BBID][last_info.CurIndex - 1]].Type == InstInfo::CallInst);
        b.Caller = BB2Ins[last_info.BBID][last_info.CurIndex - 1];
      }
      // b.Print(Ins, BB2Ins);
      b.Dump(dump);
    }
  }
  fclose(dump);
}


int main(int argc, char *argv[]) {
  if (argc != 4 && argc != 5) {
    printf("Usage: merge-trace pin-trace-file inst-file trace-file [output-file]\n");
    exit(1);
  }
  // A set of function calls that impact the outside enviroment.
  // For each tuple, it is consisting of (thread ID TID, fun address F, execution count C),
  // which represents the C execution of function F from thread TID.
  set<tuple<uint64_t, uint64_t, int32_t> > ImpactfulFunCall;
  ExtractImpactfulFunCall(argv[1], ImpactfulFunCall);

  if (argc == 4) {
    MergeTrace(argv[2], argv[3], "/scratch1/zhangmx/SlimmerMergedTrace", ImpactfulFunCall);
  } else {
    MergeTrace(argv[2], argv[3], argv[4], ImpactfulFunCall);
  }
}