/*

Copyright 2015 University of Rochester

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 

*/



#ifndef GENERIC_DUAL_H
#define GENERIC_DUAL_H

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif

#include "ConcurrentPrimitives.hpp"
#include "RDualContainer.hpp"
#include "BlockPool.hpp"
#include <list>
#include <atomic>
#include <vector>

#define FLAG_MASK 0x0000000300000000
#define REQ_MASK 0xfffffffc00000000

#define ABORTED (0x100000000)
#define VALID (0x200000000)
#define SATISFIED (0x300000000)
#define INVALID (0x000000000)

class GenericDual : public virtual RDualContainer, public Reportable{

private:

	class placeholder_local{

	public:
		uint64_t all;


		void inline init(int32_t val, uint64_t state){
			uint64_t a;
			a = 0;
			a = (uint32_t)val;
			a += state;
			all=a;
		}

		void inline init(int32_t val, uint64_t state, void* req){
			uint64_t a;
			a = 0;
			a = (uint32_t)req;
			assert((a&3)==0);// check bit alignment
			a = a << 32;
			a += (uint32_t)val;
			a += state;
			all=a;
		}

		void inline init(uint64_t a){
			all = a;
		}

		int32_t inline val(){return (int32_t)(all&0x00000000ffffffff);}
		uint64_t inline state(){return (all&FLAG_MASK);}
		inline void* req(){return (void*)((all&REQ_MASK)>>0);}

		bool inline valid(){
			uint64_t a = (all&FLAG_MASK);
			return a == VALID || a == SATISFIED;
		}	
		bool inline aborted(){return (all&FLAG_MASK) == ABORTED;}	
		bool inline sat(){return (all&FLAG_MASK) == SATISFIED;}	

	};

	class placeholder{

	public:
		std::atomic<uint64_t> all;
		std::atomic<bool> abandoned;	
		//pad to cache line size
		//char pad[LEVEL1_DCACHE_LINESIZE-(sizeof(std::atomic<uint64_t>)+sizeof(std::atomic<bool>))];


		void inline init(int32_t val, uint64_t state){
			uint64_t a;
			a = 0;
			a = (uint32_t)val;
			a += state;
			all=a;
			abandoned.store(false,std::memory_order::memory_order_relaxed);
			all.store(a,std::memory_order::memory_order_release);
		}
		int32_t inline val(){return (int32_t)(all&0x00000000ffffffff);}
		bool inline valid(){
			uint64_t a = (all&FLAG_MASK);
			return a == VALID || a == SATISFIED;
		}	
		bool inline aborted(){return (all&FLAG_MASK) == ABORTED;}	
		bool inline sat(){return (all&FLAG_MASK) == SATISFIED;}	
		uint64_t inline state(){return (all&FLAG_MASK);}
		inline void* req(){return (void*)((all&REQ_MASK)>>32);}


		bool inline satisfy(int32_t arg){
			return satisfy(arg,NULL);
		}

		bool inline satisfy(int32_t arg, void* req){
			placeholder_local oldval;
			oldval.init((int32_t)NULL,VALID);

			placeholder_local newval;
			newval.init(arg,SATISFIED,req);
			return all.compare_exchange_strong(oldval.all,newval.all);
		}

		bool inline CAS(placeholder_local& oldval, placeholder_local& newval){	
			return all.compare_exchange_strong(oldval.all,newval.all);
		}

		bool inline abandon(){
			bool res = abandoned.load(std::memory_order::memory_order_acquire);
			bool f = false;
			res = (!res) && abandoned.compare_exchange_strong(f,true);
			return res;
		}

	};


	class Request{
	public:
		std::atomic<int32_t> val;
		std::atomic<placeholder*> ph;
		std::atomic<uint64_t> key;
		void init(int32_t val, placeholder* ph, uint64_t key){
			this->val.store(val,std::memory_order::memory_order_relaxed);
			this->ph.store(ph,std::memory_order::memory_order_relaxed);
			this->key.store(key,std::memory_order::memory_order_release);
		}
	};

	RContainer* q_array[2];
	RPeekableContainer* antiContainer;
	cptr<Request> activeRequest;

	int task_num;
	bool nonBlocking;
	

	inline int32_t finished_insert(placeholder* ph, bool polarity,int tid);
	inline int32_t mix(placeholder* ph, placeholder* opp_ph,bool polarity,int tid);
	inline int32_t remsert(int32_t val,bool polarity,int tid);
	inline void contention_manager(bool polarity,int tid);


	placeholder* allocPlaceholder(int32_t val, int tid);
	inline int32_t validateAndComplete(placeholder* ph, int32_t val, bool polarity, int tid);
	inline int32_t doOppositeCheck(placeholder* ph, bool polarity, bool nb, int tid);
	inline int32_t oppositeCheck(placeholder* ph, bool polarity, bool nb, int tid);
	inline int32_t oppositeCheckNB(placeholder* ph, bool polarity, bool nb, int tid);
	inline uint64_t helpRequestNB(cptr_local<Request> req, int tid);


	inline void reserveHazard(Request* req, int tid);
	inline void reserveHazard(placeholder* ph, int tid);
	inline void clearHazards(int tid);
	inline void retire(placeholder* ph, int tid);		
	const int RETIREMENT_LIMIT=0;
	BlockPool<placeholder>* bp;
	BlockPool<Request>* bpReq;
	HazardTracker* hazPh;
	HazardTracker* hazReq;

public:
	
	int32_t remove(int tid);
	void insert(int32_t val,int tid);
	GenericDual(RContainer* dataqueue,RContainer* antidataqueue, bool nonblocking, int task_num, bool glibc_mem);
	~GenericDual();
	void conclude();


};


class GenericDualFactory : public RContainerFactory{
	RContainerFactory* dataContainer;
	RContainerFactory* antiContainer;
	bool nonblocking;
public:
	GenericDualFactory(RContainerFactory* dataContainer, RContainerFactory* antiContainer, bool nonblocking){
		this->dataContainer = dataContainer;
		this->antiContainer = antiContainer;
		this->nonblocking = nonblocking;
	}

	GenericDual* build(GlobalTestConfig* gtc){
		return new GenericDual(dataContainer->build(gtc), antiContainer->build(gtc), 
			nonblocking, gtc->task_num, gtc->environment["glibc"]=="1");
	}
	
	~GenericDualFactory(){
		delete dataContainer;
		delete antiContainer;
	}

};


#endif
























