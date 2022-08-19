#pragma once
#include <stdint.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <assert.h>
#include <chrono>
#include <random>
#include <signal.h>

#include "LockFreeLIFO.h"


#define ENSURE(x) assert(x)

#ifdef NDEBUG
#define MEM_TEST()
#else
#define MEM_TEST memtest
#endif

class Collectable;
class CollectableSentinel;

extern CollectableSentinel CollectableNull;

#define collectable_null ((Collectable*)&CollectableNull)
//#define collectable_null nullptr
namespace GC {
    typedef uint32_t Handle;
    const Handle EndOfHandleFreeList = 0xffffffff;
    
    const int MAX_COLLECTED_THREADS = 256;

    union HandleType
    {
        Collectable* ptr;
        Handle list;
    };
    const int HandleBlocks = 8192;
    extern LockFreeLIFO<Handle, HandleBlocks + MAX_COLLECTED_THREADS + 1> HandleBlockQueue;
    extern LockFreeLIFO<Handle, MAX_COLLECTED_THREADS * 10000> ReleaseHandlesQueue;
    const int HandlesPerBlock = 16384;
    const int TotalHandles = HandlesPerBlock * 8192;//about 134 million
  
    extern Handle HandleList[MAX_COLLECTED_THREADS];
    extern HandleType Handles[TotalHandles];

    extern int unqueued_handles;
    extern int prev_unqueued_handle;

    extern thread_local int MyThreadNumber;

    int GrabHandleList();

    inline Handle AllocateHandle()
    {
        Handle next = HandleList[MyThreadNumber];
        if (next == EndOfHandleFreeList) next = GrabHandleList();
        HandleList[MyThreadNumber] = Handles[next].list;
        return next;
    }

    inline void DeallocHandleInGC(int pos)
    {
        ++unqueued_handles;
        Handles[pos].list = prev_unqueued_handle;
        prev_unqueued_handle = pos;
        if (unqueued_handles == HandlesPerBlock) {
            int queue_pos = HandleBlockQueue.pop_free();
            HandleBlockQueue.all_links[queue_pos].data = pos;
            HandleBlockQueue.push_fifo(queue_pos);
            prev_unqueued_handle = EndOfHandleFreeList;
            unqueued_handles = 0;
        }
    }

    const Handle NULLHandle = 0;//set the first handle to the nullptr

   
    void log_alloc(size_t a);
    void log_array_alloc(size_t a, size_t n);

    union SnapPtr {
        Handle     handles[2];
        uint64_t   combined;
    };


    inline void double_ptr_store(SnapPtr* dest, Handle v)
    {
        SnapPtr temp;
        temp.handles[1] = temp.handles[0] = v;
        reinterpret_cast<std::atomic_uint64_t*>(&dest->combined)->store(temp.combined, std::memory_order_relaxed);
    }

    inline void single_ptr_store(SnapPtr* dest, Handle v)
    {
        dest->handles[0] = v;
    }
    inline Handle load(const SnapPtr* dest)
    {
        return dest->handles[0];
    }
    inline Handle load_snapshot(const SnapPtr* dest)
    {
        return dest->handles[1];
    }

    inline void fast_restore(SnapPtr* source)
    {
        if (source == nullptr) return;
        SnapPtr temp;
        temp.combined = source->combined;
        if (temp.handles[0] != temp.handles[1])source->handles[1] = temp.handles[0];

    }
    inline void restore(SnapPtr* source)
    {
        if (source == nullptr) return;
        SnapPtr temp, desired;
        temp.combined = source->combined;
        do {
            if (temp.handles[0] == temp.handles[1]) {
                return;
            }
            
            desired.handles[0] = desired.handles[1] = temp.handles[0];
        } while (!reinterpret_cast<std::atomic_uint64_t*>(&source->combined)->compare_exchange_weak(temp.combined, desired.combined,std::memory_order_seq_cst));
    }

    extern thread_local void (*write_barrier)(SnapPtr*, Handle);

    enum class PhaseEnum : std::uint8_t
    {
        NOT_MUTATING,
        NOT_COLLECTING,
        COLLECTING,
        RESTORING_SNAPSHOT,
        EXIT
    };
    struct StateType
    {
        uint8_t threads_not_mutating;
        uint8_t threads_in_collection;
        uint8_t threads_in_sweep;
        uint8_t threads_out_of_collection;
        PhaseEnum phase;
    };
 

    typedef uint64_t GCStateWhole;
    typedef std::atomic_uint64_t AtomicGCStateWhole;

    union StateStoreType
    {
        StateType state;
        GCStateWhole store;
    };

    extern StateStoreType State;

    extern thread_local PhaseEnum ThreadState;
    extern thread_local int NotMutatingCount;
    extern //thread_local 
        bool CombinedThread;




//#define cnew_array(A,N) ([&]{ auto _NfjkasjdflN_ = N; auto _AskdlfA_=new A[_NfjkasjdflN_];  GC::log_array_alloc(_AskdlfA_[0]->my_size(),_NfjkasjdflN_); GC::Handle _lskdfjKJK_ = GC::AllocateHandle(); GC::Handles[_lskdfjKJK_].ptr =  _AskdlfA_; return _lskdfjKJK_; })()

    extern std::atomic_uint32_t ThreadsInGC;
    
    void exit_collect_thread();
    void init(bool combine_thread=false);
    void _start_collection();
    //waits until no threads are collecting
    void _end_collection_start_sweep();
    void _end_sweep();
    StateStoreType get_state();
    bool compare_set_state(StateStoreType* expected, StateStoreType to);
    void safe_point();
    void init_thread(bool combine_thread=false);
    void exit_thread();
    struct ThreadRAII
    {
        ThreadRAII() { if (!CombinedThread) init_thread(); }
        ~ThreadRAII() { if (!CombinedThread) exit_thread(); }
    };
    void thread_leave_mutation();
    void thread_enter_mutation(bool from_init_thread=false);
    struct LeaveMutationRAII
    {
        LeaveMutationRAII() { thread_leave_mutation(); }
        ~LeaveMutationRAII() { thread_enter_mutation(); }
    };

    struct EnterMutationRAII
    {
        ~EnterMutationRAII() { thread_leave_mutation(); }
        EnterMutationRAII() { thread_enter_mutation(); }
    };

}
