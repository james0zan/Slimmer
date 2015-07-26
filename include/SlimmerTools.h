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

inline pair<uint64_t, uint32_t> I(uint64_t tid, uint32_t id) {
  return make_pair(tid, id);
}

/// An iterator for the compressed trace data.
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

  /// Obtain the next event.
  ///
  /// \param * - stores the corresponding field of the next event.
  /// \return - return false if the trace is ended.
  ///
  bool NextEvent(
    char& event_label, const uint64_t*& tid_ptr, 
    const uint32_t*& id_ptr, const uint64_t*& addr_ptr, 
    const uint64_t*& length_ptr, const uint64_t*& addr2_ptr) {
    
    if (decoded_iter >= decoded_size) {
      if (ended || data_iter >= trace.size()) return false; // Trace is ended
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter += sizeof(uint64_t);

      decoded_size = LZ4_decompress_safe ((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      assert(decoded_size > 0);
      decoded_iter = 0;

      data_iter += length + sizeof(uint64_t);
    }
    decoded_iter += GetEvent(false, decoded + decoded_iter, event_label, tid_ptr, id_ptr, addr_ptr, length_ptr, addr2_ptr);
    if (event_label == EndEventLabel) ended = true;
    return true;
  }
};

/// A smallest block is a continuous part of a basic block
/// that will always be executed continuously.
///
struct SmallestBlock {
  enum SmallestBlockType {
    NormalBlock, // A continuous part of a basic block that contains no memory accessed
    MemoryAccessBlock, // A single memory access
    ExternalCallBlock, // A single external call that do not impact the outside enviroment
    ImpactfulCallBlock, // A single external call that impacts the outside enviroment
    MemsetBlock, // A single memset
    MemmoveBlock // A single memset
  } Type;

  uint64_t TID;
  uint32_t BBID, Start, End; // The instructions' ID of this SmallestBlock is blong to [Start, End).
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
    } else if (Type == MemoryAccessBlock || Type == MemsetBlock) {
      printf("[Thead %lu] %s\n\t<BB %u, Index %u> Address [%p, %p)", 
        TID, Type == MemoryAccessBlock ? "MemoryAccessBlock" : "MemsetBlock",
        BBID, Start, (void*)Addr[0], (void*)Addr[1]);
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
    } else if (Type == MemmoveBlock) {
      printf("[Thead %lu] MemmoveBlock\n\t<BB %u, Index %u> Address [%p, %p) [%p, %p)", 
        TID, BBID, Start, (void*)Addr[0], (void*)Addr[1], (void*)Addr[2], (void*)Addr[3]);
      printf("\n\tIsFirst %d IsLast %d Caller %u\n", IsFirst, IsLast, Caller);
      printf("\tLastBBID %d\n", LastBBID);
      printf("\t%u: %s\n", BB2Ins[BBID][Start], Ins[BB2Ins[BBID][Start]].Code.c_str());
    } 
  }

  size_t Size() {
    return 39 + 8 * Addr.size();
  }

  /// Dump a SmallestBlock to a file.
  ///
  void Dump(FILE* f) {
    uint32_t addr_size = Addr.size();
    uint8_t type = 0;
    if (Type == NormalBlock) type = 0;
    if (Type == MemoryAccessBlock) type = 1;
    if (Type == ExternalCallBlock) type = 2;
    if (Type == ImpactfulCallBlock) type = 3;
    if (Type == MemsetBlock) type = 4;
    if (Type == MemmoveBlock) type = 5;

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

  /// Dump a SmallestBlock to a buffer.
  ///
  void Dump(char *from) {
    uint32_t addr_size = Addr.size();
    uint8_t type = 0;
    if (Type == NormalBlock) type = 0;
    if (Type == MemoryAccessBlock) type = 1;
    if (Type == ExternalCallBlock) type = 2;
    if (Type == ImpactfulCallBlock) type = 3;
    if (Type == MemsetBlock) type = 4;
    if (Type == MemmoveBlock) type = 5;

    (*(uint32_t *)(from)) = addr_size; from += 4;
    (*(uint8_t *)(from)) = type; from += 1;
    (*(uint64_t *)(from)) = TID; from += 8;
    (*(uint32_t *)(from)) = BBID; from += 4;
    (*(uint32_t *)(from)) = Start; from += 4;
    (*(uint32_t *)(from)) = End; from += 4;
    (*(uint8_t *)(from)) = IsFirst; from += 1;
    (*(uint32_t *)(from)) = Caller; from += 4;
    (*(uint8_t *)(from)) = IsLast; from += 1;
    (*(int32_t *)(from)) = LastBBID; from += 4;
    for (uint32_t i = 0; i < addr_size; ++i, from += 8) {
      (*(uint64_t *)(from)) = Addr[i];
    }
    (*(uint32_t *)(from)) = addr_size; from += 4;
  }

  /// Read a SmallestBlock from a file.
  ///
  /// \param from - the SmallestBlock is stored start from here
  /// \return - the size of this SmallestBlock
  ///
  uint32_t ReadFrom(const char*& from) {
    const char *origin = from;

    uint32_t addr_size = (*(uint32_t *)(from)); from += 4;
    uint8_t type = (*(uint8_t *)(from)); from += 1;
    if (type == 0) Type = NormalBlock;
    if (type == 1) Type = MemoryAccessBlock;
    if (type == 2) Type = ExternalCallBlock;
    if (type == 3) Type = ImpactfulCallBlock;
    if (type == 4) Type = MemsetBlock;
    if (type == 5) Type = MemmoveBlock;
    TID = (*(uint64_t *)(from)); from += 8;
    BBID = (*(uint32_t *)(from)); from += 4;
    Start = (*(uint32_t *)(from)); from += 4;
    End = (*(uint32_t *)(from)); from += 4;
    IsFirst = (*(uint8_t *)(from)); from += 1;
    Caller = (*(uint32_t *)(from)); from += 4;
    IsLast = (*(uint8_t *)(from)); from += 1;
    LastBBID = (*(int32_t *)(from)); from += 4;
    Addr.clear();
    for (uint32_t i = 0; i < addr_size; ++i, from += 8) {
      Addr.push_back(*(uint64_t *)(from));
    }
    from += 4;

    return from - origin;
  }

  /// Read a SmallestBlock from a file backwardly.
  ///
  /// \param data - the mmaped data buffer that reserve SmallestBlocks.
  /// \param cur - the end of this SmallestBlock is stored at data[cur - 1].
  /// \return - the size of this SmallestBlock
  ///
  void ReadBack(const char*& data, int64_t& cur) {    
    cur -= 4; uint32_t addr_size = (*(uint32_t *)(&data[cur])); 
    Addr.clear();
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
    if (type == 4) Type = MemsetBlock;
    if (type == 5) Type = MemmoveBlock;
    cur -= 4;
  }
};

