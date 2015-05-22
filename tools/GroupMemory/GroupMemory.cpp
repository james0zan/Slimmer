#include "SlimmerUtil.h"

#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

namespace boost {
void throw_exception(std::exception const& e) {}
}

SegmentTree *Addr2Group;
map<uint32_t, SegmentTree *> Group2Addr;

void AddAddrToGroup(uint32_t group_id, uint64_t l, uint64_t r) {
  if (Group2Addr.find(group_id) == Group2Addr.end()) {
    Group2Addr[group_id] = SegmentTree::NewTree();
  }
  Group2Addr[group_id]->Set(l, r, 1);
  Addr2Group->Set(l, r, group_id);
}

void GroupMemory(char *trace_file_name) {
  Addr2Group = SegmentTree::NewTree();
  Group2Addr.clear();
  int MaxGroupID = 0;

  boost::iostreams::mapped_file_source trace(trace_file_name);
  auto data = trace.data();

  char event_label;
  const uint64_t *tid, *length;
  const uint32_t *id;
  const void **addr;
  
  for (int64_t cur = trace.size() - 1; cur > 0; --cur) {
    event_label = data[cur];
    switch (event_label) {
      case BasicBlockEventLabel:
        cur -= 32; id = (const uint32_t *)(&data[cur]);
        cur -= 64; tid = (const uint64_t *)(&data[cur]);
        printf("BasicBlockEvent: %lu\t%u\n", *tid, *id);
        break;
      case MemoryEventLabel:
        cur -= 64;  length = (const uint64_t *)(&data[cur]);
        cur -= size_of_ptr; addr = (const void **)(&data[cur]);
        cur -= 32; id = (const uint32_t *)(&data[cur]);
        cur -= 64; tid = (const uint64_t *)(&data[cur]);
        printf("MemoryEvent:     %lu\t%u\t%p\t%lu\n", *tid, *id, *addr, *length);

        int group_id = 0;
        auto segments = Addr2Group->Collect((uint64_t)(*addr), (uint64_t)(*addr) + (*length));
        for (auto i: segments) group_id = max(group_id, get<0>(i));
        if (group_id == 0) group_id = (++MaxGroupID);
        
        for (auto i: segments) {
          int origin_id = get<0>(i);
          if (origin_id == 0) {
            AddAddrToGroup(group_id, get<1>(i), get<2>(i));
          } else if (origin_id != group_id) {
            auto iter = Group2Addr.find(origin_id);
            if (iter == Group2Addr.end()) continue;
            
            for (auto &j: iter->second->Collect(0, SegmentTree::MAX_RANGE)) {
              if (get<0>(j) == 0) continue;
              AddAddrToGroup(group_id, get<1>(j), get<2>(j));
            }

            delete iter->second; iter->second = NULL;
            Group2Addr.erase(iter);
          }
        }
        break;
    }
  }

  delete Addr2Group;
  for (auto& i: Group2Addr) {
    delete i.second;
  }
  Group2Addr.clear();
}

int main(int argc, char *argv[]) {
  GroupMemory(argv[1]);
}