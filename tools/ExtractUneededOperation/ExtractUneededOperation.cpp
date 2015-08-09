#include <fstream>
#include "SlimmerTools.h"

// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

/// Print BUG

map<int32_t, uint32_t> InsCnt;
map<int32_t, set<int32_t> > UneededGraph;
set<int32_t> Printed;

void BFSOnUneededGraph(int32_t id, set<int32_t>& bug) {
  if (Printed.count(id)) return;
  stack<int32_t> q;
  q.push(id);
  while (!q.empty()) {
    id = q.top(); q.pop();
    bug.insert(id); Printed.insert(id);
    for (auto i: UneededGraph[id]) {
      if (Printed.count(i) == 0)
        q.push(i);
    }
  }
}

map<string, vector<string> > CodeCahe;
string GetCode(string path, size_t loc) {
  if (CodeCahe.count(path) == 0) {
    ifstream in(path);
    string str; vector<string> c;
    while (in.good() && !in.eof()) {
      getline(in, str);
      c.push_back(str);
    }
    CodeCahe[path] = c;
  }
  if (loc <= 0 || CodeCahe[path].size() < loc) return "";
  else return CodeCahe[path][loc - 1];
}

void PrintBug() {
  int bug_cnt = 1;
  for (auto i: InsCnt) {
    if (!Printed.count(i.first)) {
      set<int32_t> bug;
      BFSOnUneededGraph(i.first, bug);
      
      printf("===============\nBug %d\n===============\n", bug_cnt++);
      printf("\n------IR------\n");
      for (auto j: bug) {
        printf("(%4d)\t%d:\t%s\n", InsCnt[j], j, Ins[j].Code.c_str());
      }
      printf("\n------Related Code------\n");
      map<string, set<int> > used_code;
      map<pair<string, int>, int> code_cnt;
      for (auto j: bug) {
        if (Ins[j].File == "[UNKNOWN]") continue;

        for (int i = -3; i <= 3; ++i)
          used_code[Ins[j].File].insert(Ins[j].LoC + i);
        code_cnt[make_pair(Ins[j].File, Ins[j].LoC)] += 1;
      }
      for (auto& i: used_code) {
        printf("\n%s\n", i.first.c_str());
        int last_line = -1;
        for (auto l: i.second) {
          if (l < 0) continue;
          if (last_line != l - 1) {
            printf("\n");
          }

          string code = GetCode(i.first, l);
          if (code != "") {
            if (code_cnt[make_pair(i.first, l)] > 0)
              printf("(%4d)\t%d:\t%s\n", code_cnt[make_pair(i.first, l)], l, code.c_str());
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

/// Load the memory dependency infomation generated by extract-memory-dependency
/// tool.
///
/// \param mem_dependencies_file_name - path to the generated file of
/// extract-memory-dependency tool.
/// \param mem_dependencies - the memory dependencies extracted by
/// extract-memory-dependency tool.
///
map<DynamicInst, vector<DynamicInst> > MemDependencies;
void
LoadMemDependency(char *mem_dependencies_file_name,
                  map<DynamicInst, vector<DynamicInst> > &mem_dependencies) {
  // FILE *f = fopen(mem_dependencies_file_name, "r");
  map<DynamicInst, vector<DynamicInst> > tmp;
  TraceIter iter(mem_dependencies_file_name);
  DynamicInst a, b;
  while (true) {
    iter.Next(&a, sizeof(a));
    iter.Next(&b, sizeof(b));
    // fscanf(f, "%lu%d%d%lu%d%d", &a.TID, &a.ID, &a.Cnt, &b.TID, &b.ID,
    // &b.Cnt);
    if (a.TID == 0 && a.ID == -1 && a.Cnt == -1)
      break;
    tmp[a].push_back(b);
  }
  uint64_t tid;
  int32_t id, cnt;
  map<pair<uint64_t, uint32_t>, uint32_t> count;
  // while (fscanf(f, "%lu%d%d", &tid, &id, &cnt) != EOF) {
  while (iter.Next(&tid, sizeof(tid))) {
    iter.Next(&id, sizeof(id));
    iter.Next(&cnt, sizeof(cnt));
    count[I(tid, id)] = cnt - 1;
  }

  for (auto &i : tmp) {
    DynamicInst tmp_a = i.first;
    tmp_a.Cnt -= count[I(i.first.TID, i.first.ID)];
    // printf("The last %d-th execution of\n  instruction %d, %s\n  from thread
    // %lu is depended on:\n",
    //       tmp_a.Cnt, tmp_a.ID, Ins[tmp_a.ID].Code.c_str(), tmp_a.TID);
    for (auto &j : i.second) {
      DynamicInst tmp_b = j;
      tmp_b.Cnt -= count[I(j.TID, j.ID)];

      // printf("  * the last  %d-th execution of\n    instruction %d, %s\n
      // from thread %lu is depended on\n",
      //   tmp_b.Cnt, tmp_b.ID, Ins[tmp_b.ID].Code.c_str(), tmp_b.TID);
      mem_dependencies[tmp_a].push_back(tmp_b);
    }
  }
  // puts("==============");
}

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

map<uint32_t, set<uint32_t> > PostDominator;
/// Prepare the post dominator infomation from the calling graph of basic
/// blocks.
///
/// \param bbgraph_file_name - path to the calling graph of basic blocks.
/// \param post_dominator - for recording post dominator infomation.
///
void PreparePostDominator(char *bbgraph_file_name,
                          map<uint32_t, set<uint32_t> > &post_dominator) {
  post_dominator.clear();

  FILE *f = fopen(bbgraph_file_name, "r");
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

/// Proccessing the dependencies of one instruction .
///
/// \param is_needed - this instruction is needed or not?
/// \param dyn_ins - the dynamic instruction.
/// \param last_bb_id - ID of the last executed basic block.
/// \param needed - for recording all the needed but not yet proccessed dynamic
/// instructions
/// \param mem_depended - the memory dependencies extracted by
/// extract-memory-dependency tool.
///
void OneInstruction(bool is_needed, DynamicInst dyn_ins, int32_t last_bb_id,
                    set<pair<uint64_t, uint32_t> > &needed,
                    set<DynamicInst> &mem_depended,
                    // CompressBuffer& uneeded_graph,
                    map<DynamicInst, set<DynamicInst> >& unneeded_mem_dep,
                    map<pair<uint64_t, uint32_t>, set<DynamicInst> >& unneeded_dep, 
                    map<uint64_t, stack<set<DynamicInst> > >& bb_unused) {
  if (!is_needed) {
    // printf("!!!The last %d-th execution of\n  instruction %d, %s\n  from "
    //        "thread %lu is uneeded.\n",
    //        dyn_ins.Cnt, dyn_ins.ID, Ins[dyn_ins.ID].Code.c_str(), dyn_ins.TID);
    // return;
    bb_unused[dyn_ins.TID].top().insert(dyn_ins);

    // uneeded_graph.Append((void *)&(dyn_ins.ID), sizeof(dyn_ins.ID));
    // set<DynamicInst> _tmp(unneeded_dep[I(dyn_ins.TID, dyn_ins.ID)]);
    // _tmp.insert(unneeded_mem_dep[dyn_ins].begin(), unneeded_mem_dep[dyn_ins].end());
    // uint32_t cnt = _tmp.size();
    // uneeded_graph.Append((void *)&cnt, sizeof(cnt));
    // for (auto& i: _tmp) {
    //   uneeded_graph.Append((void *)&(i.ID), sizeof(i.ID));
    // }
    
    InsCnt[dyn_ins.ID]++;
    for (auto& i: unneeded_dep[I(dyn_ins.TID, dyn_ins.ID)]) {
      UneededGraph[dyn_ins.ID].insert(i.ID);
      UneededGraph[i.ID].insert(dyn_ins.ID);
    }
    for (auto& i: unneeded_mem_dep[dyn_ins]) {
      UneededGraph[dyn_ins.ID].insert(i.ID);
      UneededGraph[i.ID].insert(dyn_ins.ID);
    }
  }

  needed.erase(I(dyn_ins.TID, dyn_ins.ID));
  mem_depended.erase(dyn_ins);
  unneeded_dep.erase(I(dyn_ins.TID, dyn_ins.ID));
  unneeded_mem_dep.erase(dyn_ins);

  // printf("The last %d-th execution of\n  instruction %d, %s\n  from thread
  // %lu is depended on:\n",
  //   dyn_ins.Cnt, dyn_ins.ID, Ins[dyn_ins.ID].Code.c_str(), dyn_ins.TID);

  // SSA dependencies
  for (auto dep : Ins[dyn_ins.ID].SSADependencies) {
    if (dep.first == InstInfo::Inst) {
      // printf("  * the last execution of\n\tinstruction %d, %s\n\tfrom thread
      // %lu\n",
      //   dep.second, Ins[dep.second].Code.c_str(), dyn_ins.TID);
      if (is_needed) {
        needed.insert(I(dyn_ins.TID, dep.second));
      } else {
        unneeded_dep[I(dyn_ins.TID, dep.second)].insert(dyn_ins);
      }
    } else if (dep.first == InstInfo::Arg ||
               dep.first == InstInfo::PointerArg) {
      // TODO
    }
  }

  // Memory dependencies
  for (auto dep : MemDependencies[dyn_ins]) {
    if (is_needed) {
      mem_depended.insert(dep);
    } else {
      unneeded_mem_dep[dep].insert(dyn_ins);
    }
    // printf("  * the last %d-th execution of\n\tinstruction %d, %s\n\tfrom
    // thread %lu\n",
    //   dep.Cnt, dep.ID, Ins[dep.ID].Code.c_str(), dep.TID);
  }

  // Phi dependencies
  for (auto phi_dep : Ins[dyn_ins.ID].PhiDependencies) {
    if ((int32_t)get<0>(phi_dep) == last_bb_id) {
      if (get<1>(phi_dep) == InstInfo::Inst) {
        // printf("  * the last execution of\n\tinstruction %d, %s\n\tfrom
        // thread %lu\n",
        //   get<2>(phi_dep), Ins[get<2>(phi_dep)].Code.c_str(), dyn_ins.TID);
        if (is_needed) {
          needed.insert(I(dyn_ins.TID, get<2>(phi_dep)));
        } else {
          unneeded_dep[I(dyn_ins.TID, get<2>(phi_dep))].insert(dyn_ins);
        }
      } else if (get<1>(phi_dep) == InstInfo::Arg ||
                 get<1>(phi_dep) == InstInfo::PointerArg) {
        // TODO
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

/// Extract the unneeded opertions in instruction-level.
///
/// \param merged_trace_file_name - the merged trace outputed by the merge-trace
/// tool.
/// \param output_file_name - the path to output file.
///
void ExtractUneededOperation(char *merged_trace_file_name,
                             char *output_file_name) {
  set<pair<uint64_t, uint32_t> > needed;
  set<DynamicInst> mem_depended;
  map<pair<uint64_t, uint32_t>, int32_t> InstCount;
  map<uint64_t, stack<bool> > fun_used;
  map<uint64_t, stack<pair<uint32_t, bool> > > bb_used;
  map<uint64_t, stack<set<DynamicInst> > > bb_unused;

  // CompressBuffer uneeded_graph("/scratch1/zhangmx/SlimmerUneeded");
  map<DynamicInst, set<DynamicInst> > unneeded_mem_dep;
  map<pair<uint64_t, uint32_t>, set<DynamicInst> > unneeded_dep;

  BackwardTraceIter iter(merged_trace_file_name);
  SmallestBlock b;
  while (iter.FormerSmallestBlock(b)) {
    // b.Print(Ins, BB2Ins);

    if (b.IsLast > 0) {
      fun_used[b.TID].push(false);
      bb_used[b.TID].push(make_pair(b.BBID, false));
      bb_unused[b.TID].push(set<DynamicInst>());
    }
    bool this_bb_used = false;

    if (b.Type == SmallestBlock::ImpactfulCallBlock) {
      DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][b.Start],
                          -InstCount[I(b.TID, BB2Ins[b.BBID][b.Start])]);
      OneInstruction(true, dyn_ins, b.LastBBID, needed, mem_depended,
        /*uneeded_graph,*/ unneeded_mem_dep, unneeded_dep, bb_unused);
      InstCount[I(dyn_ins.TID, dyn_ins.ID)]++;

      fun_used[b.TID].top() = true;
      this_bb_used = true;
    } else if (b.Type == SmallestBlock::MemoryAccessBlock ||
               b.Type == SmallestBlock::ExternalCallBlock ||
               b.Type == SmallestBlock::MemsetBlock ||
               b.Type == SmallestBlock::MemmoveBlock) {
      DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][b.Start],
                          -InstCount[I(b.TID, BB2Ins[b.BBID][b.Start])]);
      bool is_needed = ((needed.count(I(dyn_ins.TID, dyn_ins.ID)) > 0) ||
                        (mem_depended.count(dyn_ins) > 0));
      OneInstruction(is_needed, dyn_ins, b.LastBBID, needed, mem_depended,
        /*uneeded_graph,*/ unneeded_mem_dep, unneeded_dep, bb_unused);
      InstCount[I(dyn_ins.TID, dyn_ins.ID)]++;

      fun_used[b.TID].top() |= is_needed;
      this_bb_used |= is_needed;
    } else if (b.Type == SmallestBlock::NormalBlock) {
      for (int _ = b.End - 1; _ >= (int)b.Start; --_) {
        DynamicInst dyn_ins(b.TID, BB2Ins[b.BBID][_],
                            -InstCount[I(b.TID, BB2Ins[b.BBID][_])]);
        bool is_needed = (needed.count(I(dyn_ins.TID, dyn_ins.ID)) > 0);

        if (Ins[dyn_ins.ID].Type == InstInfo::TerminatorInst) {
          if (PostDominator[b.BBID].count(bb_used[b.TID].top().first))
            continue;

          is_needed |= bb_used[b.TID].top().second;

          if (!is_needed) {
            // uneeded_graph.Append((void *)&(dyn_ins.ID), sizeof(dyn_ins.ID));
            // uint32_t cnt = bb_unused[b.TID].top().size();
            // uneeded_graph.Append((void *)&cnt, sizeof(cnt));
            // for (auto& i: bb_unused[b.TID].top()) {
            //   uneeded_graph.Append((void *)&(i.ID), sizeof(i.ID));
            // }

            InsCnt[dyn_ins.ID]++;
            for (auto& i: bb_unused[b.TID].top()) {
              UneededGraph[dyn_ins.ID].insert(i.ID);
              UneededGraph[i.ID].insert(dyn_ins.ID);
            }
          }
        } else if (Ins[dyn_ins.ID].Type == InstInfo::ReturnInst) {
          if (isReturnVoid(Ins[dyn_ins.ID].Code))
            continue;

          assert(b.IsLast == 1 || b.IsLast == 2);
          if (b.IsLast == 2) {
            is_needed = true; // The return value of the last function
          } else if (b.IsLast == 1) {
            is_needed |= (needed.count(I(dyn_ins.TID, b.Caller)) > 0);
          }
        }

        OneInstruction(is_needed, dyn_ins, b.LastBBID, needed, mem_depended,
          /*uneeded_graph,*/ unneeded_mem_dep, unneeded_dep, bb_unused);
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
      }
      // printf("Pop %lu %lu\n", b.TID, fun_used[b.TID].size());
      fun_used[b.TID].pop();
      bb_used[b.TID].pop();
      bb_unused[b.TID].pop();
    }
  }
  PrintBug();
}

int main(int argc, char *argv[]) {
  if (argc != 5 && argc != 6) {
    printf("Usage: extract-uneeded-operation inst-file bbgraph-file "
           "mem-dependencies merged-trace-file [output-file]\n");
    exit(1);
  }
  LoadInstInfo(argv[1], Ins, BB2Ins);

  PreparePostDominator(argv[2], PostDominator);
  LoadMemDependency(argv[3], MemDependencies);

  if (argc == 5) {
    ExtractUneededOperation(argv[4], "SlimmerUneededOperation");
  } else {
    ExtractUneededOperation(argv[4], argv[5]);
  }
}
