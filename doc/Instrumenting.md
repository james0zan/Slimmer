# Traced Events

The SlimmerTrace pass will instrument
every basic block, every memory access, every allocation site and every external call of the program,
so that each execution of the instrumented application will generate a tracing file for analyzing.

Specifically, the tracing file is consisting of the following types of events:

## BasicBlockEvent

A BasicBlockEvent is logged at the beginning of each basic block.
It is used to infer the control dependencies.

The fields of a BasicBlockEvent are:

    id: the unique basic block ID
    tid: the thread that executes this basic block

## MemoryEvent

A MemoryEvent is logged for each memory load/store/allocation.
It is used to infer the data dependencies.

The fields of a MemoryEvent are:

    id: the unique instruction ID of the load/store instruction
    tid: the thread that executes this instruction
    addr: the starting address of the loaded/stored memory
    size: the size of the loaded/stored data

We can determine whether the instruction is a load, store, or allocation by its ID.

## ReturnEvent & ArgumentEvent

A ReturnEvent is logged for each **uninstrumented** function.
It is used to infer the data that impacts the outside system.

The fields of a ReturnEvent are:

    id: the unique instruction ID of the call instruction
    tid: the thread that executes this instruction
    fun: the starting address of the function

The ArgumentEvent is used for recording the pointer arguments of the uninstrumented functions.
It contains only two fields, which are:

    tid: the thread that executes this instruction
    arg: the value of the pointer argument

# Code Information (generated while linking)

In order to collect enough information for analyzing the program traces,
the SlimmerTrace will also output a set of code infomation files while the compiling.

The default directory of these info files is "Slimmer",
which can be changed by setting the "slimmer-info-dir" flag.

## Inst

The "Inst" file maps an instruction ID to the corresponding information,
in which each line is:

    InstructionID:
        BasicBlockID,
        Is a pointer or not,
        Line of code or -1 if not available,
        Path to the code file (in base64) or [UNKNOWN] if not available,
        The instruction's LLVM IR (in base64),
        [SSA dependency 1, SSA dependency 2, ..., ],
        Type = {NormalInst, LoadInst, StoreInst, CallInst, ExternalCallInst,
            ReturnInst, TerminatorInst, PhiNode, AtomicInst, AllocaInst},
        {
            For CallInst: {
                The called function name or [UNKNOWN] if not available,
            },
            For TerminatorInst: {
                [BasicBlockID of successor 1, BasicBlockID of successor 2, ..., ]
            },
            For PhiNode: {
                [<Income BasicBlockID 1, Income value>, <Income BasicBlockID 2, Income value>, ..., ]
            },
        },
        

A data dependency (i.e., a SSA dependency or a income value for the PhiNode) is represented as:

    <
        Type = {Inst, Arg, Constant},
        ID: {
            For Inst: the InstructionID,
            For Arg: which argument it uses,
            For Constant: 0
        }
    >


Specifically, a "ExternalCallInst" is a function call that calls an uninstrumented function (e.g., a library function); a "AtomicInst" is an AtomicCmpXchgInst or an AtomicRMWInst.

## InstrumentedFun

The name of instrumented functions.
One function per line.

## BBGraph

The basic block calling graph,
in which we record which basic block can be jumped from which basic clocks.

