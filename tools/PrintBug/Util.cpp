#include "SlimmerTools.h"

namespace boost {
void throw_exception(std::exception const &e) {}
}

pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id) {
  return make_pair(tid, id);
}

//===----------------------------------------------------------------------===//
//                        SmallestBlock
//===----------------------------------------------------------------------===//

SmallestBlock::SmallestBlock(SmallestBlockType t, uint64_t tid, uint32_t bb_id,
                             uint32_t start, uint32_t end,
                             pair<uint8_t, uint32_t> first,
                             int32_t last_bb_id) {
  Type = t;
  TID = tid;
  BBID = bb_id;
  Start = start;
  End = end;
  IsFirst = first.first;
  Caller = first.second;
  IsLast = 0;
  LastBBID = last_bb_id;
}

void SmallestBlock::Print(vector<InstInfo> &Ins,
                          vector<vector<uint32_t> > &BB2Ins) {
  if (Type == NormalBlock) {
    printf("[Thead %lu] NormalBlock\n\t<BB %u, Index %u> -> <BB %u, Index %u>",
           TID, BBID, Start, BBID, End);
    printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
    printf("\tLastBBID %d\n", LastBBID);
    for (uint32_t i = Start; i < End; ++i) {
      printf("\t%u: %s\n", BB2Ins[BBID][i], Ins[BB2Ins[BBID][i]].Code.c_str());
    }
  } else if (Type == MemoryAccessBlock || Type == MemsetBlock) {
    printf("[Thead %lu] %s\n\t<BB %u, Index %u> Address [%p, %p)", TID,
           Type == MemoryAccessBlock ? "MemoryAccessBlock" : "MemsetBlock",
           BBID, Start, (void *)Addr[0], (void *)Addr[1]);
    printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
    printf("\tLastBBID %d\n", LastBBID);
    printf("\t%u: %s\n", BB2Ins[BBID][Start],
           Ins[BB2Ins[BBID][Start]].Code.c_str());
  } else if (Type == ExternalCallBlock || Type == ImpactfulCallBlock) {
    printf("[Thead %lu] %s\n", TID, Type == ExternalCallBlock
                                        ? "ExternalCallBlock"
                                        : "ImpactfulCallBlock");
    printf("\t<BB %u, Index %u> Address %p\n\tArg", BBID, Start,
           (void *)Addr[0]);
    for (size_t i = 1; i < Addr.size(); ++i)
      printf(" %p", (void *)Addr[i]);
    printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
    printf("\tLastBBID %d\n", LastBBID);
    printf("\t%u: %s\n", BB2Ins[BBID][Start],
           Ins[BB2Ins[BBID][Start]].Code.c_str());
  } else if (Type == MemmoveBlock) {
    printf("[Thead %lu] MemmoveBlock\n\t<BB %u, Index %u> Address [%p, %p) "
           "[%p, %p)",
           TID, BBID, Start, (void *)Addr[0], (void *)Addr[1], (void *)Addr[2],
           (void *)Addr[3]);
    printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
    printf("\tLastBBID %d\n", LastBBID);
    printf("\t%u: %s\n", BB2Ins[BBID][Start],
           Ins[BB2Ins[BBID][Start]].Code.c_str());
  } else if (Type == DeclareBlock) {
    printf("DeclareBlock\n\tAddress [%p, %p)\n", (void *)Addr[0],
           (void *)Addr[1]);
  }
}

//===----------------------------------------------------------------------===//
//                        TraceIter
//===----------------------------------------------------------------------===//

/// Prepare the decompressed data
///
/// \return - return false if the trace is ended.
///
bool TraceIter::Prepare() {
  if (decoded_iter >= decoded_size) {
    if (ended || data_iter >= trace.size())
      return false; // Trace is ended
    uint64_t length = (*(uint64_t *)(&data[data_iter]));
    data_iter += sizeof(uint64_t);

    decoded_size = LZ4_decompress_safe((const char *)&data[data_iter], decoded,
                                       length, COMPRESS_BLOCK_SIZE);
    assert(decoded_size > 0);
    decoded_iter = 0;

    data_iter += length + sizeof(uint64_t);
  }
  return true;
}

/// Obtain len bytes
///
/// \param ptr - destination of the data.
/// \param len - length of the data.
/// \return - return false if the trace is ended.
///
bool TraceIter::Next(void *ptr, size_t len) {
  if (!Prepare())
    return false;

  memcpy(ptr, decoded + decoded_iter, len);
  decoded_iter += len;
  return true;
}

/// Obtain the next event.
///
/// \param * - stores the corresponding field of the next event.
/// \return - return false if the trace is ended.
///
bool TraceIter::NextEvent(char &event_label, const uint64_t *&tid_ptr,
                          const uint32_t *&id_ptr, const uint64_t *&addr_ptr,
                          const uint64_t *&length_ptr,
                          const uint64_t *&addr2_ptr) {
  if (!Prepare())
    return false;

  decoded_iter += GetEvent(false, decoded + decoded_iter, event_label, tid_ptr,
                           id_ptr, addr_ptr, length_ptr, addr2_ptr);
  if (event_label == EndEventLabel)
    ended = true;
  return true;
}
