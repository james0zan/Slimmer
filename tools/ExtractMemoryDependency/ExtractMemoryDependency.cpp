#include "SlimmerTools.h"
#include "SegmentTree.hpp"

// A segment tree that maps a memory address to its group
SegmentTree<int> *Addr2Group;
// For each group, we use a segment tree to record all the memory addresses that belong to it
map<uint32_t, SegmentTree<int> *> Group2Addr;
// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

/// Collect all the groups within a specific range of memory.
///
/// \param l - the starting address of the memory.
/// \param r - the lasting address + 1 (not included) of memory.
/// \return - a list of segments that are belonged to different groups.
///
vector<Segment<int> > Collect(uint64_t l, uint64_t r) {
  auto segments = Addr2Group->Collect(l, r);
  segments[0].left = l;
  segments[segments.size() - 1].right = r;

  return segments;
}

/// Merging a list of segment trees into one segment tree.
///
/// \param trees - a list of segment trees that should be merged.
/// \param new_group - the merged group ID.
/// \return - the merged segment tree.
///
SegmentTree<int>* Merging(vector<SegmentTree<int> *> trees, int new_group) {
  vector<SegmentTree<int> *> l_childs, r_childs;
  for (auto i: trees) {
    if (i->type == COVERED_SEGMENT) {
      Addr2Group->Set(i->left, i->right, new_group);
      return new SegmentTree<int>(COVERED_SEGMENT, 1, i->left, i->right);
    } else if (i->type == PARTIAL_SEGMENT) {
      l_childs.push_back(i->l_child);
      r_childs.push_back(i->r_child);
    }
  }
  if (l_childs.size() == 0) {
    return new SegmentTree<int>(EMPTY_SEGMENT, 0, trees[0]->left, trees[0]->right);
  }
  return new SegmentTree<int>(PARTIAL_SEGMENT, -1, trees[0]->left, trees[0]->right,
    Merging(l_childs, new_group),
    Merging(r_childs, new_group));
}

/// Merging a list of groups into one group.
///
/// \param groups - a list of groups that should be merged.
/// \return - the merged group ID.
///
int MaxGroupID;
map<pair<uint64_t, uint32_t>, int> Ins2Group;
map<int, set<pair<uint64_t, uint32_t> > > Group2Ins;
int Merging(set<int> groups) {
  int new_group;
  if (groups.size() == 0) {
    new_group = (++MaxGroupID);
    Group2Addr[new_group] = SegmentTree<int>::NewTree();
    return new_group;
  }
  
  new_group = *groups.begin();
  if (groups.size() == 1) {
    return new_group;
  }

  // Merging two or more groups
  vector<SegmentTree<int> *> trees;
  for (auto i: groups) {
    if (Group2Addr.count(i))
      trees.push_back(Group2Addr[i]);
  }
  Group2Addr[new_group] = Merging(trees, new_group);
  
  for (auto i: groups) {
    if (i == new_group) continue;

    auto tree = Group2Addr[i];
    if (tree) delete tree;
    Group2Addr.erase(i);

    for (auto ins: Group2Ins[i]) Ins2Group[ins] = new_group;
    Group2Ins[new_group].insert(Group2Ins[i].begin(), Group2Ins[i].end());
    Group2Ins.erase(i);
  }

  return new_group;
}

