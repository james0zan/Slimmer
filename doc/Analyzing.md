# Algorithm for Generating Unused Operations

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

# Simple Dynamic AA

A disjoint set is used for maintaining the infomation of pointer groups.

The GetWrited and GetRead function of a ReturnEvent is implemented by union all the pointers' group of that function's parameters.

    for event <- [last event, ..., first event]:
        if event is MemoryEvent:
            AddAddrToGroup(GetGroupID(event.id), event.addr, event.length)
        if event is BasicBlockEvent:
            for ins_id <- [last ins of the BB, ..., first]:
                if the result of ins_id is a pointer:
                    for i in GetSSADep(ins):
                        if i is a pointer:
                            UnionGroup(GetGroupID(ins_id), GetGroupID(i))
                RemoveGroupInfo(ins_id)
