#include "SlimmerUtil.h"

#include <fstream>
using namespace std;

//===----------------------------------------------------------------------===//
//                           CompressBuffer
//===----------------------------------------------------------------------===//

CompressBuffer::CompressBuffer(const char *path) {
  size = COMPRESS_BLOCK_SIZE;

  buffer = (char *)malloc(size);
  compressed = (char *)malloc(LZ4_compressBound(size));
  assert(buffer && compressed && "Failed to malloc the CompressBuffer!\n");

  stream = fopen(path, "wb");
  assert(stream && "Failed to open tracing file!\n");
  offset = 0;
}

CompressBuffer::~CompressBuffer() {
  uint64_t after_compress =
      LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed,
                                 offset, LZ4_compressBound(size));
  fwrite(&after_compress, sizeof(after_compress), 1, stream);
  size_t cur = 0;
  while (cur < after_compress) {
    size_t tmp = fwrite(compressed + cur, 1, after_compress - cur, stream);
    if (tmp > 0)
      cur += tmp;
  }
  fwrite(&after_compress, sizeof(after_compress), 1, stream);
  fclose(stream);
}

/// Prepare a buffer larger than len
void CompressBuffer::Prepare(size_t len) {
  if (offset + len > size) {
    uint64_t after_compress =
        LZ4_compress_limitedOutput((const char *)buffer, (char *)compressed,
                                   offset, LZ4_compressBound(size));
    fwrite(&after_compress, sizeof(after_compress), 1, stream);
    size_t cur = 0;
    while (cur < after_compress) {
      size_t tmp = fwrite(compressed + cur, 1, after_compress - cur, stream);
      if (tmp > 0)
        cur += tmp;
    }
    fwrite(&after_compress, sizeof(after_compress), 1, stream);
    offset = 0;
  }
}

/// Append [ptr, ptr+len) to the buffer.
void CompressBuffer::Append(void *ptr, size_t len) {
  Prepare(len);

  memcpy(buffer + offset, ptr, len);
  offset += len;
}

//===----------------------------------------------------------------------===//
//                           Other
//===----------------------------------------------------------------------===//

/// Read the functions that are already traced by LLVM.
///
/// \param path - the path to the InstrumentedFun file.
/// \param instrumented - the set that reserves all the instrumented functions.
///
void LoadInstrumentedFun(string path, set<string> &instrumented) {
  ifstream file(path);
  string name;
  while (file >> name) {
    instrumented.insert(name);
  }
}

/// Read the instruction infomation.
///
/// \param path - the path to the Inst file.
/// \param info - the vector that reserves all the instruction infomation.
/// \param bb2ins - map a basic block ID to all the instructions it reserve.
///
void LoadInstInfo(string path, vector<InstInfo> &info,
                  vector<vector<uint32_t> > &bb2ins) {
  ifstream file(path);
  string tmp;
  int cnt, x;
  while (file >> x) {
    InstInfo ins;
    ins.ID = x;
    file >> ins.BB >> ins.IsPointer >> ins.LoC;

    if (ins.BB >= bb2ins.size())
      bb2ins.resize(ins.BB + 1);
    bb2ins[ins.BB].push_back(ins.ID);

    file >> ins.File;
    if (ins.File != "[UNKNOWN]")
      ins.File = base64_decode(ins.File);
    file >> ins.Code;
    if (ins.Code != "[UNKNOWN]")
      ins.Code = base64_decode(ins.Code);

    // printf("%d:\n\t%d\n\t%d\n\t%d\n\t%s\n\t%s\n", ins.ID, ins.BB,
    // ins.IsPointer, ins.LoC, ins.File.c_str(), ins.Code.c_str());
    file >> cnt;
    while (cnt--) {
      file >> tmp >> x;
      if (tmp == "Inst") {
        ins.SSADependencies.push_back(make_pair(InstInfo::Inst, x));
      } else if (tmp == "PointerArg") {
        ins.SSADependencies.push_back(make_pair(InstInfo::PointerArg, x));
      } else if (tmp == "Arg") {
        ins.SSADependencies.push_back(make_pair(InstInfo::Arg, x));
      } else {
        ins.SSADependencies.push_back(make_pair(InstInfo::Constant, x));
      }
    }
    // printf("\t");
    // for (auto i: ins.SSADependencies) {
    //   if (i.first == InstInfo::Inst) {
    //     printf("Inst %d ", i.second);
    //   } else if (i.first == InstInfo::Arg) {
    //     printf("Arg %d ", i.second);
    //   } else {
    //     printf("Constant ");
    //   }
    // }
    // printf("\n");

    file >> tmp;
    if (tmp == "Inst") {
      ins.Type = InstInfo::NormalInst;
    } else if (tmp == "LoadInst") {
      ins.Type = InstInfo::LoadInst;
    } else if (tmp == "StoreInst") {
      ins.Type = InstInfo::StoreInst;
    } else if (tmp == "CallInst") {
      ins.Type = InstInfo::CallInst;
    } else if (tmp == "ExternalCallInst") {
      ins.Type = InstInfo::ExternalCallInst;
    } else if (tmp == "ReturnInst") {
      ins.Type = InstInfo::ReturnInst;
    } else if (tmp == "TerminatorInst") {
      ins.Type = InstInfo::TerminatorInst;
    } else if (tmp == "PhiNode") {
      ins.Type = InstInfo::PhiNode;
    } else if (tmp == "AtomicInst") {
      ins.Type = InstInfo::AtomicInst;
    } else if (tmp == "AllocaInst") {
      ins.Type = InstInfo::AllocaInst;
    } else {
      ins.Type = InstInfo::VarArg;
    }

    if (ins.Type == InstInfo::CallInst ||
        ins.Type == InstInfo::ExternalCallInst) {
      file >> ins.Fun;
    } else if (ins.Type == InstInfo::TerminatorInst ||
               ins.Type == InstInfo::ReturnInst) {
      file >> cnt;
      while (cnt--) {
        file >> x;
        ins.Successors.push_back(x);
      }
    } else if (ins.Type == InstInfo::PhiNode) {
      file >> cnt;
      while (cnt--) {
        int a, b;
        file >> a >> tmp >> b;
        if (tmp == "Inst") {
          ins.PhiDependencies.push_back(make_tuple(a, InstInfo::Inst, b));
        } else if (tmp == "PointerArg") {
          ins.PhiDependencies.push_back(make_tuple(a, InstInfo::PointerArg, b));
        } else if (tmp == "Arg") {
          ins.PhiDependencies.push_back(make_tuple(a, InstInfo::Arg, b));
        } else {
          ins.PhiDependencies.push_back(make_tuple(a, InstInfo::Constant, b));
        }
      }
    }

    assert(info.size() == ins.ID);
    info.push_back(ins);
  }
}