/// Extracting the memory dependencies.
///
/// \param merged_trace_file_name - the merged trace outputed by the merge-trace tool.
/// \param output_file_name - the path to output file.
///
void ExtractMemoryDependency(char *merged_trace_file_name, char *output_file_name) {
  FILE *output_file = fopen(output_file_name, "w");
  boost::iostreams::mapped_file_source trace(merged_trace_file_name);
  auto data = trace.data();

  SegmentTree<DynamicInst> *Addr2LastStore= SegmentTree<DynamicInst>::NewTree();
  map<pair<uint64_t, uint32_t>, uint32_t > InstCount;
  map<uint64_t, set<uint32_t> > ArgGroup;

  for (size_t cur = 0; cur < trace.size();) {
    SmallestBlock b; cur += b.ReadFrom(data);

    if (b.Type == SmallestBlock::MemoryAccessBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst = DynamicInst(b.TID, ins_id, InstCount[I(b.TID, ins_id)]++);

      if (b.Addr[0] >= b.Addr[1]) continue; // Inefficacious write

      if (Ins[ins_id].Type == InstInfo::StoreInst) {
        // Recording a store
        Addr2LastStore->Set(b.Addr[0], b.Addr[1], dyn_inst);
      } else {
        printf("The %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu is depended on:\n",
          dyn_inst.Cnt, dyn_inst.ID, Ins[dyn_inst.ID].Code.c_str(), dyn_inst.TID);

        // Obtaining all the last writes
        for (auto j: Addr2LastStore->Collect(b.Addr[0], b.Addr[1])) {
          if (j.type == COVERED_SEGMENT) {
            printf("\t* the %d-th execution of\n\t  instruction %d, %s\n\t  from thread %lu\n",
              j.value.Cnt, j.value.ID, Ins[j.value.ID].Code.c_str(), j.value.TID);
            fprintf(output_file, "%lu %d %d %lu %d %d\n",
              dyn_inst.TID, dyn_inst.ID, dyn_inst.Cnt,
              j.value.TID, j.value.ID, j.value.Cnt);
          }
        }
      }
    } else if (b.Type == SmallestBlock::MemsetBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst = DynamicInst(b.TID, ins_id, InstCount[I(b.TID, ins_id)]++);

      if (b.Addr[0] >= b.Addr[1]) continue; // Inefficacious write

      // Recording a store
      Addr2LastStore->Set(b.Addr[0], b.Addr[1], dyn_inst);
    } else if (b.Type == SmallestBlock::MemmoveBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst = DynamicInst(b.TID, ins_id, InstCount[I(b.TID, ins_id)]++);

      if (b.Addr[0] >= b.Addr[1]) continue; // Inefficacious write

      printf("The %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu is depended on:\n",
        dyn_inst.Cnt, dyn_inst.ID, Ins[dyn_inst.ID].Code.c_str(), dyn_inst.TID);

      // Obtaining all the last writes
      for (auto j: Addr2LastStore->Collect(b.Addr[2], b.Addr[3])) {
        if (j.type == COVERED_SEGMENT) {
          printf("\t* the %d-th execution of\n\t  instruction %d, %s\n\t  from thread %lu\n",
            j.value.Cnt, j.value.ID, Ins[j.value.ID].Code.c_str(), j.value.TID);
          fprintf(output_file, "%lu %d %d %lu %d %d\n",
            dyn_inst.TID, dyn_inst.ID, dyn_inst.Cnt,
            j.value.TID, j.value.ID, j.value.Cnt);
        }
      }

      // Recording a store
      Addr2LastStore->Set(b.Addr[0], b.Addr[1], dyn_inst);
    } else if (b.Type == SmallestBlock::ExternalCallBlock || b.Type == SmallestBlock::ImpactfulCallBlock) {
      uint32_t ins_id = BB2Ins[b.BBID][b.Start];
      DynamicInst dyn_inst = DynamicInst(b.TID, ins_id, InstCount[I(b.TID, ins_id)]++);
      printf("The %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu is depended on:\n",
        dyn_inst.Cnt, dyn_inst.ID, Ins[dyn_inst.ID].Code.c_str(), dyn_inst.TID);

      // All the memory addresses of a group are assumed to be accessed,
      // if one of them is passed as an argument.
      for (size_t i = 1; i < b.Addr.size(); ++i) {
        int group_id; 
        if (!Addr2Group->Get(b.Addr[i], group_id)) continue;
        for (auto i: Group2Addr[group_id]->Collect(0, SegmentTree<int>::MAX_RANGE)) {
          if (i.type == COVERED_SEGMENT) {
            for (auto j: Addr2LastStore->Collect(i.left, i.right)) {
              if (j.type == COVERED_SEGMENT) {
                printf("\t* the %d-th execution of\n\t  instruction %d, %s\n\t  from thread %lu\n",
                  j.value.Cnt, j.value.ID, Ins[j.value.ID].Code.c_str(), j.value.TID);
                fprintf(output_file, "%lu %d %d %lu %d %d\n",
                  dyn_inst.TID, dyn_inst.ID, dyn_inst.Cnt,
                  j.value.TID, j.value.ID, j.value.Cnt);
              }
            }
            Addr2LastStore->Set(i.left, i.right, dyn_inst);
          }
        }
      }
    }
  }

  fprintf(output_file, "0 -1 -1 0 -1 -1\n"); // Line of demarcation
  for (auto i: InstCount) {
    // printf("Thread %lu has executed instruction %d %d-th\n", i.first.first, i.first.second, i.second);
    fprintf(output_file, "%lu %d %d\n", i.first.first, i.first.second, i.second);
  }
  delete Addr2Group;
  delete Addr2LastStore;
  for (auto& i: Group2Addr) {
    delete i.second;
  }
  Group2Addr.clear();
  fclose(output_file);
}


