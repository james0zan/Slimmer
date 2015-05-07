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
