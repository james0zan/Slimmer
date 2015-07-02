#include "SlimmerUtil.h"
#include "SegmentTree.hpp"

#include <set>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

// A segment tree that maps a memory address to its group
SegmentTree<int> *Addr2Group;
// For each group, we use a segment tree to record all the memory addresses that belong to it
map<uint32_t, SegmentTree<int> *> Group2Addr;
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


void ExtractMemoryDependency(char *trace_file_name, char *output_file_name) {
  FILE *output_file = fopen(output_file_name, "w");
  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  SegmentTree<DynamicInst> *Addr2LastStore= SegmentTree<DynamicInst>::NewTree();
  map<pair<uint64_t, uint32_t>, uint32_t > InstCount;
  map<uint64_t, set<uint32_t> > ArgGroup;

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
    
  bool ended = false;
  char *buffer = (char *)malloc(COMPRESS_BLOCK_SIZE);
  for (size_t _ = 0; !ended && _ < trace.size();) {
    uint64_t length = (*(uint64_t *)(&data[_]));
    _ += sizeof(uint64_t);

    uint64_t decoded = LZ4_decompress_safe ((const char*) &data[_], buffer, length, COMPRESS_BLOCK_SIZE);
    for (uint64_t cur = 0; !ended && cur < decoded;) {
      cur += GetEvent(false, buffer + cur, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
      if (event_label == MemoryEventLabel) {
        if (Ins[*id_ptr].Type == InstInfo::StoreInst) {
          DynamicInst dyn_inst = DynamicInst(*tid_ptr, *id_ptr, InstCount[I(*tid_ptr, *id_ptr)]++);
          Addr2LastStore->Set(*addr_ptr, *addr_ptr + *length_ptr, dyn_inst);
        } else {
          DynamicInst dyn_inst = DynamicInst(*tid_ptr, *id_ptr, InstCount[I(*tid_ptr, *id_ptr)]++);
          printf("The %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu is depended on:\n",
            dyn_inst.Cnt, dyn_inst.ID, Ins[dyn_inst.ID].Code.c_str(), dyn_inst.TID);

          for (auto j: Addr2LastStore->Collect(*addr_ptr, *addr_ptr + *length_ptr)) {
            if (j.type == COVERED_SEGMENT) {
              printf("\t* the %d-th execution of\n\t  instruction %d, %s\n\t  from thread %lu\n",
                j.value.Cnt, j.value.ID, Ins[j.value.ID].Code.c_str(), j.value.TID);
              fprintf(output_file, "%lu %d %d %lu %d %d\n",
                dyn_inst.TID, dyn_inst.ID, dyn_inst.Cnt,
                j.value.TID, j.value.ID, j.value.Cnt);
            }
          }
        }
      } else if (event_label == ArgumentEventLabel) {
        int group_id;
        if (Addr2Group->Get(*addr_ptr, group_id))
          ArgGroup[*tid_ptr].insert(group_id);
      } else if (event_label == ReturnEventLabel) {
        DynamicInst dyn_inst = DynamicInst(*tid_ptr, *id_ptr, InstCount[I(*tid_ptr, *id_ptr)]++);
        printf("The %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu is depended on:\n",
            dyn_inst.Cnt, dyn_inst.ID, Ins[dyn_inst.ID].Code.c_str(), dyn_inst.TID);
        for (auto group_id: ArgGroup[*tid_ptr]) {
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
        ArgGroup.erase(*tid_ptr);  
      }
    }
    _ += length + sizeof(uint64_t);
  }

  fprintf(output_file, "0 -1 -1 0 -1 -1\n");
  for (auto i: InstCount) {
    printf("Thread %lu has executed instruction %d %d-th\n", i.first.first, i.first.second, i.second);
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

void GroupMemory(char *trace_file_name, char *output_file_name) {
  Addr2Group = SegmentTree<int>::NewTree();
  Group2Addr.clear();
  MaxGroupID = 0;

  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  set<int> shoud_merge;

  bool ended = false;
  char *buffer = (char *)malloc(COMPRESS_BLOCK_SIZE);
  for (int64_t _ = trace.size(); _ > 0;) {
    _ -= sizeof(uint64_t);
    uint64_t length = (*(uint64_t *)(&data[_]));
    _ -= length;

    uint64_t decoded = LZ4_decompress_safe ((const char*) &data[_], buffer, length, COMPRESS_BLOCK_SIZE);
    for (int64_t cur = decoded - 1; cur > 0;) {
      cur -= GetEvent(true, buffer + cur, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
      if (event_label == MemoryEventLabel) {
        shoud_merge.clear();
        vector<pair<uint64_t, uint64_t> > ranges;
        for (auto i: Collect(*addr_ptr, (*addr_ptr) + (*length_ptr))) {
          if (i.type == EMPTY_SEGMENT) {
            ranges.push_back(make_pair(i.left, i.right));
          } else {
            shoud_merge.insert(i.value);
          }
        }
          
        int new_group = Merging(shoud_merge);
        for (auto i: ranges) {
          Group2Addr[new_group]->Set(i.first, i.second, 1);
          Addr2Group->Set(i.first, i.second, new_group);
        }
        
        auto pointer_type = (Ins[*id_ptr].Type == InstInfo::LoadInst ? Ins[*id_ptr].SSADependencies[0].first : Ins[*id_ptr].SSADependencies[1].first);
        if (pointer_type == InstInfo::Inst) {
          int pointer_id = (Ins[*id_ptr].Type == InstInfo::LoadInst ? Ins[*id_ptr].SSADependencies[0].second : Ins[*id_ptr].SSADependencies[1].second);
          // printf("=====\nSet %d, %s\n%d, %s\n%p %p to %d\n", *id_ptr, Ins[*id_ptr].Code.c_str(), pointer_id, Ins[pointer_id].Code.c_str(), (void*)*addr_ptr, (void*)((*addr_ptr) + (*length_ptr)), new_group);
          auto ins = I(*tid_ptr, pointer_id);
          if (Ins2Group.count(ins)) Group2Ins[Ins2Group[ins]].erase(ins);
          Ins2Group[ins] = new_group; Group2Ins[new_group].insert(ins);
        } else if (pointer_type == InstInfo::Arg) {
          //TODO
        }
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
          // printf("=====\nID: %d, %s\nMerge", ins_id, Ins[ins_id].Code.c_str());
          // for (auto i: shoud_merge) printf(" %d", i);
          // printf(" to %d\n", new_group);
          
          // Clear old groups
          for (auto dep: Ins[ins_id].SSADependencies) {
            if (!(dep.first == InstInfo::Inst && Ins[dep.second].IsPointer)) continue;
            auto dependent_ins = I(*tid_ptr, dep.second);
            Ins2Group[dependent_ins] = new_group;
            Group2Ins[new_group].insert(dependent_ins);
            // printf("Set %d to %d\n", dep.second, new_group);
          }
          Group2Ins[Ins2Group[ins]].erase(ins);
          Ins2Group.erase(ins);
        }
      }
    }
    _ -= sizeof(uint64_t);
  }

  auto cur = Addr2Group->Collect(0, SegmentTree<int>::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lx,%lx): %d %d\n", i.left, i.right, i.type, i.value);
  }

  ExtractMemoryDependency(trace_file_name, output_file_name);
}

int main(int argc, char *argv[]) {
  if (argc != 3 && argc != 4) {
    printf("Usage: extract-memory-dependency inst-file trace-file [output-file]\n");
    exit(1);
  }
  LoadInstInfo(argv[1], Ins, BB2Ins);
  if (argc == 3) {
    GroupMemory(argv[2], "SlimmerMemoryDependency");
  } else {
    GroupMemory(argv[2], argv[3]);
  }
}