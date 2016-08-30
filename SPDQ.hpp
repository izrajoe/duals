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



#ifndef SPDUAL_H
#define SPDUAL_H

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif

#include <stdint.h>
#include <stdbool.h>
#include <list>
#include <atomic>
#include "RDualContainer.hpp"
#include "BlockPool.hpp"


// single polarity dual ring queue
class SPDQ: public virtual RDualContainer{

	const static int _RING_SIZE = 2048; // TODO make nonstatic
	const static int _STARVATION = 2; // TODO make nonstatic
	const static int LOCK_FREE_DWELL_TIME=1; // TODO make nonstatic

	// location struct with flags
	struct idx_struct{
		union {
			// we use ui so that we can read the entire struct
			// in a single read
			// and we can compare and swap the entire thing easily
			// (by CASing ui)
			volatile uint32_t ui;  
			#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			// order depends on endianess because we must fai ui
			// (we can't fai on a bit array)
			struct {
				volatile uint32_t idx: 29;  
				// had to get rid of this union because it didn't seem to function properly
				// closed and safe are always equal
				//union { 
				volatile uint32_t ready : 1;    // for lock free
				volatile uint32_t closed : 1;  // flag for head or tail
				volatile uint32_t safe : 1;    // flag for regular nodes
				//};
			};
			#else
			struct {
				//union {
				volatile uint32_t closed : 1;  
				volatile uint32_t safe : 1;    
				volatile uint32_t ready : 1; 
				//};
				uint32_t idx: 29;
			};
			#endif
		};

		// We need to define == for CAS
		bool operator==(const idx_struct  &x)
		{
			//return x.closed == closed && x.t == t;
			return ui==x.ui;
		}

		// need atomic access to flags
		#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		void inline close(){
			uint32_t u;
			//printf("index %d\n",idx);
			__sync_fetch_and_or(&ui,0b01000000000000000000000000000000);
			//closed = 1;
			//printf("closed %d\n",closed);
			//printf("index %d\n",idx);
		}
		#else
		void inline close(){
			assert(false);
			//closed = 1;
			//__sync_fetch_and_or(&ui,0b00000000000000000000000000000011);
		}
		#endif
	};


	// The node struct is an entry in the queue
