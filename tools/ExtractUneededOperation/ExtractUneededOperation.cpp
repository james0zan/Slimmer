#include "SlimmerUtil.h"
#include "SegmentTree.hpp"

#include <set>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

namespace boost {
void throw_exception(std::exception const& e) {}
}

inline pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id) {
  return make_pair(tid, id);
}

// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

map<DynamicInst, vector<DynamicInst> > MemDependencies;
void LoadMemDependency(char *mem_dependencies_file_name, map<DynamicInst, vector<DynamicInst> >& MemDependencies) {
  FILE *f = fopen(mem_dependencies_file_name, "r");
  map<DynamicInst, vector<DynamicInst> > tmp;
  DynamicInst a, b;
  while (true) {
    fscanf(f, "%lu%d%d%lu%d%d", &a.TID, &a.ID, &a.Cnt, &b.TID, &b.ID, &b.Cnt);
    if (a.TID == 0 && a.ID == -1 && a.Cnt == -1) break;
    tmp[a].push_back(b);
  }
  uint64_t tid; int32_t id, cnt;
  map<pair<uint64_t, uint32_t>, uint32_t> count;
  while (fscanf(f, "%lu%d%d", &tid, &id, &cnt) != EOF) {
    count[I(tid, id)] = cnt - 1;
  }

  for (auto& i: tmp) {
    DynamicInst tmp_a = i.first;
    tmp_a.Cnt -= count[I(i.first.TID, i.first.ID)];
    // printf("The last %d-th execution of\n  instruction %d, %s\n  from thread %lu is depended on:\n",
    //       tmp_a.Cnt, tmp_a.ID, Ins[tmp_a.ID].Code.c_str(), tmp_a.TID);
    for (auto& j: i.second) {
      DynamicInst tmp_b = j;
      tmp_b.Cnt -= count[I(j.TID, j.ID)];

      // printf("  * the last  %d-th execution of\n    instruction %d, %s\n    from thread %lu is depended on\n",
      //   tmp_b.Cnt, tmp_b.ID, Ins[tmp_b.ID].Code.c_str(), tmp_b.TID);
      MemDependencies[tmp_a].push_back(tmp_b);
    }
  }
  // puts("==============");
}

set<tuple<uint64_t, uint64_t, int32_t> > ImpactfulFunCall;
void LoadImpactfulFunCall(char *impacful_fun_call_file_name, set<tuple<uint64_t, uint64_t, int32_t> >& ImpactfulFunCall) {
  FILE *f = fopen(impacful_fun_call_file_name, "r");
  uint64_t tid, fun; int32_t cnt;
  while (fscanf(f, "%lu%lu%d", &tid, &fun, &cnt) != EOF) {
    ImpactfulFunCall.insert(make_tuple(tid, fun, cnt));
  }
}

void ExtractUneededOperation(char *trace_file_name, char *output_file_name) {
  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid_ptr, *length_ptr, *addr_ptr;
  const uint32_t *id_ptr;
  
  map<pair<uint64_t, uint64_t>, int32_t> FunCount;
  map<pair<uint64_t, uint32_t>, int32_t> InstCount;
  set<uint64_t> used_successor;
  set<DynamicInst> impactful_call_ins, mem_depended;
  set<pair<uint64_t, uint32_t> > needed;

  // TODO: Arg, Last return
  char *buffer = (char *)malloc(COMPRESS_BLOCK_SIZE);
  for (int64_t _ = trace.size(); _ > 0;) {
    _ -= sizeof(uint64_t);
    uint64_t length = (*(uint64_t *)(&data[_]));
    _ -= length;

    uint64_t decoded = LZ4_decompress_safe ((const char*) &data[_], buffer, length, COMPRESS_BLOCK_SIZE);
    for (int64_t cur = decoded - 1; cur > 0;) {
      cur -= GetEvent(true, buffer + cur, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
      if (event_label == ReturnEventLabel) {
        auto dyn_fun_call = make_tuple(*tid_ptr, *addr_ptr, -FunCount[make_pair(*tid_ptr, *addr_ptr)]);
        if (ImpactfulFunCall.count(dyn_fun_call)) {
          impactful_call_ins.insert(DynamicInst(*tid_ptr, *id_ptr, -InstCount[I(*tid_ptr, *id_ptr)]));
          ImpactfulFunCall.erase(dyn_fun_call);
        }
        FunCount[I(*tid_ptr, *addr_ptr)]++;
      } else if (event_label == BasicBlockEventLabel) {
        bool used = false;
        for (int _ = BB2Ins[*id_ptr].size() - 1; _ >= 0; --_) {
          int ins_id = BB2Ins[*id_ptr][_];
          DynamicInst dyn_ins(*tid_ptr, ins_id, -InstCount[I(*tid_ptr, ins_id)]);

          bool is_needed = (needed.count(I(*tid_ptr, ins_id)) > 0);
          // Impactful function call
          is_needed |= (Ins[ins_id].Type == InstInfo::CallInst) && (impactful_call_ins.count(dyn_ins) > 0);
          impactful_call_ins.erase(dyn_ins);
          // Contol depended
          is_needed |= (Ins[ins_id].Type == InstInfo::TerminatorInst) && (used_successor.count(*tid_ptr) > 0);
          // Memory depended
          is_needed |= (mem_depended.count(dyn_ins) > 0);
          mem_depended.erase(dyn_ins);

          if (!is_needed) {
            printf("!!!The last %d-th execution of\n  instruction %d, %s\n  from thread %lu is uneeded.\n",
              InstCount[I(*tid_ptr, ins_id)], ins_id, Ins[ins_id].Code.c_str(), *tid_ptr);
          } else {
            used = true;
            needed.erase(I(*tid_ptr, ins_id));
            
            printf("The last %d-th execution of\n  instruction %d, %s\n  from thread %lu is depended on:\n",
              dyn_ins.Cnt, dyn_ins.ID, Ins[dyn_ins.ID].Code.c_str(), dyn_ins.TID);

            // SSA dependencies
            for (auto dep: Ins[ins_id].SSADependencies) {
              if (dep.first == InstInfo::Inst) {
                printf("  * the last execution of\n\tinstruction %d, %s\n\tfrom thread %lu\n",
                  dep.second, Ins[dep.second].Code.c_str(), *tid_ptr);
                needed.insert(I(*tid_ptr, dep.second));
              } else if (dep.first == InstInfo::Arg) {
                // TODO
              }
            }
            // Memory dependencies
            for (auto dep: MemDependencies[dyn_ins]) {
              mem_depended.insert(dep);
              printf("  * the last %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu\n",
                  dep.Cnt, dep.ID, Ins[dep.ID].Code.c_str(), dep.TID);
            }
          }
          InstCount[I(*tid_ptr, ins_id)]++;
        }
        // A basic block is used if at least one of its instruction is used
        if (used) {
          used_successor.insert(*tid_ptr);
        } else {
          used_successor.erase(*tid_ptr);
        }
      }
    }
    _ -= sizeof(uint64_t);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 5 && argc != 6) {
    printf("Usage: extract-uneeded-operation inst-file mem-dependencies impactful-fun-call trace-file [output-file]\n");
    exit(1);
  }
  LoadInstInfo(argv[1], Ins, BB2Ins);
  LoadMemDependency(argv[2], MemDependencies);
  LoadImpactfulFunCall(argv[3], ImpactfulFunCall);
  if (argc == 5) {
    ExtractUneededOperation(argv[4], "SlimmerUneededOperation");
  } else {
    ExtractUneededOperation(argv[4], argv[5]);
  }
}