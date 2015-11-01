#include "SlimmerTools.h"

// Counting the number of memory groups.
int MaxGroupID;
// <Thread ID, Instruction ID> -> Its group.
map<pair<uint64_t, uint32_t>, int> Ins2Group;
// A memory group -> all the instructions belong to it.
map<int, set<pair<uint64_t, uint32_t> > > Group2Ins;

//===----------------------------------------------------------------------===//
//                        Segment Tree
//===----------------------------------------------------------------------===//

/// Collect all the groups within a specific range of memory.
///
/// \param l - the starting address of the memory.
/// \param r - the lasting address + 1 (not included) of memory.
/// \param addr_group - the segment tree that maps a memory address to its
/// group.
/// \return - a list of segments that are belonged to different groups.
///
vector<Segment<int> > Collect(uint64_t l, uint64_t r,
                              SegmentTree<int> *addr_group) {
  auto segments = addr_group->Collect(l, r);
  segments[0].left = l;
  segments[segments.size() - 1].right = r;

  return segments;
}

/// Merging a list of segment trees into one segment tree.
///
/// \param trees - a list of segment trees that should be merged.
/// \param new_group - the merged group ID.
/// \param addr_group - the segment tree that maps a memory address to its
/// group.
/// \return - the merged segment tree.
///
SegmentTree<int> *Merging(vector<SegmentTree<int> *> trees, int new_group,
                          SegmentTree<int> *addr_group) {
  vector<SegmentTree<int> *> l_childs, r_childs;
  for (auto i : trees) {
    if (i->type == COVERED_SEGMENT) {
      addr_group->Set(i->left, i->right, new_group);
      return new SegmentTree<int>(COVERED_SEGMENT, 1, i->left, i->right);
    } else if (i->type == PARTIAL_SEGMENT) {
      l_childs.push_back(i->l_child);
      r_childs.push_back(i->r_child);
    }
  }
  if (l_childs.size() == 0) {
    return new SegmentTree<int>(EMPTY_SEGMENT, 0, trees[0]->left,
                                trees[0]->right);
  }
  return new SegmentTree<int>(PARTIAL_SEGMENT, -1, trees[0]->left,
                              trees[0]->right,
                              Merging(l_childs, new_group, addr_group),
                              Merging(r_childs, new_group, addr_group));
}

/// Merging a list of groups into one group.
///
/// \param groups - a list of groups that should be merged.
/// \return - the merged group ID.
///
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
  for (auto i : groups) {
    if (Group2Addr.count(i))
      trees.push_back(Group2Addr[i]);
  }
  Group2Addr[new_group] = Merging(trees, new_group, Addr2Group);

  for (auto i : groups) {
    if (i == new_group)
      continue;

    auto tree = Group2Addr[i];
    if (tree)
      delete tree;
    Group2Addr.erase(i);

    for (auto ins : Group2Ins[i])
      Ins2Group[ins] = new_group;
    Group2Ins[new_group].insert(Group2Ins[i].begin(), Group2Ins[i].end());
    Group2Ins.erase(i);
  }

  return new_group;
}

//===----------------------------------------------------------------------===//
//                        GroupMemory
//===----------------------------------------------------------------------===//

