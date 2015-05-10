# Dependency

LLVM 3.4.2 (branches/release_34)

GNU gold (GNU Binutils 2.25.51.20150507) 1.11

# Compile Slimmer

    LLVM_SRC=/path/to/llvm/src
    LLVM_OBJ=$LLVM_SRC/build
    LLVM_BIN=$LLVM_OBJ/Release+Asserts/bin

    BINUTILS_INCLUDE=/path/to/binutils/include 

    git clone https://github.com/james0zan/Slimmer.git
    mkdir -p Slimmer/build && cd Slimmer/build

    CC=$LLVM_BIN/clang CXX=$LLVM_BIN/clang++ ../configure --with-llvmsrc=$LLVM_SRC --with-llvmobj=$LLVM_OBJ --with-binutils-include=$BINUTILS_INCLUDE --enable-optimized=yes

    CC=$LLVM_BIN/clang CXX=$LLVM_BIN/clang++ CPPFLAGS="-I/usr/include/c++/4.8/ -I/usr/include/x86_64-linux-gnu/c++/4.8/" CXXFLAGS="-std=c++11" VERBOSE=1 make

# Traced Events

## BasicBlockEvent

A BasicBlockEvent is logged at the beginning of each basic block.
It is used to infer the control dependencies.

The fields of a BasicBlockEvent are:

    id: the unique basic block ID
    tid: the thread that executes this basic block

## MemoryEvent

A MemoryEvent is logged for each memory load/store.
It is used to infer the data dependencies.

The fields of a MemoryEvent are:

    id: the unique instruction ID of the load/store instruction
    tid: the thread that executes this instruction
    addr: the starting address of the loaded/stored memory
    size: the size of the loaded/stored data

We can determine whether the instruction is a load or store by its ID.

## OutputEvent

An OutputEvent is logged for each output system call.
It logs the data that impacts the outside system.

Currently, the OutputEvent is indirectly inferred by using the dynamic instrumentation tool PIN.
A LLVM version is in the TODO list.

# Code Information (generated while linking)

## Inst

The ``Inst" file maps an instruction ID to the corresponding information,
in which each line is:

    InstructionID:
        BasicBlockID,
        Path to the code file (in base64) or [UNKNOWN] if not available,
        Line of code or -1 if not available,
        The instruction's LLVM IR (in base64)

## VarArg

## CallInst

## PhiNode

## TerminatorInst