public:
	struct Node{
		union{
			//__int128 ui;  // gonna need this type for 64 bit indexing, but it's not supported on my machine
			volatile uint64_t ui; 
			struct{
				struct idx_struct loc;			// location struct, contains index and flag
				volatile int32_t val;  					//value of node
			};
			// the struct is padded to cache line size in paper, 
			// we can fill the rest with junk
			// and it won't affect the CAS because we can just use ui

		
		};
		//pad to cache line size
		char pad[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)];

		// We need to define == for CAS
		bool operator==(const Node  &x)
		{
			return ui==x.ui;
		}

		void inline initNode(uint32_t safe_closed, uint32_t ready, uint32_t idx, uint32_t val){
			this->loc.safe=safe_closed;
			this->loc.closed=safe_closed;
			this->loc.ready=ready;
			this->loc.idx=idx;
			this->val=val;
		}

		// need atomic access to flags
		bool inline set_ready(uint32_t i){
			struct Node oldnode;
			struct Node newnode;
			oldnode.initNode(loc.safe,0,i,val);
			newnode.initNode(loc.safe,1,i,val);
			return __sync_bool_compare_and_swap (&(this->ui), oldnode.ui, newnode.ui);
		}


	};

	// struct of circular ring queue
	// it makes up one entry of the 
	// linked circular ring queue

	// based on
	// implementation of the Linked Circular Ring Queue
	// "Fast Concurrent Queues for x86 Processors"
	// Adam Morrison and Yehuda Afek
	// 2013
	// http://www.cs.tau.ac.il/~adamx/ppopp2013-x86queues.pdf

	class DCRQ{
	public:
		struct idx_struct head;			// the head index in ring
		char pad1[LEVEL1_DCACHE_LINESIZE-sizeof(struct idx_struct)]; // padding to cache line size

		struct idx_struct tail;		//the tail index in ring
		char pad2[LEVEL1_DCACHE_LINESIZE-sizeof(struct idx_struct)]; // padding to cache line size

		struct DCRQ* next;		//next: pointer to next DCRQ in linked list, initially null
		char pad3[LEVEL1_DCACHE_LINESIZE-sizeof(struct DCRQ*)]; // padding to cache line size

		uint64_t index;
		char pad4[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)]; // padding to cache line size

		bool antidata;
		char pad5[LEVEL1_DCACHE_LINESIZE-sizeof(bool)]; // padding to cache line size

		struct DCRQ* prev;
		char pad6[LEVEL1_DCACHE_LINESIZE-sizeof(struct DCRQ*)]; // padding to cache line size	

		struct Node ring[SPDQ::_RING_SIZE];		//ring: array of nodes, initially node (1,u,null)

		bool lock_free;

		bool sealed;

	private:
		void fixstate();
		int emptycheck(const struct idx_struct h);
		int32_t dequeue_normal(bool antidata, int32_t arg);
		int32_t dequeue_lock_free(bool antidata, int32_t arg);

	public:
		void inline initNode(Node* n, uint32_t safe_closed, uint32_t ready, uint32_t idx, uint32_t val);
		void initRingQueue(uint64_t index, bool antidata, bool lock_free);
		bool seal();
		int32_t enqueue(bool antidata, int32_t arg);
		int32_t dequeue(bool antidata, int32_t arg);
	};

	// counted pointer (TODO replace with my counted pointer)
	struct DCRQ_ptr{
		union{
			//__int128 ui;  // gonna need this type for 64 bit indexing, but it's not supported on my machine
			volatile uint64_t ui;// TODO: deal with atomic reads
			struct{
				volatile uint32_t cntr;			// location struct, contains index and flag // TODO: deal with atomic reads
				struct DCRQ* ptr;  					//value of node
			};
		};
		//struct DCRQ* ptr;  					//value of node
		// the struct is padded to cache line size in paper, 
		// we can fill the rest with junk
		// and it won't affect the CAS because we can just use ui
		//pad to cache line size
		char pad[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)];

		// We need to define == for CAS
		bool operator==(const Node  &x)
		{
			return ui==x.ui;
		}
	};

	// wait structure
	class DCRQ_wait{
	public:
		static const uint64_t IS_SAT = ((uint64_t)1)<<32;
		std::atomic<uint64_t> ui;

		DCRQ_wait& operator=(const DCRQ_wait& x){
			ui.store(x.ui);
			return *this;
		}

		void set(uint32_t val, uint32_t sat){
			uint64_t u;
			u = val;
			if(sat){u=u|IS_SAT;}
			ui.store(u, std::memory_order::memory_order_seq_cst);
		}

		bool satisfy(uint32_t old_val, uint32_t val){
			uint64_t u;
			u = val;
			u=u|IS_SAT;

			uint64_t old_ui = (uint64_t)old_val;

			return ui.compare_exchange_strong(old_ui,u, std::memory_order::memory_order_seq_cst);
		}

		bool is_sat(){
			return (ui.load() & IS_SAT)!=((uint64_t)0);
		}

		uint32_t val(){
			return ui.load() & 0x00000000ffffffff;
		}

		void wipe(){
			set(0,1);
			//atomic_thread_fence(std::memory_order_seq_cst);
			//ui.store(0, std::memory_order::memory_order_seq_cst);
			//atomic_thread_fence(std::memory_order_seq_cst);
		}


	};



private:
	int32_t _dequeue(DCRQ_ptr h, bool antidata, int32_t arg, int tid);
	int32_t _enqueue(DCRQ_ptr h, bool antidata, int32_t arg, int tid);
	bool swingHead(DCRQ_ptr head, int tid);
	bool appendRing(DCRQ_ptr prev, DCRQ_ptr next);
public:
	DCRQ_ptr head; // the head DCRQ in the linked list
	DCRQ_ptr tail; // the tail DCRQ in the linked list

	volatile uint64_t head_index; // TODO: deal with atomic reads
	char pad[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)];
	
	struct padded<DCRQ_wait>* waiters;

	struct volatile_padded<uint64_t>* hazard;
	struct volatile_padded<std::list< DCRQ*>*>* retired;
	int task_num;
	bool lock_free;
	BlockPool<DCRQ>* bp;

	SPDQ(int t_num, bool glibc_mem,bool lock_free);
	~SPDQ();

	int32_t remove(int tid);
	void insert(int32_t arg, int tid);
	void retire(int tid, struct DCRQ* dcrq);

};


class SPDQFactory : public RContainerFactory{

	bool nonblocking;

public:
	SPDQFactory(bool nonblocking){
		this->nonblocking = nonblocking;
	}

	SPDQ* build(GlobalTestConfig* gtc){
		return new SPDQ(gtc->task_num, gtc->environment["glibc"]=="1",nonblocking);
	}

};

#endif
