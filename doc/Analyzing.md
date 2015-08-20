# Memory grouping

A tricky part of analyzing the traces is the handling of external library function calls.
Since these functions are not instrumented by the SlimmerTrace pass, they are black boxes for us.
However, we still need to know which memory addresses have been accessed by them for completing the memory dependency relations.
In order to resolve this problem, we designed a memory grouping algorithm
that can group a memory address with all the other addresses that are calculable from it.

Specifically, a memory address *AddrB* is calculable from another memory address *AddrA* if

1) *AddrB* == **AddrA*, i.e., the point-to relation;

2) *AddrB* == *AddrA* + offset$, i.e., *AddrA* and *AddrB* are within the same aggregate type object (e.g., struct, array);

3) there exist a memory address *AddrC* that *AddrB* is calculable from *AddrC* and *AddrC* is calculable from *AddrA*, i.e., the transitive law.

Essentially, all the memory addresses of a logical object, all the pointers point to it, all the pointers point to those pointers, and again and again will be grouped into one group.
Here, we use the phrease "logical object" for representing
a set of data connected by linking pointers, e.g., a linked list or a linked tree.

After obtaining the memory groups, if a pointer *Ptr* is passed to an external function (recorded by Argument events),
we assume that all memory addresses that belong the same group of *Ptr* is also accessible by the function.
This method is not precise, because it assumes that the entire logical object (e.g., a list, or a tree) is read and modified by an external function call, even if only part of the object is truly accessed.
However, since our tool only looks for code that is likely to be unneeded, it do not need the algorithm to be precise.


# Algorithm for Generating Unused Operations

After obtaining all the needed infomation, the extracting of unneeded operations become stratforward.
Our tool (i.e., print-bug) will iterate all the executed dynamic instruction backwardly.
For each dynamic instruction 

1) if it is an external function call that impacts the outside,
it is needed and all the last writes of the memory accessed by that function is needed;

2) if it is marked as needed by former iterated dynamic instructions,
mark all the dependency of it as needed;

3) in other case, it is not needed.

The algorithm can be fomalized as following:

    addr2load := a map that maps the memory address to a list of instructions that load it
    data_dep := a map that maps an load instruction to the depended store instructions
    used_ins = a set of instructions that are used

    used_ins.add(LastReturn())
    for event <- [last event, ..., first event]:
        if event is ReturnEvent or MemoryEvent:
            ins := <event.tid, event.id>
            writed_addr = GetWrited(event)
            read_addr = GetRead(event)

            used, loads = addr2load.Eliminate(writed_addr)
            for i in loads: data_dep[i].add(ins)
            addr2load.Add(read_addr, ins)

            if event is ReturnEvent and event.fun impacts the outside:
                used = true

            if used:
                used_ins.add(ins)
                addr2load.MarkUsed(ins)
            else:
                 OutputUnusedIns(ins)
        if event is BasicBlockEvent:
            for ins_id <- [last ins of the BB, ..., first]:
                ins := <event.tid, ins_id>
                if ins in used_ins:
                    used_ins.remove(ins)
                    addr2load.MarkUsed(ins)

                    for i in data_dep[ins]: used_ins.add(i)
                    data_dep.Remove(ins)

                    used_ins.add(GetSSADep(ins))
                    used_ins.add(GetControlDep(ins))
                    if ins_id is CallInst:
                        used_ins.add(GetFunReturnDep(ins))                    
                else:
                    OutputUnusedIns(ins)
