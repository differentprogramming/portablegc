// portablegc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
/*
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

#include <iostream>
#include <stdint.h>
#include <atomic>
#include <cassert>
#include <mutex>

template <typename T>
struct LockFreeFIFOLink
{
    T data;
    int32_t next;
};

template<typename T, int MAX_LEN>
struct LockFreeFIFOHead
{
    typedef union {
        struct {
            int32_t head;
            int32_t aba;
        };
        uint64_t combined;
    } ABAIndex;

    ABAIndex fifo;
    ABAIndex free;

    void push_fifo(int index);
    int pop_fifo();
    void push_free(int index);
    int pop_free();
    int steal_fifo();
    int steal_free();

    LockFreeFIFOLink<T> all_links[MAX_LEN];
    struct LockFreeFIFOHead();
};

template<typename T, int MAX_LEN>
LockFreeFIFOHead<T, MAX_LEN>::LockFreeFIFOHead() {
    ABAIndex head;
    head.head = -1;
    head.aba = 0;

    fifo.combined = head.combined;
    for (int i = 0; i < MAX_LEN; ++i) {
        all_links[i].next = i - 1;
    }
    head.head = MAX_LEN - 1;
    ((std::atomic_uint64_t*)&free.combined)->store(head.combined, std::memory_order_seq_cst);
}


template<typename T, int MAX_LEN>
void LockFreeFIFOHead<T, MAX_LEN>::push_fifo(int index)
{
    int32_t head;
    head = ((std::atomic_uint32_t*)&fifo.head)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        all_links[index].next = head.head;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint32_t*)&fifo.head), &head, index);
    } while (!success);
}
template<typename T, int MAX_LEN>
int LockFreeFIFOHead<T, MAX_LEN>::pop_fifo()
{
    int ret;
    ABAIndex head;
    head.combined = ((std::atomic_uint64_t*)&fifo.combined)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        if (head.head == -1) return -1;
        ret = head.head;
        ABAIndex to;
        to.head = all_links[ret].next;
        to.aba = head.aba + 1;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint64_t*)&fifo.combined), &head.combined, to);
    } while (!success);
    return ret;
}
template<typename T, int MAX_LEN>
int LockFreeFIFOHead<T, MAX_LEN>::steal_fifo()
{
    int32_t head = -1;
    head = std::atomic_exchange(((std::atomic_uint32_t*)&fifo.head), head);
    return head;
}

template<typename T, int MAX_LEN>
void LockFreeFIFOHead<T, MAX_LEN>::push_free(int index)
{
    int32_t head;
    head = ((std::atomic_uint32_t*)&free.head)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        all_links[index].next = head.head;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint32_t*)&free.head), &head, index);
    } while (!success);
}
template<typename T, int MAX_LEN>
int LockFreeFIFOHead<T, MAX_LEN>::pop_free()
{
    int ret;
    ABAIndex head;
    head.combined = ((std::atomic_uint64_t*)&free.combined)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        if (head.head == -1) return -1;
        ret = head.head;
        ABAIndex to;
        to.head = all_links[ret].next;
        to.aba = head.aba + 1;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint64_t*)&free.combined), &head.combined, to);
    } while (!success);
    return ret;
}
template<typename T, int MAX_LEN>
int LockFreeFIFOHead<T, MAX_LEN>::steal_free()
{
    int32_t head = -1;
    head = std::atomic_exchange(((std::atomic_uint32_t*)&free.head), head);
    return head;
}


namespace GC {

    /*
    struct GCStateType
    {
        uint8_t threads_not_mutating;
        uint8_t threads_in_collection;
        uint8_t threads_out_of_collection;
        bool collecting : 1;
        bool exit_program : 1;
        bool sweeping : 1;
        unsigned char collection_number : 5;
    };
    */

    struct StateType
    {
        uint8_t threads_not_mutating;
        uint8_t threads_in_collection;
        uint8_t threads_out_of_collection;
        uint8_t threads_acknowledged_sweep;
        bool collecting;
        bool exit_program;
        bool sweeping;
        uint8_t collection_number;
    };

    const int MAX_COLLECTED_THREADS = 256;
    const int MAX_COLLECTION_NUMBER_BITS = 5;

    typedef uint64_t GCStateWhole;
    typedef std::atomic_uint64_t AtomicGCStateWhole;

    union StateStoreType
    {
        StateType state;
        GCStateWhole store;
    };

    StateStoreType State;

    enum class ThreadStateEnum {
        NOT_MUTATING,
        IN_COLLECTION,
        OUT_OF_COLLECTION
    };

    thread_local ThreadStateEnum ThreadState;
    thread_local int NotMutatingCount;
    thread_local int MyThreadNumber;
    thread_local int AcknowledgedSweep;

    std::atomic_uint32_t ThreadsInGC;


    void init_gc()
    {
        State.state.threads_not_mutating = 0;
        State.state.threads_out_of_collection = 0;
        State.state.threads_in_collection = 0;
        State.state.collecting = false;
        State.state.exit_program = false;
        State.state.sweeping = false;
        State.state.collection_number = 0;
        ThreadsInGC.store(0, std::memory_order_seq_cst);
    }


    void _gc_start_collection()
    {}
    //waits until no threads are collecting
    void _gc_end_collection_start_sweep()
    {}
    void _gc_end_sweep()
    {}

    StateStoreType get_gc_state()
    {
        StateStoreType ret;
        ret.store = ((std::atomic_uint32_t*)&State.store)->load(std::memory_order_seq_cst);
        return ret;
    }

    bool compare_set_gc_state(StateStoreType* expected, StateStoreType to)
    {
        return std::atomic_compare_exchange_weak(((AtomicGCStateWhole*)&State.store), &expected->store, to.store);
    }

    //turns out that hazard pointers won't work because we would need a fence to make sure they're visible when we start collecting, and if we need a fence
    //then the collector has to wait for the fences, and if it's waiting for the fences it can just wait for all threads to be IN_COLLECTION and not need the hazard
    //
    //count into collection to start gc or count out of collection to start sweep
    //
    void safe_point()
    {
        StateStoreType gc = get_gc_state();
        StateStoreType to;
        if (ThreadState == ThreadStateEnum::IN_COLLECTION) {
            if (gc.state.collecting == true) return;
            bool success = false;
            do {
                if (gc.state.collecting) return;
                to.state = gc.state;
                to.state.threads_in_collection--;
                to.state.threads_out_of_collection++;

                success = compare_set_gc_state(&gc, to);
            } while (!success);
        }
        if (ThreadState == ThreadStateEnum::OUT_OF_COLLECTION) {
            if (gc.state.collecting != true) return;
            bool success = false;
            do {
                to.state = gc.state;
                if (gc.state.sweeping && !AcknowledgedSweep) {

                    to.state.threads_acknowledged_sweep++;
                    assert(gc.state.collecting);
                    success = compare_set_gc_state(&gc, to);
                    if (success) AcknowledgedSweep = true;
                }
                else {
                    if (gc.state.collecting) return;
                    to.state.threads_in_collection++;
                    to.state.threads_out_of_collection--;
                    if (AcknowledgedSweep) {
                        to.state.threads_acknowledged_sweep--;
                    }
                    success = compare_set_gc_state(&gc, to);
                    if (success) AcknowledgedSweep = false;
                }
            } while (!success);
        }
    }

    void init_thread_gc()
    {
        MyThreadNumber = ThreadsInGC++;
        AcknowledgedSweep = false;
        bool success = false;
        StateStoreType gc = get_gc_state();
        do {
            StateStoreType to;
            to.state = gc.state;
            if (gc.state.collecting) {
                ThreadState = ThreadStateEnum::IN_COLLECTION;
                to.state.threads_in_collection++;
            }
            else
            {
                ThreadState = ThreadStateEnum::OUT_OF_COLLECTION;
                to.state.threads_out_of_collection++;
            }
            success = compare_set_gc_state(&gc, to);
        } while (!success);
        NotMutatingCount = 0;
    }

    void exit_thread_gc()
    {
        bool success = false;
        StateStoreType gc = get_gc_state();
        do {
            StateStoreType to;
            to.state = gc.state;
            if (ThreadState == ThreadStateEnum::IN_COLLECTION) {
                to.state.threads_in_collection--;
            }
            else if (ThreadState == ThreadStateEnum::OUT_OF_COLLECTION)
            {
                to.state.threads_out_of_collection--;
            }
            else {
                to.state.threads_not_mutating--;
            }

            success = compare_set_gc_state(&gc, to);
        } while (!success);
        ThreadsInGC--;
    }

    struct ThreadGCRAII
    {
        ThreadGCRAII() { init_thread_gc(); }
        ~ThreadGCRAII() { exit_thread_gc(); }

    };

    void thread_leave_mutation()
    {
        ++NotMutatingCount;
        if (NotMutatingCount > 1) {
            return;
        }
        bool success = false;
        StateStoreType gc = get_gc_state();
        do {
            StateStoreType to;
            to.state = gc.state;
            if (ThreadState == ThreadStateEnum::IN_COLLECTION) {
                to.state.threads_in_collection--;
            }
            else
            {
                to.state.threads_out_of_collection--;
                if (AcknowledgedSweep) {
                    to.state.threads_acknowledged_sweep--;
                }
            }
            to.state.threads_not_mutating++;
            success = compare_set_gc_state(&gc, to);
        } while (!success);
    }
    void thread_enter_mutation()
    {
        --NotMutatingCount;
        if (NotMutatingCount != 0) {
            return;
        }
        bool success = false;
        StateStoreType gc = get_gc_state();
        do {
            StateStoreType to;
            to.state = gc.state;
            to.state.threads_not_mutating--;
            if (gc.state.collecting) {
                ThreadState = ThreadStateEnum::IN_COLLECTION;
                to.state.threads_in_collection++;
            }
            else
            {
                ThreadState = ThreadStateEnum::OUT_OF_COLLECTION;
                to.state.threads_out_of_collection++;
                if (AcknowledgedSweep) {
                    if (to.state.sweeping) {
                        to.state.threads_acknowledged_sweep++;
                    }
                    else AcknowledgedSweep = false;
                }
            }
            success = compare_set_gc_state(&gc, to);
        } while (!success);
    }

    struct LeaveMutationRAII
    {
        LeaveMutationRAII() { thread_leave_mutation(); }
        ~LeaveMutationRAII() { thread_enter_mutation(); }
    };

    typedef uint32_t CollectableIndex;

    //i1 has the low bits of both, and i2 has the high bits 
    const uint32_t VALUE_TAG_BITS = 1;
    const size_t COLLECTION_SIZE_BITS = 14;
    const size_t COLLECTION_SIZE = (size_t)1 << COLLECTION_SIZE;
    const size_t OUTER_INDEX_SIZE_BITS = 32 - COLLECTION_SIZE_BITS - VALUE_TAG_BITS;
    const size_t OUTER_INDEX_SIZE = (size_t)1 << OUTER_INDEX_SIZE_BITS;

    union CollectblePtr
    {
        struct {
            CollectableIndex i1;
            CollectableIndex i2;
        };
        uint64_t both;
    };

    enum class CollectableTags {
        NotAllocated,
        Cons,

    };
    const int NUM_COLLECTABLE_TAGS = 2;

    enum class ValueTags {
        SmallInt = 1,
        Atom,
        CodePoint
    };

    //any value where the top NUM_COLLECTABLE_TAGS bits aren't 0 is a tagged value. 
    inline bool is_value_tagged(CollectableIndex p)
    {
        if (NUM_COLLECTABLE_TAGS == 0)
            return false;
        //next line will cause a warning if NUM_COLLECTABLE_TAGS is zero, but will never execute in that case
        return ((((int32_t)-1) << (32 - NUM_COLLECTABLE_TAGS)) & p) != 0;
    }
    inline int32_t extract_tagged_int(CollectableIndex p)
    {
        return (((int32_t)p) << NUM_COLLECTABLE_TAGS) >> NUM_COLLECTABLE_TAGS;
    }
    inline uint32_t extract_tagged_unsigned(CollectableIndex p)
    {
        return p & (((uint32_t)0xffffffff) >> NUM_COLLECTABLE_TAGS);
    }

    CollectableIndex generate_index(uint32_t block, uint32_t within_block)
    {
        return (block << OUTER_INDEX_SIZE_BITS) | within_block;
    }
    uint32_t extract_block(CollectableIndex i)
    {
        return (uint32_t)i >> OUTER_INDEX_SIZE_BITS;
    }
    uint32_t extract_block_offset(CollectableIndex i)
    {
        return i & ((uint32_t)0xffffffff >> OUTER_INDEX_SIZE_BITS);
    }

    struct Collectable {
        //when not tracing contains self index
        //when tracing points back to where we came from or 0 if that was a root
        //when in a free list points to the next free element as an unbiased index into this block
        CollectableIndex back_ptr_and_free_ptr;
        CollectableIndex me;
        uint32_t back_ptr_from_counter;//came from nth snapshot ptr
        uint8_t collection_number;
        std::atomic_bool marked;
        virtual int num_ptrs_in_snapshot() = 0;
        virtual CollectableIndex index_into_snapshot_ptrs(int num) = 0;
        //not snapshot, includes ones that could be null because they're live
        virtual int total_collectable_ptrs() = 0;
        virtual size_t my_size() = 0;
        virtual CollectblePtr* index_into_collectable_ptrs(int num) = 0;
        CollectableTags type_tag();
        Collectable* block_ptr();
        inline Collectable* next_in_block()
        {
            CollectableIndex next = me + 1;
            if (0 == extract_block_offset(next)) return nullptr;
            return (Collectable*)((uint8_t)this + my_size());
        }
        virtual void Collected() = 0; //destroy when collected
        virtual void Allocated() = 0; //initialize when allocated
        virtual ~Collectable() {}
    };

    struct BlockOwnershipManager {
        CollectableTags GCBlockTags[OUTER_INDEX_SIZE];
        Collectable* GCBlocks[OUTER_INDEX_SIZE];

        //255 for not allocated
        //254 for not owned
        uint8_t ThreadOwnershipOfBlock[OUTER_INDEX_SIZE];

        uint32_t ThreadOwnershipDSkiplist[MAX_COLLECTED_THREADS * OUTER_INDEX_SIZE * 2];
        uint32_t ThreadOwnershipDSkiplistStart[MAX_COLLECTED_THREADS];
        std::mutex BlockAllocationMutex;


    };

    BlockOwnershipManager BlockOwnership;

    inline Collectable* Collectable::block_ptr() {
        return BlockOwnership.GCBlocks[extract_block(me)];
    }
    inline CollectableTags Collectable::type_tag() {
        return BlockOwnership.GCBlockTags[extract_block(me)];
    }
}//end namespace GC
 
int main()
{

    std::cout << "Hello World!\n";
}

