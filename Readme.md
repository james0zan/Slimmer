# Dependency

LLVM 3.4.2 (branches/release_34)

GNU gold (GNU Binutils 2.25.51.20150507) 1.11

Intel Pin 2.14 

libboost-iostreams-dev

liblz4-dev

# Compile Slimmer

    LLVM_SRC=/path/to/llvm/src
    LLVM_OBJ=$LLVM_SRC/build
    LLVM_BIN=$LLVM_OBJ/Release+Asserts/bin
    PIN=/path/to/intel/pintool

    BINUTILS_INCLUDE=/path/to/binutils/include 

    git clone https://github.com/james0zan/Slimmer.git
    mkdir -p Slimmer/build && cd Slimmer/build

    CC=$LLVM_BIN/clang CXX=$LLVM_BIN/clang++ ../configure --with-llvmsrc=$LLVM_SRC --with-llvmobj=$LLVM_OBJ --with-binutils-include=$BINUTILS_INCLUDE --enable-optimized=yes

    PIN_HOME=$PIN CC=$LLVM_BIN/clang CXX=$LLVM_BIN/clang++ CPPFLAGS="-I/usr/include/c++/4.8/ -I/usr/include/x86_64-linux-gnu/c++/4.8/" CXXFLAGS="-std=c++11" make

# Usages

The usage of Slimmer consists of three steps: 1) statically instrumenting the target program for generating traces; 2) running the application with PIN for identifying impactful external function calls;
and 3) analyzing the traces for finding potential bug sites.

Specifically, the static instrumenting part of Slimmer is implemented with a LLVM pass
([lib/SlimmerTrace](https://github.com/james0zan/Slimmer/tree/master/lib/SlimmerTrace)).
Thus the users need to append it to the analyses chain for enabling the tracing.
For facilitating this procedure, we built our own version of gold plugin
([tools/SlimmerGold](https://github.com/james0zan/Slimmer/tree/master/tools/SlimmerGold))
that has the instrumenting pass enabled automatically.

As a summary, in order to instrument the target program, the user need to:

1. replace the standard gold plugin with the compiled "lib/SlimmerGold.so";

2. compile the target program with LTO enabled (-flto);

3. linking the program with additional runtime libraries required by Slimmer (-lSlimmerRuntime -lpthread -lstdc++ -llz4).

More detailed description of LTO and gold plugin can be found at [here](http://llvm.org/docs/LinkTimeOptimization.html) and [here](http://llvm.org/docs/GoldPlugin.html).

After instrumenting, the user only needs to execute the program with our PIN tool (lib/SlimmerPinTool.so)
for generating traces and uses the analyzing tool (bin/print-bug) for finding potential bug sites.

# Example

Here is an example for showcasing how to use Slimmer for finding potential performance bugs.

## The example code

    #include <stdio.h>

    int main() {
      bool flag = false;
      for (int i = 0; i < 4; ++i) {
        if (i == 2) {
          flag = true;
        }
      }
      printf("%d\n", flag);
    }

The above code is a simple version of [GCC#57812](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57812),
in which the loop should break immediately after "flag" is set to "true".
It is obvious that all the iterations after "flag" set to "true" do not perform any useful work.

## Instrumenting

    cd test/SyntheticBugs/
    clang++ -flto -g -O0 UnusedLaterLoop.cpp -o UnusedLaterLoop -L../../build/Release+Asserts/lib -lSlimmerRuntime -lpthread -lstdc++ -llz4

The above compiling command will not only generate an instrumented application but also a set of code information files that will be needed in the analyzing (see [doc/Instrumenting.md](https://github.com/james0zan/Slimmer/tree/master/doc/Instrumenting.md) for more information).

The folder for reserving these information files will be printed to standard output stream while the compiling.

## Running & Analyzing

    pin.sh -t path/to/SlimmerPinTool.so -i path/to/InstrumentedFun -- ./UnusedLaterLoop
    print-bug path/to/SlimmerInfoDir path/to/SlimmerTrace path/to/SlimmerPinTrace

When running the application with the PIN tool, the program itself will generate a trace file (default to be named as SlimmerTrace) and the PIN tool will also generate another trace file (default to be named as SlimmerPinTrace).

By providing all these data to the "pint-bug" tool it will be able to print the potential bugs sites.

## Result

The output of the above program will be:

    ===============
    Bug 1
    ===============

    ------IR------
    (   1)  4:    store i8 0, i8* %flag, align 1, !dbg !14

    ------Related Code------

    test/SyntheticBugs/UnusedLaterLoop.cpp

            7:  #include <stdio.h>
            9:  int main() {
    (   1)  10:   bool flag = false;
            11:   for (int i = 0; i < 4; ++i) {
            12:     if (i == 2) {
            13:       flag = true;

    ===============
    Bug 2
    ===============

    ------IR------
    (   2)  7:    %0 = load i32* %i, align 4, !dbg !17
    (   2)  8:    %cmp = icmp slt i32 %0, 4, !dbg !17
    (   2)  9:    br i1 %cmp, label %for.body, label %for.end, !dbg !17
    (   1)  10:   %1 = load i32* %i, align 4, !dbg !18
    (   1)  11:   %cmp1 = icmp eq i32 %1, 2, !dbg !18
    (   1)  12:   br i1 %cmp1, label %if.then, label %if.end, !dbg !18
    (   2)  16:   %2 = load i32* %i, align 4, !dbg !17
    (   2)  17:   %inc = add nsw i32 %2, 1, !dbg !17
    (   2)  18:   store i32 %inc, i32* %i, align 4, !dbg !17

    ------Related Code------

    test/SyntheticBugs/UnusedLaterLoop.cpp

            9:  int main() {
            10:   bool flag = false;
    (  12)  11:   for (int i = 0; i < 4; ++i) {
    (   3)  12:     if (i == 2) {
            13:       flag = true;
            14:     }
            15:   }

As we can see, the "Bug 2" is the bug that we have described above.

In contrast, "Bug 1" is caused by the fact that the initialization of flag is not used in this case,
because it is overwritten before any useful read.
This is an unneeded operation that satisfies our definition, but it is not a bug since it may be used in other test cases.

