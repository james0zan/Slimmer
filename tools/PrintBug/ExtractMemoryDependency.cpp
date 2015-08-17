#include "SlimmerTools.h"

/// Extracting the memory dependencies.
///
/// \param merged_trace_file_name - the merged trace outputed by the merge-trace
/// tool.
/// \param output_file_name - the path to output file.
///
void ExtractMemoryDependency(vector<SmallestBlock> &block_trace,
                             map<DynamicInst, vector<DynamicInst> > &mem_dep) {
  map<DynamicInst, set<DynamicInst> > _mem_dep;

  SegmentTree<DynamicInst> *last_store = SegmentTree<DynamicInst>::NewTree();
  map<pair<uint64_t, uint32_t>, uint32_t> ins_count;

  for (auto &b : block_trace) {
    if (b.Type == SmallestBlock::MemoryAccessBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst =
          DynamicInst(b.TID, ins_id, ins_count[I(b.TID, ins_id)]++);

      if (b.Addr[0] >= b.Addr[1])
        continue; // Inefficacious write

      if (Ins[ins_id].Type == InstInfo::StoreInst) {
        // Recording a store
        last_store->Set(b.Addr[0], b.Addr[1], dyn_inst);
      } else if (Ins[ins_id].Type == InstInfo::LoadInst) {
        // Obtaining all the last writes
        for (auto j : last_store->Collect(b.Addr[0], b.Addr[1])) {
          if (j.type == COVERED_SEGMENT) {
            _mem_dep[dyn_inst].insert(j.value);
          }
        }

        if (Ins[ins_id].Type == InstInfo::AtomicInst) {
          // Recording a store from atomic operation
          last_store->Set(b.Addr[0], b.Addr[1], dyn_inst);
        }
      }
    } else if (b.Type == SmallestBlock::MemsetBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst =
          DynamicInst(b.TID, ins_id, ins_count[I(b.TID, ins_id)]++);

      if (b.Addr[0] >= b.Addr[1])
        continue; // Inefficacious write

      // Recording a store
      last_store->Set(b.Addr[0], b.Addr[1], dyn_inst);
    } else if (b.Type == SmallestBlock::MemmoveBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst =
          DynamicInst(b.TID, ins_id, ins_count[I(b.TID, ins_id)]++);

      if (b.Addr[0] >= b.Addr[1])
        continue; // Inefficacious write

      // Obtaining all the last writes
      for (auto j : last_store->Collect(b.Addr[2], b.Addr[3])) {
        if (j.type == COVERED_SEGMENT) {
          _mem_dep[dyn_inst].insert(j.value);
        }
      }

      // Recording a store
      last_store->Set(b.Addr[0], b.Addr[1], dyn_inst);
    } else if (b.Type == SmallestBlock::ExternalCallBlock ||
               b.Type == SmallestBlock::ImpactfulCallBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst =
          DynamicInst(b.TID, ins_id, ins_count[I(b.TID, ins_id)]++);
      // All the memory addresses of a group are assumed to be accessed,
      // if one of them is passed as an argument.
      for (size_t i = 1; i < b.Addr.size(); ++i) {
        int group_id = -1;
        if (!Addr2Group->Get(b.Addr[i], group_id))
          continue;
        for (auto i :
             Group2Addr[group_id]->Collect(0, SegmentTree<int>::MAX_RANGE)) {
          if (i.type == COVERED_SEGMENT) {
            for (auto j : last_store->Collect(i.left, i.right)) {
              if (j.type == COVERED_SEGMENT) {
                _mem_dep[dyn_inst].insert(j.value);
              }
            }
            last_store->Set(i.left, i.right, dyn_inst);
          }
        }
      }
    }
  }

  for (auto &i : _mem_dep) {
    DynamicInst tmp_a = i.first;
    tmp_a.Cnt -= (ins_count[I(i.first.TID, i.first.ID)] - 1);
    for (auto &j : i.second) {
      DynamicInst tmp_b = j;
      tmp_b.Cnt -= (ins_count[I(j.TID, j.ID)] - 1);
      mem_dep[tmp_a].push_back(tmp_b);
    }
  }
  delete last_store;
}
