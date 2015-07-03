#include "SlimmerTools.h"

#include <set>
using namespace std;

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

void OneInstruction(bool is_needed, DynamicInst dyn_ins, set<pair<uint64_t, uint32_t> >& needed, set<DynamicInst>& mem_depended) {
  if (!is_needed) {
    printf("!!!The last %d-th execution of\n  instruction %d, %s\n  from thread %lu is uneeded.\n",
      dyn_ins.Cnt, dyn_ins.ID, Ins[dyn_ins.ID].Code.c_str(), dyn_ins.TID);
    return;
  }
  
  needed.erase(I(dyn_ins.TID, dyn_ins.ID));
  printf("The last %d-th execution of\n  instruction %d, %s\n  from thread %lu is depended on:\n",
    dyn_ins.Cnt, dyn_ins.ID, Ins[dyn_ins.ID].Code.c_str(), dyn_ins.TID);

  // SSA dependencies
  for (auto dep: Ins[dyn_ins.ID].SSADependencies) {
    if (dep.first == InstInfo::Inst) {
      printf("  * the last execution of\n\tinstruction %d, %s\n\tfrom thread %lu\n",
        dep.second, Ins[dep.second].Code.c_str(), dyn_ins.TID);
      needed.insert(I(dyn_ins.TID, dep.second));
    } else if (dep.first == InstInfo::Arg || dep.first == InstInfo::PointerArg) {
      // TODO
    }
  }
  
  // Memory dependencies
  for (auto dep: MemDependencies[dyn_ins]) {
    mem_depended.insert(dep);
    printf("  * the last %d-th execution of\n\tinstruction %d, %s\n\tfrom thread %lu\n",
      dep.Cnt, dep.ID, Ins[dep.ID].Code.c_str(), dep.TID);
  }
  
  // Phi dependencies
  // TODO
}

bool isReturnVoid(string code) {
  size_t i = 0;
  while (i < code.size() && isspace(code[i])) ++i;
  return code.substr(i, 8) == "ret void";
}

void ExtractUneededOperation(char *merged_trace_file_name, char *output_file_name) {
  boost::iostreams::mapped_file_source trace(merged_trace_file_name);
  auto data = trace.data();

  set<pair<uint64_t, uint32_t> > needed;
  set<DynamicInst> mem_depended;
  map<pair<uint64_t, uint32_t>, int32_t> InstCount;
  map<uint64_t, stack<bool> > fun_used;
  map<uint64_t, stack<pair<uint32_t, bool> > > bb_used;

  for (int64_t cur = trace.size(); cur > 0;) {
    SmallestBlock b; b.ReadBack(data, cur);

    if (b.IsLast > 0) {
      fun_used[b.TID].push(false);
      bb_used[b.TID].push(make_pair(b.BBID, false));
    }
    bool this_bb_used = false;

    if (b.Type == SmallestBlock::ImpactfulCallBlock) {
      DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][b.Start], -InstCount[I(b.TID, BB2Ins[b.BBID][b.Start])]);
      OneInstruction(true, dyn_ins, needed, mem_depended);
      InstCount[I(dyn_ins.TID, dyn_ins.ID)]++;

      fun_used[b.TID].top() = true;
      this_bb_used = true;
    } else if (b.Type == SmallestBlock::MemoryAccessBlock || b.Type == SmallestBlock::ExternalCallBlock) {
      DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][b.Start], -InstCount[I(b.TID, BB2Ins[b.BBID][b.Start])]);
      bool is_needed = ((needed.count(I(dyn_ins.TID, dyn_ins.ID)) > 0) || (mem_depended.count(dyn_ins) > 0));
      OneInstruction(is_needed , dyn_ins, needed, mem_depended);
      mem_depended.erase(dyn_ins);
      InstCount[I(dyn_ins.TID, dyn_ins.ID)]++;

      fun_used[b.TID].top() |= is_needed;
      this_bb_used |= is_needed;
    } else if (b.Type == SmallestBlock::NormalBlock) {
      for (int _ = b.End - 1; _ >= (int)b.Start; --_) {
        DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][_], -InstCount[I(b.TID, BB2Ins[b.BBID][_])]);
        bool is_needed = (needed.count(I(dyn_ins.TID, dyn_ins.ID)) > 0);

        if (Ins[dyn_ins.ID].Type == InstInfo::TerminatorInst) {
          is_needed |= bb_used[b.TID].top().second;
        } else if (Ins[dyn_ins.ID].Type == InstInfo::ReturnInst) {
          if (isReturnVoid(Ins[dyn_ins.ID].Code)) continue;

          assert(b.IsLast == 1 || b.IsLast == 2);
          if (b.IsLast == 2) {
            is_needed = true; // The return value of the last function
          } else if (b.IsLast == 1) {
            is_needed |= (needed.count(I(dyn_ins.TID, b.Caller)) > 0);
          }
        }

        OneInstruction(is_needed, dyn_ins, needed, mem_depended);
        InstCount[I(dyn_ins.TID, dyn_ins.ID)]++;
        fun_used[b.TID].top() |= is_needed;
        this_bb_used |= is_needed;
      }
    }
    if (bb_used[b.TID].top().first != b.BBID) {
      bb_used[b.TID].top() = make_pair(b.BBID, this_bb_used);
    } else {
      bb_used[b.TID].top().second |= this_bb_used;
    }

    if (b.IsFirst > 0) {
      if (b.IsFirst == 1 && fun_used[b.TID].top()) {
        needed.insert(I(b.TID, b.Caller));
        printf("Insert a CallInst <%lu, %u>\n", b.TID, b.Caller);
      }
      fun_used[b.TID].pop();
      bb_used[b.TID].pop();
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 4 && argc != 5) {
    printf("Usage: extract-uneeded-operation inst-file mem-dependencies merged-trace-file [output-file]\n");
    exit(1);
  }
  LoadInstInfo(argv[1], Ins, BB2Ins);
  LoadMemDependency(argv[2], MemDependencies);
  if (argc == 4) {
    ExtractUneededOperation(argv[3], "SlimmerUneededOperation");
  } else {
    ExtractUneededOperation(argv[3], argv[4]);
  }
}