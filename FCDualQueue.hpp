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



#ifndef __FCDUALQUEUE_H__
#define __FCDUALQUEUE_H__


#include <atomic>
#include <algorithm>
#include "ConcurrentPrimitives.hpp"
#include "RDualContainer.hpp"
#include "BlockPool.hpp"
#include <vector>
#include <list>
#include <forward_list>
#include <deque>
#include "SimpleRing.hpp"




class FCDualQueue : public virtual RDualContainer, public Reportable {

public:
    // Used for each thread to register their request
	class ThreadNode{
	private:
		static const uint64_t IS_CONSUMER = 0x0000000100000000;
		static const uint64_t IS_VAL = 0x0000000200000000;
	public:

	std::atomic<uint64_t> ui;

	// padding
	char pad1[LEVEL1_DCACHE_LINESIZE
	-sizeof(std::atomic<uint64_t>)
	-sizeof(std::atomic<int>)
	];

	ThreadNode(){
		set(false,false,0);
	}

	ThreadNode(bool is_consumer, bool is_val, int32_t item){
		set(is_consumer,is_val,item);
	}

	void inline set(bool is_consumer, bool is_val, int32_t item){
		uint64_t u = 0;
		u += (uint32_t)(item);
		if(is_consumer){u = u|IS_CONSUMER;}
		if(is_val){u = u|IS_VAL;}
		ui.store(u,std::memory_order::memory_order_release);
	}

	bool inline is_consumer(){
		return (ui.load(std::memory_order::memory_order_acquire) & IS_CONSUMER) !=0;
	}
	bool inline is_val(){
		return (ui.load(std::memory_order::memory_order_acquire) & IS_VAL) !=0;
	}
	int32_t inline item(){
		return (int32_t)(ui.load(std::memory_order::memory_order_acquire) & 0x00000000ffffffff);
	}
	};

	// Flat Combining Lock
	std::atomic<int> fc_lock;
	char pad1[LEVEL1_DCACHE_LINESIZE-sizeof(std::atomic<int>)];

	// Main data structure
	std::list<int32_t> main_ds;
	char pad2[LEVEL1_DCACHE_LINESIZE-sizeof(std::list<int32_t>)];

	// Thread request array
	ThreadNode* thread_requests;
	char pad3[LEVEL1_DCACHE_LINESIZE-sizeof(ThreadNode*)];

	// Thread local request caches
	padded<SimpleRing<ThreadNode*>>* consumers_caches; // TODO switch back to deque, test

	// number of threads
	int task_num = 1;

	// Combining constants
	const int MAX_COMBINING_ROUNDS = 10;
	const int COMBINING_LIST_CHECK_FREQUENCY = 10;

	// constructor
	FCDualQueue(int task_num, bool glibc);

	// Synchronous Queue interface's put routine.
	// Only get requests become combiners (reduces contention on lock)
	void insert(int32_t value,int tid);

	// Synchronous Queue interface's get routine.
	int32_t remove(int tid);

private:
    // Actual combining routine
    void doFlatCombining(int tid);

};

class FCDualQueueFactory : public RContainerFactory{
	FCDualQueue* build(GlobalTestConfig* gtc){
		return new FCDualQueue(gtc->task_num, gtc->environment["glibc"]=="1");
	}
};

#endif

