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

// NB: On Windows, you must include Winbase.h/Synchapi.h/Windows.h before pevents.h
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 
#include <Windows.h>
//#include <afx.h>
#endif
//A library to simulate Windows events on Posix
//I use this because, on Windows, the events don't need a mutex, so the calls should be more efficient than using condition variables etc - no possible pause caused by contention over the mutex.
#include "pevents/pevents.h"

#ifdef _WIN32
#define __unused__  [[maybe_unused]]
#else
#define __unused__ __attribute__((unused))
#endif

#if defined(_MSC_VER)
/* Microsoft C/C++-compatible compiler */
#include <intrin.h>

#elif defined(__x86_64__)
#include <x86intrin.h>
#endif
//#include "LockFreeFIFO.h"

#define ENSURE(x) assert(x)

class Collectable;
class CollectableSentinel;

//extern CollectableSentinel CollectableNull;

//#define collectable_null ((Collectable*)&CollectableNull)
#define collectable_null nullptr
namespace GC {
    typedef uint32_t Handle;
    const Handle EndOfHandleFreeList = 0xffffffff;

    void log_alloc(size_t a);
    void log_array_alloc(size_t a, size_t n);

    typedef union SnapPtr {
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

    const int MAX_COLLECTED_THREADS = 10;
 

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
    extern thread_local int MyThreadNumber;
    extern thread_local bool CombinedThread;

    const int TotalHandles = 100000000;

    const int HandlesPerThread = TotalHandles / MAX_COLLECTED_THREADS;


    const Handle NULLHandle = 0;//set the first handle to the nullptr

    extern Handle HandleList[MAX_COLLECTED_THREADS];
    extern Handle GCHandleListHead[MAX_COLLECTED_THREADS];
    extern Handle GCHandleListTail[MAX_COLLECTED_THREADS];

   
    union HandleType
    {
        Collectable* ptr;
        Handle list;
    };

    extern HandleType Handles[TotalHandles];

    inline void init_before_gc()
    {
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i)
        {
            GCHandleListTail[i] = GCHandleListHead[i] = EndOfHandleFreeList;
        }
    }

    inline void DeallocHandleInGC(Handle l)
    {
        int thread = l / HandlesPerThread;

        Handles[l].list = GCHandleListHead[thread];
        if (GCHandleListTail[thread] == EndOfHandleFreeList) GCHandleListTail[thread] = l;

        GCHandleListHead[thread] = l;
    }

    inline void MergeHandleList(int thread) {
        if (GCHandleListTail[thread] != EndOfHandleFreeList)
        {
            Handles[GCHandleListTail[thread]].list = HandleList[thread];
            HandleList[thread] = GCHandleListHead[thread];
        }
    }

    inline void MergeAllHandleLists()
    {
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            MergeHandleList(i);
        }
    }
   
    inline Handle AllocateHandle()
    {
        Handle next = HandleList[MyThreadNumber];
        HandleList[MyThreadNumber] = Handles[next].list;
        return next;
    }


#define cnew(A) ([&]{ auto * _AskdlfA_=new A;  GC::log_alloc(_AskdlfA_->my_size()); GC::Handle _lskdfjKJK_ = GC::AllocateHandle(); if (GC::EndOfHandleFreeList==_lskdfjKJK_) abort(); GC::Handles[_lskdfjKJK_].ptr =  _AskdlfA_; _AskdlfA_->myHandle = _lskdfjKJK_; return _AskdlfA_; })()
#define cnew2template(A,B) ([&]{ auto * _AskdlfA_=new A,B;  GC::log_alloc(_AskdlfA_->my_size()); GC::Handle _lskdfjKJK_ = GC::AllocateHandle(); if (GC::EndOfHandleFreeList==_lskdfjKJK_) abort(); GC::Handles[_lskdfjKJK_].ptr =  _AskdlfA_; _AskdlfA_->myHandle = _lskdfjKJK_; return _AskdlfA_; })()
#define cnew3template(A,B,C) ([&]{ auto * _AskdlfA_=new A,B,C;  GC::log_alloc(_AskdlfA_->my_size()); GC::Handle _lskdfjKJK_ = GC::AllocateHandle(); if (GC::EndOfHandleFreeList==_lskdfjKJK_) abort(); GC::Handles[_lskdfjKJK_].ptr =  _AskdlfA_; _AskdlfA_->myHandle = _lskdfjKJK_; return _AskdlfA_; })()
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
        ThreadRAII() { init_thread(); }
        ~ThreadRAII() { exit_thread(); }
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
