// portablegc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//


#include "CollectableHash.h"


namespace GC {




    //How handles are allocated...
    //A lock free LIFO is filled with blocks of 16k free handles
    //whenever an object is created, 

  
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