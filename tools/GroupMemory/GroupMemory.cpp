#include "SlimmerUtil.h"

#include <set>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

// A segment tree that maps a memory address to its group
SegmentTree *Addr2Group;
// For each group, we use a segment tree to record all the memory addresses that belong to it
map<uint32_t, SegmentTree *> Group2Addr;
// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

namespace boost {
void throw_exception(std::exception const& e) {}
}

inline pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id) {
  return make_pair(tid, id);
}

/// Collect all the groups within a specific range of memory.
///
/// \param l - the starting address of the memory.
/// \param r - the lasting address + 1 (not included) of memory.
/// \return - a list of segments that are belonged to different groups.
///
vector<Segment> Collect(uint64_t l, uint64_t r) {
  auto segments = Addr2Group->Collect(l, r);
  
  Segment& first = segments[0];
  first = make_tuple(get<0>(first), l, get<2>(first));
  
  Segment& last = segments[segments.size() - 1];
  last = make_tuple(get<0>(last), get<1>(last), r);

  return segments;
}

/// Merging a list of segment trees into one segment tree.
///
/// \param trees - a list of segment trees that should be merged.
/// \param new_group - the merged group ID.
/// \return - the merged segment tree.
///
SegmentTree* Merging(vector<SegmentTree *> trees, int new_group) {
  vector<SegmentTree *> l_childs, r_childs;
  for (auto i: trees) {
    if (i->value == 1) {
      Addr2Group->Set(i->left, i->right, new_group);
      return new SegmentTree(1, i->left, i->right);
    } else if (i->value == -1) {
      l_childs.push_back(i->l_child);
      r_childs.push_back(i->r_child);
    }
  }
  if (l_childs.size() == 0) {
    return new SegmentTree(0, trees[0]->left, trees[0]->right);
  }
  return new SegmentTree(-1, trees[0]->left, trees[0]->right,
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
    Group2Addr[new_group] = SegmentTree::NewTree();
    return new_group;
  }
  
  new_group = *groups.begin();
  if (groups.size() == 1) {
    return new_group;
  }

  // Merging two or more groups
  vector<SegmentTree *> trees;
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

int GetEvent(bool backward, const char *cur, 
  char& event_label, const uint64_t*& tid_ptr, const uint32_t*& id_ptr,
  const uint64_t*& addr_ptr, const uint64_t*& length_ptr) {
  event_label = (*cur);

  switch (event_label) {
  case EndEventLabel: return 1;
  case BasicBlockEventLabel:
    if (backward) cur -= SizeOfBasicBlockEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 65);
    // printf("BasicBlockEvent: %lu\t%u\n", *tid_ptr, *id_ptr);
    return SizeOfBasicBlockEvent;
  case MemoryEventLabel:
    if (backward) cur -= SizeOfMemoryEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 65);
    addr_ptr = (const uint64_t *)(cur + 97);
    length_ptr = (const uint64_t *)(cur + 161);
    // printf("MemoryEvent:     %lu\t%u\t%p\t%lu\n", *tid_ptr, *id_ptr, (void*)*addr_ptr, *length_ptr);
    return SizeOfMemoryEvent;
  case ReturnEventLabel:
    if (backward) cur -= SizeOfReturnEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 65);
    addr_ptr = (const uint64_t *)(cur + 97);
    // printf("ReturnEvent:     %lu\t%u\t%p\n", *tid_ptr, *id_ptr, (void*)*addr_ptr);
    return SizeOfReturnEvent;
  }
}