/// Read an event start/end at cur.
///
/// \param backward - is the trace readed backward or forward.
/// \param cur - the start/end address of the event.
/// \param event_label - the label of the event.
/// \param *_ptr - the pointer of each fields.
///
int GetEvent(bool backward, const char *cur, char &event_label,
             const uint64_t *&tid_ptr, const uint32_t *&id_ptr,
             const uint64_t *&addr_ptr, const uint64_t *&length_ptr,
             const uint64_t *&addr2_ptr) {
  event_label = (*cur);

  switch (event_label) {
  case EndEventLabel:
    return 1;
  case PlaceHolderLabel:
    return 1;
  case BasicBlockEventLabel:
    if (backward)
      cur -= SizeOfBasicBlockEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 9);
    return SizeOfBasicBlockEvent;
  case MemoryEventLabel:
    if (backward)
      cur -= SizeOfMemoryEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 9);
    addr_ptr = (const uint64_t *)(cur + 13);
    length_ptr = (const uint64_t *)(cur + 21);
    return SizeOfMemoryEvent;
  case ReturnEventLabel:
    if (backward)
      cur -= SizeOfReturnEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 9);
    addr_ptr = (const uint64_t *)(cur + 13);
    return SizeOfReturnEvent;
  case ArgumentEventLabel:
    if (backward)
      cur -= SizeOfArgumentEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    addr_ptr = (const uint64_t *)(cur + 9);
    return SizeOfArgumentEvent;
  case MemsetEventLabel:
    if (backward)
      cur -= SizeOfMemsetEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 9);
    addr_ptr = (const uint64_t *)(cur + 13);
    length_ptr = (const uint64_t *)(cur + 21);
    return SizeOfMemsetEvent;
  case MemmoveEventLabel:
    if (backward)
      cur -= SizeOfMemmoveEvent - 1;
    tid_ptr = (const uint64_t *)(cur + 1);
    id_ptr = (const uint32_t *)(cur + 9);
    addr_ptr = (const uint64_t *)(cur + 13);
    addr2_ptr = (const uint64_t *)(cur + 21);
    length_ptr = (const uint64_t *)(cur + 29);
    return SizeOfMemmoveEvent;
  }
}

/// Identify external function calls that are always impactful
bool IsImpactfulFunction(std::string name) {
  if (name == "poll" ||
    name == "fcntl" ||
    name == "fclose" ||
    name == "write" ||
    name == "getpid" || 
    name == "listen" ||
    name == "close" || 
    name == "fflush" ||
    name == "signal" || 
    name == "fstate" ||
    name == "exit" ||
    name.substr(0, 8) == "pthread_") {
    return true;
  }
  return false;
}
