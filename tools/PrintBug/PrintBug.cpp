#include <fstream>
#include <stack>

#include "SlimmerTools.h"

//===----------------------------------------------------------------------===//
//                        Common
//===----------------------------------------------------------------------===//

// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;
// A set of function calls that impact the outside enviroment.
set<uint64_t> ImpactfulFunCall;

vector<SmallestBlock> BlockTrace;

// A segment tree that maps a memory address to its group
SegmentTree<int> *Addr2Group;
// For each group, we use a segment tree to record all the memory addresses that
// belong to it
map<uint32_t, SegmentTree<int> *> Group2Addr;

// Dynamic instruction to its memory dependencies.
map<DynamicInst, vector<DynamicInst> > MemDependencies;
// Basic block ID to its post dominators.
map<uint32_t, set<uint32_t> > PostDominator;
// Address to uneeded instructions related to this address.
map<uint64_t, set<uint32_t> > Addr2Unneded;

//===----------------------------------------------------------------------===//
//                        PreparePostDominator
//===----------------------------------------------------------------------===//

/// A simple DFS function for figuring out which basic block can be reached from
/// a basic block.
void DFSOnBBGraph(uint32_t bb_id, set<uint32_t> &mark,
                  map<uint32_t, vector<uint32_t> > &successor) {
  mark.insert(bb_id);
  for (auto i : successor[bb_id]) {
    if (mark.count(i) == 0)
      DFSOnBBGraph(i, mark, successor);
  }
}

/// Set a := set a intersect set b.
void SetIntersection(set<uint32_t> &a, set<uint32_t> &b) {
  set<uint32_t> c;
  for (auto i : a) {
    if (b.count(i))
      c.insert(i);
  }
  a = c;
}

/// Prepare the post dominator infomation from the calling graph of basic
/// blocks.
///
/// \param bbgraph_file_name - path to the calling graph of basic blocks.
/// \param post_dominator - for recording post dominator infomation.
///
void PreparePostDominator(string bbgraph_file_name,
                          map<uint32_t, set<uint32_t> > &post_dominator) {
  post_dominator.clear();

  FILE *f = fopen(bbgraph_file_name.c_str(), "r");
  map<uint32_t, vector<uint32_t> > successor;
  int a, b;
  while (fscanf(f, "%d%d", &a, &b) != EOF) {
    successor[a].push_back(b);
  }

  for (auto i : successor) {
    DFSOnBBGraph(i.first, post_dominator[i.first], successor);
  }

  // Keep doing intersection if the post dominator infomation is changed.
  bool changed = true;
  while (changed) {
    changed = false;

    set<uint32_t> res;
    for (auto i : successor) {
      if (i.second.size() > 0)
        res = post_dominator[i.first];
      for (auto j : i.second) {
        SetIntersection(res, post_dominator[j]);
      }
      // if (i.second.size() == 1) res.insert(i.second[0]); // TODO

      if (res.size() != post_dominator[i.first].size()) {
        post_dominator[i.first] = res;
        changed = true;
      }
    }
  }
}

//===----------------------------------------------------------------------===//
//                        ExtractUneededOperation
//===----------------------------------------------------------------------===//

/// Proccessing the dependencies of one needed instruction.
///
/// \param dyn_ins - the dynamic instruction.
/// \param last_bb_id - ID of the last executed basic block.
/// \param needed - for recording all the needed but not yet proccessed dynamic
/// instructions
/// \param mem_depended - the memory dependencies extracted by
/// extract-memory-dependency tool.
///
void OneInstruction(DynamicInst dyn_ins, int32_t last_bb_id,
                    set<pair<uint64_t, uint32_t> > &needed,
                    set<DynamicInst> &mem_depended) {
  needed.erase(I(dyn_ins.TID, dyn_ins.ID));
  mem_depended.erase(dyn_ins);

  // SSA dependencies
  for (auto dep : Ins[dyn_ins.ID].SSADependencies) {
    if (dep.first == InstInfo::Inst) {
      needed.insert(I(dyn_ins.TID, dep.second));
    }
  }

  // Memory dependencies
  for (auto dep : MemDependencies[dyn_ins]) {
    mem_depended.insert(dep);
  }

  // Phi dependencies
  for (auto phi_dep : Ins[dyn_ins.ID].PhiDependencies) {
    if ((int32_t)get<0>(phi_dep) == last_bb_id) {
      if (get<1>(phi_dep) == InstInfo::Inst) {
        needed.insert(I(dyn_ins.TID, get<2>(phi_dep)));
      }
      break;
    }
  }
}

