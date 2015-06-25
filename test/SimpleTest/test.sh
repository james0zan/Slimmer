$CXX -flto -g -O0 main.cc -c -o main.o
$LD_NEW -z relro --hash-style=gnu --build-id --eh-frame-hdr -m elf_x86_64 -dynamic-linker /lib64/ld-linux-x86-64.so.2 \
  -o main \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/crt1.o \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/crti.o  \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/crtbegin.o \
  -L/usr/lib/gcc/x86_64-linux-gnu/4.8 -L/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu \
  -L/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../lib64 -L/lib/x86_64-linux-gnu \
  -L/lib/../lib64 -L/usr/lib/x86_64-linux-gnu -L/usr/lib/../lib64 \
  -L/usr/lib/x86_64-linux-gnu/../../lib64 -L/usr/lib/gcc/x86_64-linux-gnu/4.8/../../.. -L/lib -L/usr/lib \
  -plugin ../../build/Release+Asserts/lib/SlimmerGold.so -plugin-opt=mcpu=x86-64 \
  main.o \
  -lstdc++ -lm -lgcc_s -lgcc -lc -lgcc_s -lgcc \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/crtend.o \
  /usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/crtn.o \
  -plugin-opt=--slimmer-info-dir=Slimmer \
  -L/home/zhangmx/GSoC/Slimmer/build/Release+Asserts/lib \
  -lSlimmerRuntime -lSlimmerUtil -lpthread -lstdc++ -llz4
# $PIN_HOME/pin.sh -t ../../build/Release+Asserts/lib/SlimmerPinTool.so -i Slimmer/InstrumentedFun -- ./main
# ../../build/Release+Asserts/bin/extract-memory-dependency Slimmer/Inst SlimmerTrace 
# ../../build/Release+Asserts/bin/extract-impactful-funcall SlimmerPinTrace 
# ../../build/Release+Asserts/bin/extract-uneeded-operation Slimmer/Inst SlimmerMemoryDependency SlimmerImpactFunCall SlimmerTrace 