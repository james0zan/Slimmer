$CXX -flto -g -O0 $1.cpp -c -o $1.o
$LD_NEW -z relro --hash-style=gnu --build-id --eh-frame-hdr -m elf_x86_64 -dynamic-linker /lib64/ld-linux-x86-64.so.2 \
  -o $1 \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/crt1.o \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/crti.o  \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/crtbegin.o \
  -L/usr/lib/gcc/x86_64-linux-gnu/4.8 -L/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu \
  -L/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../lib64 -L/lib/x86_64-linux-gnu \
  -L/lib/../lib64 -L/usr/lib/x86_64-linux-gnu -L/usr/lib/../lib64 \
  -L/usr/lib/x86_64-linux-gnu/../../lib64 -L/usr/lib/gcc/x86_64-linux-gnu/4.8/../../.. -L/lib -L/usr/lib \
  -plugin ../../build/Release+Asserts/lib/SlimmerGold.so -plugin-opt=mcpu=x86-64 \
  $1.o \
  -lstdc++ -lm -lgcc_s -lgcc -lc -lgcc_s -lgcc \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/crtend.o \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/crtn.o \
  -plugin-opt=--slimmer-info-dir=Slimmer \
  -L/home/zhangmx/GSoC/Slimmer/build/Release+Asserts/lib \
  -lSlimmerRuntime -lSlimmerUtil -lpthread -lstdc++ -llz4
$PIN_HOME/pin.sh -t ../../build/Release+Asserts/lib/SlimmerPinTool.so -i Slimmer/InstrumentedFun -- ./$1

../../build/Release+Asserts/bin/merge-trace SlimmerPinTrace Slimmer/Inst /scratch1/zhangmx/SlimmerTrace
../../build/Release+Asserts/bin/extract-memory-dependency Slimmer/Inst /scratch1/zhangmx/SlimmerMergedTrace
../../build/Release+Asserts/bin/extract-uneeded-operation Slimmer/Inst Slimmer/BBGraph SlimmerMemoryDependency /scratch1/zhangmx/SlimmerMergedTrace