/// Return true if it is a return void instruction.
///
/// \param code - the LLVM IR of the code.
/// \return - return if it is a return void instruction.
///
bool isReturnVoid(string code) {
  size_t i = 0;
  while (i < code.size() && isspace(code[i]))
    ++i;
  return code.substr(i, 8) == "ret void";
}

/// Proccessing a basic block assuming that its terminator is used.
void OneBlock(DynamicInst dyn_ins, SmallestBlock &b, int next_bb_id,
              set<DynamicInst> &mem_depended, set<DynamicInst> &unneeded_di,
              map<pair<uint64_t, uint32_t>, int32_t> &inst_count,
              set<pair<uint64_t, uint32_t> > &needed) {

  int cnt_diff = dyn_ins.Cnt + inst_count[I(b.TID, dyn_ins.ID)];
  set<pair<uint64_t, uint32_t> > this_needed;
  OneInstruction(dyn_ins, b.LastBBID, this_needed, mem_depended);
  unneeded_di.erase(dyn_ins);

  for (int _ = BB2Ins[next_bb_id].size() - 1; _ >= 0; --_) {
    DynamicInst dyn_ins2(b.TID, BB2Ins[next_bb_id][_],
                         cnt_diff -
                             inst_count[I(b.TID, BB2Ins[next_bb_id][_])]);
    if (this_needed.count(I(dyn_ins2.TID, dyn_ins2.ID)) > 0 ||
        mem_depended.count(dyn_ins2)) {
      OneInstruction(dyn_ins2, b.LastBBID, this_needed, mem_depended);
      unneeded_di.erase(dyn_ins2);
    }
  }
  needed.insert(this_needed.begin(), this_needed.end());
}

