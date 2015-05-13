LEVEL := ../..
LIBRARYNAME := SlimmerPinTool
SHARED_LIBRARY := 1
LOADABLE_MODULE := 1

CXXFLAGS += -DBIGARRAY_MULTIPLIER=1 -fno-stack-protector -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX  -I$(PIN_HOME)/source/include/pin -I$(PIN_HOME)/source/include/pin/gen -I$(PIN_HOME)/extras/components/include -I$(PIN_HOME)/extras/xed-intel64/include -I$(PIN_HOME)/source/tools/InstLib -O3 -fomit-frame-pointer -fno-strict-aliasing
LDFLAGS +=  -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic -Wl,--version-script=$(PIN_HOME)/source/include/pin/pintool.ver -L$(PIN_HOME)/intel64/lib -L$(PIN_HOME)/intel64/lib-ext -L$(PIN_HOME)/intel64/runtime/glibc -L$(PIN_HOME)/extras/xed-intel64/lib

include $(LEVEL)/Makefile.common
LIBS += -lpin -lxed -lpindwarf -ldl

# all:
# 	g++ -std=c++11 -DBIGARRAY_MULTIPLIER=1 -Wall -Werror -Wno-unknown-pragmas -fno-stack-protector -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX  -I$(PIN_HOME)/source/include/pin -I$(PIN_HOME)/source/include/pin/gen -I$(PIN_HOME)/extras/components/include -I$(PIN_HOME)/extras/xed-intel64/include -I$(PIN_HOME)/source/tools/InstLib -O3 -fomit-frame-pointer -fno-strict-aliasing -c -o PinTrace.o PinTrace.cpp
# 	g++ -std=c++11 -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic -Wl,--version-script=$(PIN_HOME)/source/include/pin/pintool.ver -o PinTrace.so PinTrace.o -L$(PIN_HOME)/intel64/lib -L$(PIN_HOME)/intel64/lib-ext -L$(PIN_HOME)/intel64/runtime/glibc -L$(PIN_HOME)/extras/xed-intel64/lib -lpin -lxed -lpindwarf -ldl