/// Merging all the addresses used in an instruction into one group.
///
/// \param shoud_merge - a set of groups that should be merged.
/// \param ins - a pair of {Thread ID, Instruction ID}.
/// \param b - the current SmallestBlock object that contains ins.
/// \param labeled_args - a map that maps the thread ID to arguments that have a group label attached.
/// \return - the group ID of merged group.
///
int MergeInst(
  set<int>& shoud_merge, pair<uint64_t, uint32_t> ins, 
  SmallestBlock& b, map<uint64_t, set<uint32_t> >& labeled_args) {

  auto ins_id = ins.second;

  // Merging the result variable
  if (Ins2Group.count(ins)) {
    shoud_merge.insert(Ins2Group[ins]);
  }
  // Merging all the dependencies
  for (auto dep: Ins[ins_id].SSADependencies) {
    if ((dep.first == InstInfo::Inst && Ins[dep.second].IsPointer) || dep.first == InstInfo::PointerArg) {
      auto dependent_ins = I(b.TID, dep.second);
      if (dep.first == InstInfo::PointerArg) dependent_ins.second += Ins.size();

      if (Ins2Group.count(dependent_ins)) {
        shoud_merge.insert(Ins2Group[dependent_ins]);
      }
    }
  }

  int new_group = Merging(shoud_merge);

  // Clear group information of the result variable
  if (Ins2Group.count(ins)) {
    Group2Ins[Ins2Group[ins]].erase(ins);
    Ins2Group.erase(ins);
  }
  // Label the dependencies to new groups
  for (auto dep: Ins[ins_id].SSADependencies) {
    if ((dep.first == InstInfo::Inst && Ins[dep.second].IsPointer) || dep.first == InstInfo::PointerArg) {
      auto dependent_ins = I(b.TID, dep.second);
      if (dep.first == InstInfo::PointerArg) {
        dependent_ins.second += Ins.size();
        labeled_args[dependent_ins.first].insert(dependent_ins.second);
      }
      Ins2Group[dependent_ins] = new_group;
      Group2Ins[new_group].insert(dependent_ins);
    }
  }

  // If it is the first Smallest of a function
  if (b.IsFirst == 1 || b.IsFirst == 2) {
    auto labeled_arg = labeled_args[b.TID];
    labeled_args[b.TID].clear();

    for (auto i: labeled_arg) {
      auto dependent_arg = I(b.TID, i);
      if (Ins2Group.count(dependent_arg)) {
        uint32_t arg_group = Ins2Group[dependent_arg];
        Group2Ins[Ins2Group[dependent_arg]].erase(dependent_arg);
        Ins2Group.erase(dependent_arg);

        // Map the argument with the corresponding value
        if (b.IsFirst == 1) {
          auto used_arg = Ins[b.Caller].SSADependencies[i - Ins.size()];
          if ((used_arg.first == InstInfo::Inst && Ins[used_arg.second].IsPointer) || used_arg.first == InstInfo::PointerArg) {
            auto dependent_ins = I(b.TID, used_arg.second);
            if (used_arg.first == InstInfo::PointerArg) {
              dependent_ins.second += Ins.size();
              labeled_args[dependent_ins.first].insert(dependent_ins.second);
            }

            // Merging the argument's group withe the variable's group
            if (Ins2Group.count(dependent_ins)) {
              set<int> merging; merging.insert(arg_group); merging.insert(Ins2Group[dependent_ins]);
              arg_group = Merging(merging);
            }

            Ins2Group[dependent_ins] = arg_group;
            Group2Ins[arg_group].insert(dependent_ins);
          }
        }
      }
    }
  }
  return new_group;
}