/// Extract the unneeded opertions in instruction-level.
///
/// \param merged_trace_file_name - the merged trace outputed by the merge-trace
/// tool.
/// \param output_file_name - the path to output file.
///
void ExtractUneededOperation(vector<SmallestBlock> &block_trace,
                             set<DynamicInst> &unneeded_di) {
  map<pair<uint64_t, uint32_t>, int32_t> inst_count;
  set<pair<uint64_t, uint32_t> > needed;
  set<DynamicInst> mem_depended;
  map<uint64_t, stack<bool> > fun_used;
  map<uint64_t, stack<pair<uint32_t, bool> > > next_bb_used;
  map<uint64_t, stack<tuple<uint32_t, DynamicInst, bool> > > terminator_stack;

  unneeded_di.clear();
  for (int64_t i = block_trace.size() - 1; i >= 0; --i) {
    SmallestBlock b = block_trace[i];

    if (b.IsLast > 0) {
      fun_used[b.TID].push(false);
      next_bb_used[b.TID].push(make_pair(b.BBID, false));
      terminator_stack[b.TID]
          .push(make_tuple(b.BBID, DynamicInst(0, 0, 0), true));
    } else if (next_bb_used[b.TID].size() > 0 &&
               next_bb_used[b.TID].top().first != b.BBID &&
               next_bb_used[b.TID].top().second) {
      // If a BB is just finished and it is used, its terminator is also used
      int next_bb_id = next_bb_used[b.TID].top().first;
      auto t = terminator_stack[b.TID].top();
      if (get<2>(t) == false) {
        OneBlock(get<1>(t), b, next_bb_id, mem_depended, unneeded_di,
                 inst_count, needed);
      }
      terminator_stack[b.TID].top() =
          make_tuple(b.BBID, DynamicInst(0, 0, 0), true);
    }

    bool this_bb_used = false;

    if (b.Type == SmallestBlock::ImpactfulCallBlock) {
      // b.Print(Ins, BB2Ins);
      DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][b.Start],
                          -inst_count[I(b.TID, BB2Ins[b.BBID][b.Start])]);
      OneInstruction(dyn_ins, b.LastBBID, needed, mem_depended);
      inst_count[I(dyn_ins.TID, dyn_ins.ID)]++;

      fun_used[b.TID].top() = true;
      this_bb_used = true;
    } else if (b.Type == SmallestBlock::MemoryAccessBlock ||
               b.Type == SmallestBlock::ExternalCallBlock ||
               b.Type == SmallestBlock::MemsetBlock ||
               b.Type == SmallestBlock::MemmoveBlock) {

      if (b.Type == SmallestBlock::ExternalCallBlock) {
        if (Ins[BB2Ins[b.BBID][b.Start]].Fun == "free")
          continue;
        if (Ins[BB2Ins[b.BBID][b.Start]].Fun == "va_start")
          continue;
        if (Ins[BB2Ins[b.BBID][b.Start]].Fun == "va_end")
          continue;
      }

      DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][b.Start],
                          -inst_count[I(b.TID, BB2Ins[b.BBID][b.Start])]);
      bool is_needed = false;
      if ((needed.count(I(dyn_ins.TID, dyn_ins.ID)) > 0) ||
          (mem_depended.count(dyn_ins) > 0)) {
        is_needed = true;
        OneInstruction(dyn_ins, b.LastBBID, needed, mem_depended);
      } else {
        if (b.Type == SmallestBlock::MemoryAccessBlock &&
            b.Addr[0] < b.Addr[1]) {
          Addr2Unneded[b.Addr[0]].insert(dyn_ins.ID);
        }
        unneeded_di.insert(dyn_ins);
      }
      inst_count[I(dyn_ins.TID, dyn_ins.ID)]++;

      fun_used[b.TID].top() |= is_needed;
      this_bb_used |= is_needed;
    } else if (b.Type == SmallestBlock::NormalBlock) {
      for (int _ = b.End - 1; _ >= (int)b.Start; --_) {
        DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][_],
                            -inst_count[I(b.TID, BB2Ins[b.BBID][_])]);
        bool is_needed = (needed.count(I(dyn_ins.TID, dyn_ins.ID)) > 0);

        if (Ins[dyn_ins.ID].Type == InstInfo::TerminatorInst) {
          // If the next bb is used and it is not a PostDominator of
          // the current bb, the terminator is used.
          if (!PostDominator[b.BBID].count(next_bb_used[b.TID].top().first))
            is_needed |= next_bb_used[b.TID].top().second;

          terminator_stack[b.TID].top() =
              make_tuple(b.BBID, dyn_ins, is_needed);
        } else if (Ins[dyn_ins.ID].Type == InstInfo::ReturnInst) {
          if (isReturnVoid(Ins[dyn_ins.ID].Code))
            is_needed = true;
          else {
            assert(b.IsLast == 1 || b.IsLast == 2);
            if (b.IsLast == 2) {
              is_needed = true; // The return value of the last function
            } else if (b.IsLast == 1) {
              is_needed |= (needed.count(I(dyn_ins.TID, b.Caller)) > 0);
            }
          }
        }

        if (is_needed) {
          OneInstruction(dyn_ins, b.LastBBID, needed, mem_depended);
        } else {
          // Only one successor
          if (Ins[dyn_ins.ID].Type == InstInfo::TerminatorInst &&
              Ins[dyn_ins.ID].Successors.size() <= 1) {
          } else
            unneeded_di.insert(dyn_ins);
        }
        inst_count[I(dyn_ins.TID, dyn_ins.ID)]++;
        fun_used[b.TID].top() |= is_needed;
        this_bb_used |= is_needed;
      }
    }

    if (next_bb_used[b.TID].top().first != b.BBID) {
      next_bb_used[b.TID].top() = make_pair(b.BBID, this_bb_used);
    } else {
      next_bb_used[b.TID].top().second |= this_bb_used;
    }

    if (b.IsFirst > 0) {
      if (b.IsFirst == 1 && fun_used[b.TID].top() &&
          b.Caller != (uint32_t) - 1) {
        needed.insert(I(b.TID, b.Caller));
      }

      // If this bb is used, its terminator is used
      if (next_bb_used[b.TID].top().second) {
        auto t = terminator_stack[b.TID].top();
        if (get<2>(t) == false) {
          OneBlock(get<1>(t), b, b.BBID, mem_depended, unneeded_di, inst_count,
                   needed);
        }
      }

      fun_used[b.TID].pop();
      next_bb_used[b.TID].pop();
      terminator_stack[b.TID].pop();
    }
  }
}

//===----------------------------------------------------------------------===//
//                        PrintBug
//===----------------------------------------------------------------------===//

map<string, vector<string> > CodeCahe;
string GetCode(string path, size_t loc) {
  if (CodeCahe.count(path) == 0) {
    ifstream in(path);
    string str;
    vector<string> c;
    while (in.good() && !in.eof()) {
      getline(in, str);
      c.push_back(str);
    }
    CodeCahe[path] = c;
  }
  if (loc <= 0 || CodeCahe[path].size() < loc)
    return "";
  else
    return CodeCahe[path][loc - 1];
}