/// Merging all the addresses used in an instruction into one group.
///
/// \param shoud_merge - a set of groups that should be merged.
/// \param ins - a pair of {Thread ID, Instruction ID}.
/// \param b - the current SmallestBlock object that contains ins.
/// \param labeled_args - a map that maps the thread ID to arguments that have a
/// group label attached.
/// \return - the group ID of merged group.
///
int MergeInst(set<int> &shoud_merge, pair<uint64_t, uint32_t> ins,
              SmallestBlock &b, map<uint64_t, set<uint32_t> > &labeled_args) {

  auto ins_id = ins.second;

  // Merging the result variable
  if (Ins2Group.count(ins)) {
    shoud_merge.insert(Ins2Group[ins]);
  }
  // Merging all the dependencies
  for (auto dep : Ins[ins_id].SSADependencies) {
    if ((dep.first == InstInfo::Inst && Ins[dep.second].IsPointer) ||
        dep.first == InstInfo::PointerArg) {
      auto dependent_ins = I(b.TID, dep.second);
      if (dep.first == InstInfo::PointerArg)
        dependent_ins.second += Ins.size();

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
  for (auto dep : Ins[ins_id].SSADependencies) {
    if ((dep.first == InstInfo::Inst && Ins[dep.second].IsPointer) ||
        dep.first == InstInfo::PointerArg) {
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

    for (auto i : labeled_arg) {
      auto dependent_arg = I(b.TID, i);
      if (Ins2Group.count(dependent_arg)) {
        uint32_t arg_group = Ins2Group[dependent_arg];
        Group2Ins[Ins2Group[dependent_arg]].erase(dependent_arg);
        Ins2Group.erase(dependent_arg);

        // Map the argument with the corresponding value
        if (b.IsFirst == 1 && b.Caller != (uint32_t) - 1) {
          auto used_arg = Ins[b.Caller].SSADependencies[i - Ins.size()];
          if ((used_arg.first == InstInfo::Inst &&
               Ins[used_arg.second].IsPointer) ||
              used_arg.first == InstInfo::PointerArg) {
            auto dependent_ins = I(b.TID, used_arg.second);
            if (used_arg.first == InstInfo::PointerArg) {
              dependent_ins.second += Ins.size();
              labeled_args[dependent_ins.first].insert(dependent_ins.second);
            }

            // Merging the argument's group withe the variable's group
            if (Ins2Group.count(dependent_ins)) {
              set<int> merging;
              merging.insert(arg_group);
              merging.insert(Ins2Group[dependent_ins]);
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
/// \param merged_trace_file_name - the merged trace outputed by the merge-trace
/// tool.
/// \param output_file_name - the path to output file.
///
void GroupMemory(vector<SmallestBlock> &block_trace) {
  MaxGroupID = 0;
  map<uint64_t, set<uint32_t> > labeled_args;

  set<int> shoud_merge;
  for (int i = block_trace.size() - 1; i >= 0; --i) {
    SmallestBlock b = block_trace[i];
    // b.Print(Ins, BB2Ins);
    shoud_merge.clear();

    if (b.Type == SmallestBlock::MemoryAccessBlock ||
        b.Type == SmallestBlock::MemsetBlock ||
        b.Type == SmallestBlock::MemmoveBlock) {
      if (b.Addr[0] >= b.Addr[1])
        continue; // Inefficacious write

      auto ins = I(b.TID, BB2Ins[b.BBID][b.Start]);

      // All the addresses of [Addr[0], Addr[1]) should belong to the same group
      vector<pair<uint64_t, uint64_t> > ranges;
      for (auto i : Collect(b.Addr[0], b.Addr[1], Addr2Group)) {
        if (i.type == EMPTY_SEGMENT) {
          ranges.push_back(make_pair(i.left, i.right));
        } else {
          shoud_merge.insert(i.value);
        }
      }
      if (b.Type == SmallestBlock::MemmoveBlock) {
        for (auto i : Collect(b.Addr[2], b.Addr[3], Addr2Group)) {
          if (i.type == EMPTY_SEGMENT) {
            ranges.push_back(make_pair(i.left, i.right));
          } else {
            shoud_merge.insert(i.value);
          }
        }
      }

      auto new_group = MergeInst(shoud_merge, ins, b, labeled_args);

      for (auto i : ranges) {
        Group2Addr[new_group]->Set(i.first, i.second, 1);
        Addr2Group->Set(i.first, i.second, new_group);
      }
    } else if (b.Type == SmallestBlock::DeclareBlock) {
      // All the addresses of [Addr[0], Addr[1]) should belong to the same group
      vector<pair<uint64_t, uint64_t> > ranges;
      for (auto i : Collect(b.Addr[0], b.Addr[1], Addr2Group)) {
        if (i.type == EMPTY_SEGMENT) {
          ranges.push_back(make_pair(i.left, i.right));
        } else {
          shoud_merge.insert(i.value);
        }
      }
      int new_group = Merging(shoud_merge);
      for (auto i : ranges) {
        Group2Addr[new_group]->Set(i.first, i.second, 1);
        Addr2Group->Set(i.first, i.second, new_group);
      }
    } else if (b.Type == SmallestBlock::ExternalCallBlock ||
               b.Type == SmallestBlock::ImpactfulCallBlock) {
      for (size_t i = 1; i < b.Addr.size(); ++i) {
        int group_id;
        if (!Addr2Group->Get(b.Addr[i], group_id)) {
          // If this address is not accessed before, add a group for it.
          auto new_group = (++MaxGroupID);
          Group2Addr[new_group] = SegmentTree<int>::NewTree();
          Group2Addr[new_group]->Set(b.Addr[i], b.Addr[i] + 1, 1);
          Addr2Group->Set(b.Addr[i], b.Addr[i] + 1, new_group);
        }
      }
    } else if (b.Type == SmallestBlock::NormalBlock) {
      for (int _ = b.End - 1; _ >= (int)b.Start; --_) {
        auto ins = I(b.TID, BB2Ins[b.BBID][_]);
        if (!Ins[ins.second].IsPointer ||
            Ins[ins.second].Type == InstInfo::CallInst)
          continue;

        shoud_merge.clear();
        MergeInst(shoud_merge, ins, b, labeled_args);
      }
    }
  }

  // auto cur = Addr2Group->Collect(0, SegmentTree<int>::MAX_RANGE);
  // for (auto i: cur) {
  //   printf("[%lx,%lx): %d %d\n", i.left, i.right, i.type, i.value);
  // }
}
