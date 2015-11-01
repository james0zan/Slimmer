$CXX std=c++11 -flto -g -O0 main.cc -o main -L/home/zhangmx/GSoC/Slimmer/build/Release+Asserts/lib -lSlimmerRuntime -lSlimmerUtil -lpthread -lstdc++ -llz4
# $PIN_HOME/pin.sh -t ../../build/Release+Asserts/lib/SlimmerPinTool.so -i Slimmer/137066459/InstrumentedFun -- ./main

# ../../build/Release+Asserts/bin/merge-trace SlimmerPinTrace Slimmer/137066459/Inst /scratch1/zhangmx/SlimmerTrace
# ../../build/Release+Asserts/bin/extract-memory-dependency Slimmer/1230162384/Inst /scratch1/zhangmx/SlimmerMergedTrace
# ../../build/Release+Asserts/bin/extract-uneeded-operation Slimmer/1230162384/Inst Slimmer/1230162384/BBGraph SlimmerMemoryDependency /scratch1/zhangmx/SlimmerMergedTrace

# ../../build/Release+Asserts/bin/extract-memory-dependency Slimmer/Inst /scratch1/zhangmx/SlimmerTrace 
# ../../build/Release+Asserts/bin/extract-impactful-funcall SlimmerPinTrace 
# ../../build/Release+Asserts/bin/extract-uneeded-operation Slimmer/Inst SlimmerMemoryDependency SlimmerImpactFunCall /scratch1/zhangmx/SlimmerTrace  