void BFSOnUneededGraph(map<uint32_t, set<uint32_t> > &graph, int32_t id,
                       set<int32_t> &bug, set<int32_t> &printed) {

  if (printed.count(id))
    return;
  stack<int32_t> q;
  q.push(id);
  while (!q.empty()) {
    id = q.top();
    q.pop();
    bug.insert(id);
    printed.insert(id);
    for (auto i : graph[id]) {
      if (printed.count(i) == 0)
        q.push(i);
    }
  }
}

void PrintBug(set<DynamicInst> &bug) {
  map<uint32_t, uint32_t> uneeded_ins_cnt;
  for (auto i : bug)
    uneeded_ins_cnt[i.ID]++;

  map<uint32_t, set<uint32_t> > uneeded_graph;
  for (auto i : bug) {
    // SSA dependencies
    for (auto dep : Ins[i.ID].SSADependencies) {
      if (dep.first == InstInfo::Inst) {
        DynamicInst dyn_ins(i.TID, dep.second, i.Cnt);
        if (bug.count(dyn_ins)) {
          uneeded_graph[i.ID].insert(dep.second);
          uneeded_graph[dep.second].insert(i.ID);
        }
      }
    }
    // Memory dependencies
    for (auto dep : MemDependencies[i]) {
      if (bug.count(dep)) {
        uneeded_graph[i.ID].insert(dep.ID);
        uneeded_graph[dep.ID].insert(i.ID);
      }
    }
  }

  for (auto &i : Addr2Unneded) {
    uint32_t id = (*i.second.begin());
    for (auto j : i.second) {
      uneeded_graph[id].insert(j);
      uneeded_graph[j].insert(id);
    }
  }

  set<int32_t> printed;
  int bug_cnt = 1;
  for (auto i : uneeded_ins_cnt) {
    if (!printed.count(i.first)) {
      set<int32_t> bug;
      BFSOnUneededGraph(uneeded_graph, i.first, bug, printed);

      printf("\n===============\nBug %d\n===============\n", bug_cnt++);
      printf("\n------IR------\n");
      for (auto j : bug) {
        printf("(%4d)\t%d:\t%s\n", uneeded_ins_cnt[j], j, Ins[j].Code.c_str());
      }
      printf("\n------Related Code------\n");
      map<string, set<int> > used_code;
      map<pair<string, int>, int> code_cnt;
      for (auto j : bug) {
        if (Ins[j].File == "[UNKNOWN]")
          continue;

        for (int i = -3; i <= 3; ++i)
          used_code[Ins[j].File].insert(Ins[j].LoC + i);
        code_cnt[make_pair(Ins[j].File, Ins[j].LoC)] += uneeded_ins_cnt[j];
      }
      for (auto &i : used_code) {
        printf("\n%s\n", i.first.c_str());
        int last_line = -1;
        for (auto l : i.second) {
          if (l < 0)
            continue;
          if (last_line != l - 1) {
            printf("\n");
          }

          string code = GetCode(i.first, l);
          if (code != "") {
            if (code_cnt[make_pair(i.first, l)] > 0)
              printf("(%4d)\t%d:\t%s\n", code_cnt[make_pair(i.first, l)], l,
                     code.c_str());
            else
              printf("      \t%d:\t%s\n", l, code.c_str());
          }
          last_line = l;
        }
      }
      fflush(stdout);
    }
  }
}

//===----------------------------------------------------------------------===//
//                        Main
//===----------------------------------------------------------------------===//

int main(int argc, char *argv[]) {
  if (argc != 4) {
    printf("Usage: print-bug slimmer_dir slimmer_trace pin_trace\n");
    exit(1);
  }

  string slimmer_dir = argv[1];
  printf("LoadInstInfo\n");
  LoadInstInfo(slimmer_dir + "/Inst", Ins, BB2Ins);
  printf("ExtractImpactfulFunCall\n");
  ExtractImpactfulFunCall(argv[3], ImpactfulFunCall);
  printf("MergeTrace\n");
  MergeTrace(argv[2], ImpactfulFunCall, BlockTrace);

  Addr2Group = SegmentTree<int>::NewTree();
  Group2Addr.clear();
  printf("GroupMemory\n");
  GroupMemory(BlockTrace);
  printf("ExtractMemoryDependency\n");
  ExtractMemoryDependency(BlockTrace, MemDependencies);
  delete Addr2Group;
  for (auto &i : Group2Addr) {
    delete i.second;
  }
  Group2Addr.clear();

  printf("PreparePostDominator\n");
  PreparePostDominator(slimmer_dir + "/BBGraph", PostDominator);

  set<DynamicInst> bug;
  printf("ExtractUneededOperation\n");
  ExtractUneededOperation(BlockTrace, bug);
  printf("PrintBug\n");
  PrintBug(bug);
}
