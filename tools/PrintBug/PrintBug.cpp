#include <fstream>
#include <stack>
#include "SlimmerTools.h"

// Map an instruction ID to its instruction infomation
vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
vector<vector<uint32_t> > BB2Ins;

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

void PrintBug(char *unneeded_file_name) {
  TraceIter iter(unneeded_file_name);
  uint32_t cnt, edge_cnt = 0, vertex_cnt = 0; int32_t a, b;
  while (iter.Next(&a, sizeof(a))) {
    InsCnt[a] += 1;

    iter.Next(&cnt, sizeof(cnt));
    while (cnt--) {
      iter.Next(&b, sizeof(b));
      UneededGraph[a].insert(b);
      UneededGraph[b].insert(a);
      edge_cnt++;
    }
    ++vertex_cnt;
    if (vertex_cnt % 10000 == 0) {
      printf("Read %u %u\n", vertex_cnt, edge_cnt);
      fflush(stdout);
    }
    if (vertex_cnt >= 4610000) break;
  }
  printf("Read done %u %u\n", vertex_cnt, edge_cnt);
  fflush(stdout);

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

int main(int argc, char *argv[]) {
  // if (argc != 5 && argc != 6) {
  //   printf("Usage: extract-uneeded-operation inst-file bbgraph-file "
  //          "mem-dependencies merged-trace-file [output-file]\n");
  //   exit(1);
  // }
  LoadInstInfo(argv[1], Ins, BB2Ins);
  PrintBug(argv[2]);
}