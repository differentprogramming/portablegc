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




    //*******************

  
    LockFreeLIFO<Handle, HandleBlocks + MAX_COLLECTED_THREADS+1> HandleBlockQueue;
    LockFreeLIFO<Handle, MAX_COLLECTED_THREADS * 10000> ReleaseHandlesQueue;
    Handle HandleList[MAX_COLLECTED_THREADS];
    HandleType Handles[TotalHandles];

    int unqueued_handles = 0;
    int prev_unqueued_handle = EndOfHandleFreeList;

    void init_handle_blocks()
    {
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            HandleList[i] = EndOfHandleFreeList;
        }
        for (int i = 1; i < TotalHandles; ++i)
        {
            DeallocHandleInGC(i);
        }
        HandleList[0] = 1;
        Handles[0].ptr = collectable_null;
        CollectableNull.myHandle = 0;
    }

    int GrabHandleList()
    {
        int queue_pos = HandleBlockQueue.pop_fifo();
        int ret = HandleBlockQueue.all_links[queue_pos].data;
        HandleBlockQueue.push_free(queue_pos);
        return ret;
    }



    void FreeThreadHandles()
    {
        Handle next = HandleList[MyThreadNumber];
        if (next != EndOfHandleFreeList) {
            int queue_pos = HandleBlockQueue.pop_free();
            if (queue_pos != -1) {
                ReleaseHandlesQueue.all_links[queue_pos].data = next;
                ReleaseHandlesQueue.push_fifo(queue_pos);
            }
        }
    }

    void FreeThreadHandlesInGC() {
        for (;;) {
            int queue_pos = ReleaseHandlesQueue.pop_fifo();
            if (queue_pos == -1) break;
            int n = ReleaseHandlesQueue.all_links[queue_pos].data;
            ReleaseHandlesQueue.push_free(queue_pos);
            for (;;) {
                int m = Handles[n].list;
                DeallocHandleInGC(n);
                if (m == EndOfHandleFreeList) break;
                n = m;
            }
        }
    }





};