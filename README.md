# portablegc

A precise garbage collector for C++ that supports multiple mutating threads and even multiple collecting threads.  Threads are never stopped at any stage of collection.

In order to use it, you have to create types that can tell the collector how many pointers they contain and supply them one by one to be traced. The type also has to support being created in a dormant state to sit in a block of other such objects, to be activated when needed and deactivated when collected.   There is no support for resurrecting any objects on finalization. 

It also has configurable support for tagged pointers for value types, so for instance you could have a cheap small int type, an atom type and character type if you set aside two bit for value types.

You also create blocks of root variables.  There is support for local roots, but they don't actually live on the stack, they refer to those blocks of root variables.

Each thread only allocates from blocks that it owns, but there are no limitations on sharing live objects between threads.  Like Java or .net the collector has no problem with collecting objects that are being contended on from multiple threads. 

The basic design is that each pointer is actually a pair of pointers, and when collection is not going on, each store to a pointer is actually an atomic, but not expensive, store to two pointers.  When collection starts, the stores are only to one of the two pointers and the other pointer is considered part of a snapshot that the collector follows.  When collection is done, stores go back to being to both pointers, meanwhile the collection threads are restoring the snapshot for pointers that were mutated during the collection.  

Everything happens in-place, no compaction ever takes place. 

While the mutating threads never stop for the gc (unless the gc gets so far behind that they have to wait for allocation), the gc does have to wait either for all of the mutating threads to acknowledge each state change in the gc, or to signal that they're no longer mutating. As such mutating threads have to periodically go through safe-points, and when a mutating thread makes a blocking call, it should opt out of mutating before the call and opt back in afterwards.  There are RAII objects to automate that.  Another result of this design is that it's a bad idea to have more threads active than you have hyperthreads available on the process, otherwise the gc will have to wait for task switches to progress between states. 


# I believe this is a novel garbage collector for a number of reasons.

A) It's the only parallel garbage collector I know of that never needs to pause mutator threads at all, as long as the GC is keeping up.

1) The collector DOES need to wait for each thread to either acknowledge each state change (from not collecting to collecting, from collecting to sweeping and restoring the snapshot, and from sweeping to a final stage that combines finishing the snapshot and being past collecting).

B) It's also the only one I know of that is written entirely in portable C++, requiring nothing more than the C11/C++/Rust memory order model. 

1) Being portable does incur some costs.  In the place of pointers, the garbage collector traces 32 bit indexes which are interpreted as mapping into a collection of tables (top bits pick the table, bottom bits the offset). 
2) On x86 64, it is possible to use the same algorithm to implement a normal garbage collector but not portably.  You need to use the CMPXCHG16B and MOVDQA instructions.  It will incur some cost in the collector due to the lack of a 128 bit interlocked store instruction, CMPXCHG16B will have to substitute.

C) The number of memory fenced interlocked instructions needed is very much smaller than reference counted pointers, so this should be faster.

D) Any number of mutator threads are allowed as are any number of collection threads.

Joshua Scholar 2022