/// Group the memory address into groups.
/// Two addresses will be group into one group
/// if one can be calculated from the other.
///
/// \param merged_trace_file_name - the merged trace outputed by the merge-trace tool.
/// \param output_file_name - the path to output file.
///
void GroupMemory(char *merged_trace_file_name, char *output_file_name) {
  map<uint64_t, set<uint32_t> > LabeledArgs;

  Addr2Group = SegmentTree<int>::NewTree();
  Group2Addr.clear();
  MaxGroupID = 0;

  boost::iostreams::mapped_file_source trace(merged_trace_file_name);
  auto data = trace.data();
  set<int> shoud_merge;
  for (int64_t cur = trace.size(); cur > 0;) {
    SmallestBlock b; b.ReadBack(data, cur);

    if (b.Type == SmallestBlock::MemoryAccessBlock || b.Type == SmallestBlock::MemsetBlock || b.Type == SmallestBlock::MemmoveBlock) {
      if (b.Addr[0] >= b.Addr[1]) continue; // Inefficacious write
      
      shoud_merge.clear();
      auto ins = I(b.TID, BB2Ins[b.BBID][b.Start]);

      // All the addresses of [Addr[0], Addr[1]) should belong to the same group
      vector<pair<uint64_t, uint64_t> > ranges;
      for (auto i: Collect(b.Addr[0], b.Addr[1])) {
        if (i.type == EMPTY_SEGMENT) {
          ranges.push_back(make_pair(i.left, i.right));
        } else {
          shoud_merge.insert(i.value);
        }
      }
      if (b.Type == SmallestBlock::MemmoveBlock) {
        for (auto i: Collect(b.Addr[2], b.Addr[3])) {
          if (i.type == EMPTY_SEGMENT) {
            ranges.push_back(make_pair(i.left, i.right));
          } else {
            shoud_merge.insert(i.value);
          }
        }
      }

      auto new_group = MergeInst(shoud_merge, ins, b, LabeledArgs);

      for (auto i: ranges) {
        Group2Addr[new_group]->Set(i.first, i.second, 1);
        Addr2Group->Set(i.first, i.second, new_group);
      }
    } else if (b.Type == SmallestBlock::NormalBlock) {
      for (int _ = b.End - 1; _ >= (int)b.Start; --_) {
        auto ins = I(b.TID, BB2Ins[b.BBID][_]);
        if (!Ins[ins.second].IsPointer || Ins[ins.second].Type == InstInfo::CallInst) continue;
        
        shoud_merge.clear();
        MergeInst(shoud_merge, ins, b, LabeledArgs);
      }
    }
  }

  // auto cur = Addr2Group->Collect(0, SegmentTree<int>::MAX_RANGE);
  // for (auto i: cur) {
  //   printf("[%lx,%lx): %d %d\n", i.left, i.right, i.type, i.value);
  // }

  ExtractMemoryDependency(merged_trace_file_name, output_file_name);
}

int main(int argc, char *argv[]) {
  if (argc != 3 && argc != 4) {
    printf("Usage: extract-memory-dependency inst-file merged-trace-file [output-file]\n");
    exit(1);
  }
  LoadInstInfo(argv[1], Ins, BB2Ins);
  if (argc == 3) {
    GroupMemory(argv[2], "SlimmerMemoryDependency");
  } else {
    GroupMemory(argv[2], argv[3]);
  }
}