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

void GroupMemory(char *trace_file_name) {
  Addr2Group = SegmentTree::NewTree();
  Group2Addr.clear();
  MaxGroupID = 0;

  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid, *length;
  const uint32_t *id;
  const void **addr;
  
  set<int> shoud_merge;
  for (int64_t cur = trace.size() - 1; cur > 0; --cur) {
    event_label = data[cur];
    switch (event_label) {
      case ReturnEventLabel:
        cur -= (96 + size_of_ptr);
        break;
      case BasicBlockEventLabel:
        cur -= 32; id = (const uint32_t *)(&data[cur]);
        cur -= 64; tid = (const uint64_t *)(&data[cur]);
        // printf("BasicBlockEvent: %lu\t%u\n", *tid, *id);

        for (int _ = BB2Ins[*id].size() - 1; _ >= 0; --_) {
          int ins_id = BB2Ins[*id][_];
          if (!Ins[ins_id].IsPointer) continue;

          // Merging
          shoud_merge.clear();
          if (Ins2Group.count(I(*tid, ins_id)))
            shoud_merge.insert(Ins2Group[I(*tid, ins_id)]);
          for (auto dep: Ins[ins_id].SSADependencies) {
            if (dep.first == InstInfo::Inst && Ins[dep.second].IsPointer
              && Ins2Group.count(I(*tid, dep.second))) {
              shoud_merge.insert(Ins2Group[I(*tid, dep.second)]);
            }
          }
          int new_group = Merging(shoud_merge);
          
          // Clear old groups
          for (auto dep: Ins[ins_id].SSADependencies) {
            if (!(dep.first == InstInfo::Inst && Ins[dep.second].IsPointer)) continue;
            
            Ins2Group[I(*tid, dep.second)] = new_group;
            Group2Ins[new_group].insert(I(*tid, dep.second));
          }
          Group2Ins[Ins2Group[I(*tid, ins_id)]].erase(I(*tid, ins_id));
          Ins2Group.erase(I(*tid, ins_id));
        }
        break;
      case MemoryEventLabel:
        cur -= 64;  length = (const uint64_t *)(&data[cur]);
        cur -= size_of_ptr; addr = (const void **)(&data[cur]);
        cur -= 32; id = (const uint32_t *)(&data[cur]);
        cur -= 64; tid = (const uint64_t *)(&data[cur]);
        // printf("MemoryEvent:     %lu\t%u\t%p\t%lu\n", *tid, *id, *addr, *length);

        shoud_merge.clear();
        vector<pair<uint64_t, uint64_t> > ranges;
        for (auto i: Collect((uint64_t)(*addr), (uint64_t)(*addr) + (*length))) {
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
        
        int pointer_id = (Ins[*id].Type == InstInfo::LoadInst ? Ins[*id].SSADependencies[0].second : Ins[*id].SSADependencies[1].second);
        Ins2Group[I(*tid, pointer_id)] = new_group;
        Group2Ins[new_group].insert(I(*tid, pointer_id));
        break;
    }
  }

  auto cur = Addr2Group->Collect(0, SegmentTree::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lx,%lx):%d\n", get<1>(i), get<2>(i), get<0>(i));
  }

  delete Addr2Group;
  for (auto& i: Group2Addr) {
    delete i.second;
  }
  Group2Addr.clear();
}

int main(int argc, char *argv[]) {
  LoadInstInfo(argv[1], Ins, BB2Ins);
  GroupMemory(argv[2]);
}