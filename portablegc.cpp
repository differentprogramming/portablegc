// portablegc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
/*
* 
* 
* I think I've come up with a pauseless, communicationless gc!
* The write barrier is a function pointer in an atomic variable
* Yes, mutators can get ONE MORE STORE IN, but if they store to a hazard pointer first then the write can't be missed
* switch to collect is to swap that variable
* switch to restore snapshot is swap it back
* flush non-atomic snapshot restore is a fence on the gc side!
* NO SAFEPOINTS NECESSARY
write barrier
save to hazard pointers for collection_count
If cell==collection_count save double, else save single

mutate
    save to hazard pointer for collection count first
    if  cell==collection_count save double
    else save single


allocate

find new cell, clear
set cell count=collection_count

copy into index instead of push_back

cons
inc mutate
allocate
set collection count current
fill in
inc mutate


to start set collecting atomic inc collection_count by two

follow snapshot of roots through snapshot of cells
- as reachable backwards thread  then update to current collection_count and value from leaves back,
read algorithm is read collection_count read values, read collection_count again if it changed try again
if any reached cells are snapshot and odd mutate, hold them and wait until they have values
collection done

collect cells that weren't reachable as separate scan, doesn't stop new collections
*/

#include "CollectableHash.h"

namespace GC {
     Handle GCHandleListHead[MAX_COLLECTED_THREADS];
     Handle GCHandleListTail[MAX_COLLECTED_THREADS];

    typedef uint32_t Handle;

    Handle HandleList[MAX_COLLECTED_THREADS];


    HandleType Handles[TotalHandles];

    void init_handle_lists()
    {
        int handle = 0;
        for(int i=0;i< MAX_COLLECTED_THREADS;++i)
        { 
            HandleList[i] = handle;
            for (int j = 0; j < HandlesPerThread; ++j)
            {
                Handles[j+handle].list = handle+j+1;
            }
            handle += HandlesPerThread;
            Handles[handle-1].list = EndOfHandleFreeList;
        }
        HandleList[0] = 1;
        Handles[0].ptr = collectable_null;
        //CollectableNull.myHandle = 0;
    }

};