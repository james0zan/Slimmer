#===- Slimmer/tools/Ptrace/Makefile ----------------------------------*- Makefile -*-===##
# 
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

LEVEL := ../..
TOOLNAME = lite-strace
USEDLIBS = SlimmerUtil.a

include $(LEVEL)/Makefile.common
LIBS += -lunwind-ptrace -lunwind -lunwind-x86_64 -lboost_system -lboost_iostreams -llz4
