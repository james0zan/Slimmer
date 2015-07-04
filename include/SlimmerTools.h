#ifndef SLIMMER_TOOLS_H
#define SLIMMER_TOOLS_H

#include "SlimmerUtil.h"

#include <algorithm>
#include <stack>
#include <boost/iostreams/device/mapped_file.hpp>

using namespace std;

namespace boost {
void throw_exception(std::exception const& e) {}
}

struct TraceIter {
  boost::iostreams::mapped_file_source trace;
  const char* data;
  char *decoded;
  size_t data_iter, decoded_iter, decoded_size;
  bool ended;

  TraceIter(char *trace_file_name) : trace(trace_file_name) {
    ended = false;
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = decoded_iter = decoded_size = 0;
  }

  bool NextEvent(
    char& event_label, const uint64_t*& tid_ptr, 
    const uint32_t*& id_ptr, const uint64_t*& addr_ptr, 
    const uint64_t*& length_ptr) {
    
    if (decoded_iter >= decoded_size) {
      if (ended || data_iter >= trace.size()) return false; // Trace is ended
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter += sizeof(uint64_t);

      decoded_size = LZ4_decompress_safe ((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      assert(decoded_size > 0);
      decoded_iter = 0;

      data_iter += length + sizeof(uint64_t);
    }
    decoded_iter += GetEvent(false, decoded + decoded_iter, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    if (event_label == EndEventLabel) ended = true;
    return true;
  }
};

struct TraceBackwardIter {
  boost::iostreams::mapped_file_source trace;
  const char* data;
  char *decoded;
  int64_t data_iter, decoded_iter, decoded_size;

  TraceBackwardIter(char *trace_file_name) : trace(trace_file_name) {
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = trace.size();
    decoded_iter = -1;
    decoded_size = 0;
  }

  bool FormerEvent(
    char& event_label, const uint64_t*& tid_ptr, 
    const uint32_t*& id_ptr, const uint64_t*& addr_ptr, 
    const uint64_t*& length_ptr) {
    
    if (decoded_iter <= 0) {
      if (data_iter <= 0) return false; // Trace is ended
      data_iter -= sizeof(uint64_t);
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter -= length;

      decoded_size = LZ4_decompress_safe((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      decoded_iter = decoded_size - 1;

      data_iter -= sizeof(uint64_t);
    }

    decoded_iter -= GetEvent(true, decoded + decoded_iter, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr);
    return true;
  }
};

struct SmallestBlock {
  enum SmallestBlockType {
    NormalBlock,
    MemoryAccessBlock,
    ExternalCallBlock,
    ImpactfulCallBlock,
  } Type;

  uint64_t TID;
  uint32_t BBID, Start, End;
  vector<uint64_t> Addr;

  // 0: Is a following SmallestBlock
  // 1: Is the first SmallestBlock of a called function
  // 2: Is the first SmallestBlock of a thread
  uint8_t IsFirst;
  uint8_t IsLast;
  uint32_t Caller; // The instruction ID of the caller, valid when IsFirst == 1

  int32_t LastBBID;

  SmallestBlock() {}
  SmallestBlock(SmallestBlockType t, uint64_t tid, uint32_t bb_id, uint32_t start, uint32_t end, pair<uint8_t, uint32_t> first, int32_t last_bb_id) {
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

  void Print(vector<InstInfo>& Ins, vector<vector<uint32_t> >& BB2Ins) {
    if (Type == NormalBlock) {
      printf("[Thead %lu] NormalBlock\n\t<BB %u, Index %u> -> <BB %u, Index %u>", TID, BBID, Start, BBID, End);
      printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
      printf("\tLastBBID %d\n", LastBBID);
      for (uint32_t i = Start; i < End; ++i) {
        printf("\t%u: %s\n", BB2Ins[BBID][i], Ins[BB2Ins[BBID][i]].Code.c_str());
      }
    } else if (Type == MemoryAccessBlock) {
      printf("[Thead %lu] MemoryAccessBlock\n\t<BB %u, Index %u> Address [%p, %p)", TID, BBID, Start, (void*)Addr[0], (void*)Addr[1]);
      printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
      printf("\tLastBBID %d\n", LastBBID);
      printf("\t%u: %s\n", BB2Ins[BBID][Start], Ins[BB2Ins[BBID][Start]].Code.c_str());
    } else if (Type == ExternalCallBlock || Type == ImpactfulCallBlock) {
      printf("[Thead %lu] %s\n", TID, Type == ExternalCallBlock ? "ExternalCallBlock" : "ImpactfulCallBlock");
      printf("\t<BB %u, Index %u> Address %p\n\tArg", BBID, Start, (void*)Addr[0]);
      for (size_t i = 1; i < Addr.size(); ++i) printf(" %p", (void*)Addr[i]);
      printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
      printf("\tLastBBID %d\n", LastBBID);
      printf("\t%u: %s\n", BB2Ins[BBID][Start], Ins[BB2Ins[BBID][Start]].Code.c_str());
    }
  }

  void Dump(FILE* f) {
    uint32_t addr_size = Addr.size();
    uint8_t type = 0;
    if (Type == NormalBlock) type = 0;
    if (Type == MemoryAccessBlock) type = 1;
    if (Type == ExternalCallBlock) type = 2;
    if (Type == ImpactfulCallBlock) type = 3;

    fwrite(&addr_size, sizeof(uint32_t), 1, f);
    fwrite(&type, sizeof(uint8_t), 1, f);
    fwrite(&TID, sizeof(uint64_t), 1, f);
    fwrite(&BBID, sizeof(uint32_t), 1, f);
    fwrite(&Start, sizeof(uint32_t), 1, f);
    fwrite(&End, sizeof(uint32_t), 1, f);
    fwrite(&IsFirst, sizeof(uint8_t), 1, f);
    fwrite(&Caller, sizeof(uint32_t), 1, f);
    fwrite(&IsLast, sizeof(uint8_t), 1, f);
    fwrite(&LastBBID, sizeof(int32_t), 1, f);
    for (auto i: Addr) {
      fwrite(&i, sizeof(uint64_t), 1, f);
    }
    fwrite(&addr_size, sizeof(uint32_t), 1, f);
  }

  uint32_t ReadFrom(const char*& from) {
    const char *origin = from;

    uint32_t addr_size = (*(uint32_t *)(from)); from += 4;
    uint8_t type = (*(uint8_t *)(from)); from += 1;
    if (type == 0) Type = NormalBlock;
    if (type == 1) Type = MemoryAccessBlock;
    if (type == 2) Type = ExternalCallBlock;
    if (type == 3) Type = ImpactfulCallBlock;
    TID = (*(uint64_t *)(from)); from += 8;
    BBID = (*(uint32_t *)(from)); from += 4;
    Start = (*(uint32_t *)(from)); from += 4;
    End = (*(uint32_t *)(from)); from += 4;
    IsFirst = (*(uint8_t *)(from)); from += 1;
    Caller = (*(uint32_t *)(from)); from += 4;
    IsLast = (*(uint8_t *)(from)); from += 1;
    LastBBID = (*(int32_t *)(from)); from += 4;
    for (uint32_t i = 0; i < addr_size; ++i, from += 8) {
      Addr.push_back(*(uint64_t *)(from));
    }
    from += 4;

    return from - origin;
  }

  void ReadBack(const char*& data, int64_t& cur) {    
    cur -= 4; uint32_t addr_size = (*(uint32_t *)(&data[cur])); 
    for (uint32_t i = 0; i < addr_size; ++i) {
      cur -= 8; Addr.push_back(*(uint64_t *)(&data[cur]));
    }
    reverse(Addr.begin(),Addr.end()); 
    
    cur -= 4; LastBBID = (*(int32_t *)(&data[cur])); 
    cur -= 1; IsLast = (*(uint8_t *)(&data[cur])); 
    cur -= 4; Caller = (*(uint32_t *)(&data[cur])); 
    cur -= 1; IsFirst = (*(uint8_t *)(&data[cur])); 
    cur -= 4; End = (*(uint32_t *)(&data[cur])); 
    cur -= 4; Start = (*(uint32_t *)(&data[cur])); 
    cur -= 4; BBID = (*(uint32_t *)(&data[cur])); 
    cur -= 8; TID = (*(uint64_t *)(&data[cur])); 
    cur -= 1; uint8_t type = (*(uint8_t *)(&data[cur]));
    if (type == 0) Type = NormalBlock;
    if (type == 1) Type = MemoryAccessBlock;
    if (type == 2) Type = ExternalCallBlock;
    if (type == 3) Type = ImpactfulCallBlock;
    cur -= 4;
  }
};

#endif // SLIMMER_TOOLS_H