class SmallestBlockTrace {
public:
  SmallestBlockTrace(const char *path) {
    size = COMPRESS_BLOCK_SIZE;

    buffer = (char *)malloc(size);
    compressed = (char *)malloc(LZ4_compressBound(size));
    assert(buffer && compressed && "Failed to malloc the SmallestBlockTrace!\n");

    stream = fopen(path, "wb");
    assert(stream && "Failed to open tracing file!\n");
    offset = 0;
  }

  void Append(SmallestBlock& b) {
    if (offset + b.Size() > size) {
      uint64_t after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
      fwrite(&after_compress, sizeof(after_compress), 1, stream);
      size_t cur = 0;
      while (cur < after_compress) {
        size_t tmp = fwrite(compressed + cur, 1, after_compress - cur, stream);
        if (tmp > 0) cur += tmp;
      }
      fwrite(&after_compress, sizeof(after_compress), 1, stream);
      printf("after_compress: %lu\n", after_compress);
      offset = 0;
    }
    assert(offset + b.Size() <= size);

    b.Dump(buffer + offset);
    offset += b.Size();
  }

  ~SmallestBlockTrace() {
    uint64_t after_compress = LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed, offset, LZ4_compressBound(size));
    fwrite(&after_compress, sizeof(after_compress), 1, stream);
    size_t cur = 0;
    while (cur < after_compress) {
      size_t tmp = fwrite(compressed + cur, 1, after_compress - cur, stream);
      if (tmp > 0) cur += tmp;
    }
    fwrite(&after_compress, sizeof(after_compress), 1, stream);
    printf("after_compress2: %lu\n", after_compress);
    fclose(stream);
  }

private:
  char *buffer, *compressed;
  FILE* stream;
  size_t offset;
  size_t size; // Size of the event buffer in bytes
};

/// An iterator for the compressed trace data.
struct SmallestBlockIter {
  boost::iostreams::mapped_file_source trace;
  const char* data;
  char *decoded;
  size_t data_iter, decoded_iter, decoded_size;

  SmallestBlockIter(char *trace_file_name) : trace(trace_file_name) {
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = decoded_iter = decoded_size = 0;
  }

  /// Obtain the next SmallestBlock.
  ///
  /// \param * - stores the corresponding field of the next event.
  /// \return - return false if the trace is ended.
  ///
  bool NextSmallestBlock(SmallestBlock& b) {
    if (decoded_iter >= decoded_size) {
      if (data_iter >= trace.size()) return false; // Trace is ended
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter += sizeof(uint64_t);

      decoded_size = LZ4_decompress_safe ((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      assert(decoded_size > 0);
      decoded_iter = 0;

      data_iter += length + sizeof(uint64_t);
    }
    const char *cur = decoded + decoded_iter;
    decoded_iter += b.ReadFrom(cur);
    return true;
  }
};

struct SmallestBlockBackwardIter {
  boost::iostreams::mapped_file_source trace;
  const char* data;
  char *decoded;
  int64_t data_iter, decoded_iter, decoded_size;

  SmallestBlockBackwardIter(char *trace_file_name) : trace(trace_file_name) {
    data = trace.data();
    decoded = (char *)malloc(COMPRESS_BLOCK_SIZE);
    data_iter = trace.size();
    decoded_iter = -1;
    decoded_size = 0;
  }

  bool FormerSmallestBlock(SmallestBlock& b) {
    if (decoded_iter <= 0) {
      if (data_iter <= 0) return false; // Trace is ended
      data_iter -= sizeof(uint64_t);
      uint64_t length = (*(uint64_t *)(&data[data_iter]));
      data_iter -= length;

      decoded_size = LZ4_decompress_safe((const char*) &data[data_iter], decoded, length, COMPRESS_BLOCK_SIZE);
      decoded_iter = decoded_size;

      data_iter -= sizeof(uint64_t);
    }

    const char* tmp = decoded;
    b.ReadBack(tmp, decoded_iter);
    return true;
  }
};

#endif // SLIMMER_TOOLS_H