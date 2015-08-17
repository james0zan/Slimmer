#ifndef SLIMMER_TOOLS_H
#define SLIMMER_TOOLS_H

#include "SlimmerUtil.h"
#include "SegmentTree.hpp"

#include <algorithm>
#include <stack>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

//===----------------------------------------------------------------------===//
//                        Common
//===----------------------------------------------------------------------===//

// Map an instruction ID to its instruction infomation
extern vector<InstInfo> Ins;
// Map a basic block ID to all the instructions that belong to it
extern vector<vector<uint32_t> > BB2Ins;
// A segment tree that maps a memory address to its group
extern SegmentTree<int> *Addr2Group;
// For each group, we use a segment tree to record all the memory addresses that
// belong to it
extern map<uint32_t, SegmentTree<int> *> Group2Addr;

//===----------------------------------------------------------------------===//
//                        Util
//===----------------------------------------------------------------------===//

namespace boost {
void throw_exception(std::exception const &e);
}

pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id);

/// A smallest block is a continuous part of a basic block
/// that will always be executed continuously.
///
struct SmallestBlock {
  enum SmallestBlockType {
    NormalBlock, // A continuous part of a basic block that contains no memory
                 // accessed
    MemoryAccessBlock,  // A single memory access
    ExternalCallBlock,  // A single external call that do not impact the outside
                        // enviroment
    ImpactfulCallBlock, // A single external call that impacts the outside
                        // enviroment
    MemsetBlock,        // A single memset
    MemmoveBlock,       // A single memset
    DeclareBlock
  } Type;

  uint64_t TID;
  uint32_t BBID, Start, End; // The instructions' ID of this SmallestBlock is
                             // blong to [Start, End).
  // If this is a MemoryAccessBlock:
  //    Addr[0] is the starting address of accessed memory;
  //    Addr[1] is the ending address of accessed memory.
  // If this is a ExternalCallBlock or ImpactfulCallBlock:
  //    Addr is the vector of pointer arguments.
  vector<uint64_t> Addr;

  // IsFirst = 0: Is a following SmallestBlock
  // IsFirst = 1: Is the first SmallestBlock of a called function
  // IsFirst = 2: Is the first SmallestBlock of a thread
  uint8_t IsFirst;
  // IsLast = 0: Is not the last SmallestBlock
  // IsLast = 1: Is the last SmallestBlock of a called function
  // IsLast = 2: Is the last SmallestBlock of a thread
  uint8_t IsLast;
  uint32_t Caller; // The instruction ID of the caller, valid when IsFirst == 1

  int32_t LastBBID; // The basic block ID of the last executed basic block

  SmallestBlock() {}
  SmallestBlock(SmallestBlockType t, uint64_t tid, uint32_t bb_id,
                uint32_t start, uint32_t end, pair<uint8_t, uint32_t> first,
                int32_t last_bb_id);

  void Print(vector<InstInfo> &Ins, vector<vector<uint32_t> > &BB2Ins);
};

/// An iterator for the compressed trace data.
struct TraceIter {
  boost::iostreams::mapped_file_source trace;
  const char *data;
  char *decoded;
  size_t data_iter, decoded_iter, decoded_size;
  bool ended;

  TraceIter(char *trace_file_name) : trace(trace_file_name) {
    ended = false;
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = decoded_iter = decoded_size = 0;
  }

  /// Prepare the decompressed data
  bool Prepare();
  /// Obtain len bytes
  bool Next(void *ptr, size_t len);
  /// Obtain the next event.
  bool NextEvent(char &event_label, const uint64_t *&tid_ptr,
                 const uint32_t *&id_ptr, const uint64_t *&addr_ptr,
                 const uint64_t *&length_ptr, const uint64_t *&addr2_ptr);
};

//===----------------------------------------------------------------------===//
//                        Other
//===----------------------------------------------------------------------===//

void ExtractImpactfulFunCall(
  char *pin_trace_file_name, set<uint64_t> &impactful_fun_call);

void MergeTrace(
  char *trace_file_name, set<uint64_t> &impactful_fun_call,
  vector<SmallestBlock> &block_trace);

void GroupMemory(vector<SmallestBlock> &block_trace);

void ExtractMemoryDependency(
  vector<SmallestBlock> &block_trace,
  map<DynamicInst, vector<DynamicInst> > &mem_dep);

#endif // SLIMMER_TOOLS_H