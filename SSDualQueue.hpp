/* Dualqueue: a non-blocking queue that spins on dequeue operations
   until data is available. Based heavily on an implementation of the
   Michael and Scott lock-free queue algorithm written by Michael
   Scott.

   Written by Bill Scherer
   Copyright (c) 2003, Bill Scherer

	Updated to use C++ 11 atomics by Joe Izraelevitz
	2014
*/

#ifndef __SSDUALQUEUE_H__
#define __SSDUALQUEUE_H__

#include <stdlib.h>
#include <assert.h>

#include "BlockPool.hpp"
#include "RDualContainer.hpp"
#include "ConcurrentPrimitives.hpp"
//#include "atomic_ops.h"
#include <unistd.h>
#include <list>


template <class T>
class cnt_ptr_local{
	public:
	uint64_t all;
	void init(T* ptr, uint32_t sn, bool is_request, bool is_fulfilled){
		uint64_t a;
		a = 0;
		a = sn&0x3fffffff;
		a = a<<32;
		a += (uint32_t)ptr;
		if(is_request){a=a|0x8000000000000000;}
		if(is_fulfilled){a=a|0x4000000000000000;}
		all=a;
	}
	T operator *(){return *this->ptr();}
	T* ptr(){return (T*)(all&0x00000000ffffffff);}
	bool is_request(){
		return (all & 0x8000000000000000)!=0;
	}
	bool is_fulfilled(){
		return (all & 0x4000000000000000)!=0;
	}
	uint32_t sn(){return (all&0x3fffffff00000000) >>32;}	


};

// TODO: reimplement with overlapping atomic unions
template <class T>
class cnt_ptr{
public:
		std::atomic<uint64_t> all;

		void init(T* ptr, uint32_t sn, bool is_request, bool is_fulfilled){
			uint64_t a;
			a = 0;
			a = sn&0x3fffffff;
			a = a<<32;
			a += (uint32_t)ptr;
			if(is_request){a=a|0x8000000000000000;}
			if(is_fulfilled){a=a|0x4000000000000000;}
			all.store(a);
		}
		T operator *(){return *this->ptr();}
		T* ptr(){return (T*)(all.load()&0x00000000ffffffff);}
		bool is_request(){
			return (all.load() & 0x8000000000000000)!=0;
		}
		bool is_fulfilled(){
			return (all.load() & 0x4000000000000000)!=0;
		}
		uint32_t sn(){return (all.load()&0x3fffffff00000000) >> 32;}	

		bool CAS(cnt_ptr_local<T> &oldval,T* newval){
			cnt_ptr_local<T> replacement;
			replacement.init(newval,oldval.sn()+1,oldval.is_request(),oldval.is_fulfilled());
			uint64_t old= oldval.all;
			return all.compare_exchange_strong(old,replacement.all);
		}
		bool CAS(cnt_ptr_local<T> &oldval,cnt_ptr_local<T> &newval){
			cnt_ptr_local<T> replacement;
			replacement.init(newval.ptr(),oldval.sn()+1,newval.is_request(),newval.is_fulfilled());
			uint64_t old= oldval.all.load();
			return all.compare_exchange_strong(old,replacement.all);
		}
		bool CAS(cnt_ptr<T> &oldval,T* newval){
			cnt_ptr_local<T> replacement;
			replacement.init(newval,oldval.sn()+1,oldval.is_request(),oldval.is_fulfilled());
			uint64_t old= oldval.all.load();
			return all.compare_exchange_strong(old,replacement.all);
		}
		bool CAS(cnt_ptr<T> &oldval,cnt_ptr_local<T> &newval){
			cnt_ptr_local<T> replacement;
			replacement.init(newval.ptr(),oldval.sn()+1,newval.is_request(),newval.is_fulfilled());
			uint64_t old= oldval.all.load();
			return all.compare_exchange_strong(old,replacement.all);
		}


};

/* queue node representation */
typedef struct dqnode_t
{
    uint32_t data;
	char pad1[LEVEL1_DCACHE_LINESIZE-sizeof(uint32_t)];
    cnt_ptr<struct dqnode_t> request;
	char pad2[LEVEL1_DCACHE_LINESIZE-sizeof(cnt_ptr<struct dqnode_t>)];
    cnt_ptr<struct dqnode_t> next;
	char pad3[LEVEL1_DCACHE_LINESIZE-sizeof(cnt_ptr<struct dqnode_t>)];
} 
dqnode_t;



/* interface */
class SSDualQueue : public RDualContainer{
public:
	cnt_ptr<dqnode_t> head;
	char pad1[LEVEL1_DCACHE_LINESIZE-sizeof(cnt_ptr<dqnode_t>)];
    cnt_ptr<dqnode_t> tail;
	char pad2[LEVEL1_DCACHE_LINESIZE-sizeof(cnt_ptr<dqnode_t>)];
    BlockPool<struct dqnode_t>* bp;
	char pad3[LEVEL1_DCACHE_LINESIZE-sizeof(BlockPool<struct dqnode_t>*)];
	SSDualQueue(int t_num, bool glibc_mem);
	void insert(int32_t val, int tid);
	int32_t remove(int tid);
};

class SSDualQueueFactory : public RContainerFactory{
	SSDualQueue* build(GlobalTestConfig* gtc){
		return new SSDualQueue(gtc->task_num, gtc->environment["glibc"]=="1");
	}
};

#endif /* __DUALQUEUE_H__ */