void GroupMemory(char *trace_file_name) {
  Addr2Group = SegmentTree::NewTree();
  Group2Addr.clear();
  MaxGroupID = 0;

  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  set<int> shoud_merge;
  for (int64_t cur = trace.size() - 1; cur > 0;) {
    cur -= GetEvent(true, &data[cur], event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    if (event_label == MemoryEventLabel) {
      shoud_merge.clear();
      vector<pair<uint64_t, uint64_t> > ranges;
      for (auto i: Collect(*addr_ptr, (*addr_ptr) + (*length_ptr))) {
        int origin_group = get<0>(i);
        if (origin_group == 0) {
          ranges.push_back(make_pair(get<1>(i), get<2>(i)));
        } else {
          shoud_merge.insert(origin_group);
        }
      }
        
      int new_group = Merging(shoud_merge);
      for (auto i: ranges) {
        Group2Addr[new_group]->Set(i.first, i.second, 1);
        Addr2Group->Set(i.first, i.second, new_group);
      }
        
      int pointer_id = (Ins[*id_ptr].Type == InstInfo::LoadInst ? Ins[*id_ptr].SSADependencies[0].second : Ins[*id_ptr].SSADependencies[1].second);
      auto ins = I(*tid_ptr, pointer_id);
      if (Ins2Group.count(ins)) Group2Ins[Ins2Group[ins]].erase(ins);
      Ins2Group[ins] = new_group; Group2Ins[new_group].insert(ins);
    } else if (event_label == BasicBlockEventLabel) {
      for (int _ = BB2Ins[*id_ptr].size() - 1; _ >= 0; --_) {
        int ins_id = BB2Ins[*id_ptr][_];
        if (!Ins[ins_id].IsPointer) continue;
        if (Ins[ins_id].Type == InstInfo::CallInst) continue;

        // Merging
        shoud_merge.clear();
        auto ins = I(*tid_ptr, ins_id);
        if (Ins2Group.count(ins)) shoud_merge.insert(Ins2Group[ins]);
        for (auto dep: Ins[ins_id].SSADependencies) {
          if (!(dep.first == InstInfo::Inst && Ins[dep.second].IsPointer)) continue;
          pair<uint64_t, uint32_t> dependent_ins = I(*tid_ptr, dep.second);
          if (Ins2Group.count(dependent_ins)) shoud_merge.insert(Ins2Group[dependent_ins]);
        }
        int new_group = Merging(shoud_merge);
        
        // Clear old groups
        for (auto dep: Ins[ins_id].SSADependencies) {
          if (!(dep.first == InstInfo::Inst && Ins[dep.second].IsPointer)) continue;
          auto dependent_ins = I(*tid_ptr, dep.second);
          Ins2Group[dependent_ins] = new_group;
          Group2Ins[new_group].insert(dependent_ins);
        }
        Group2Ins[Ins2Group[ins]].erase(ins);
        Ins2Group.erase(ins);
      }
    }
  }

  // auto cur = Addr2Group->Collect(0, SegmentTree::MAX_RANGE);
  // for (auto i: cur) {
  //   printf("[%lx,%lx):%d\n", get<1>(i), get<2>(i), get<0>(i));
  // }

  // delete Addr2Group;
  // for (auto& i: Group2Addr) {
  //   delete i.second;
  // }
  // Group2Addr.clear();
}

void ExtractMemoryDependency(char *trace_file_name, char *output_file_name) {
  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  for (int64_t cur = 0; cur < trace.size();) {
    cur += GetEvent(true, &data[cur], event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    // if (event_label == MemoryEventLabel) {
    //   if (Ins[*id_ptr].Type == InstInfo::StoreInst) {
    //     Addr2LastStore->Set(*addr_ptr, *addr_ptr + *length_ptr, dyn);
    //   } else {
    //     for (auto j: Addr2LastStore->Collect(*addr_ptr, *addr_ptr + *length_ptr)) {
    //       if (get<0>(j) != 0) {
    //         OutputMemoryDependency(dyn, get<0>(j));
    //       }
    //     }
    //   }
    // } else if (event_label == ReturnEvent) {
      
    // }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 3 && argc != 4) {
    printf("Usage: group-memory inst-file trace-file [output-file]\n");
    exit(1);
  }
  LoadInstInfo(argv[1], Ins, BB2Ins);
  GroupMemory(argv[2]);
  if (argc == 3) {
    ExtractMemoryDependency(argv[2], "SlimmerMemoryDependency");
  } else {
    ExtractMemoryDependency(argv[2], argv[3]);
  }
}