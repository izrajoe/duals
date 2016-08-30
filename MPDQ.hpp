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



#ifndef MPDQ_H
#define MPDQ_H

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif

#include <stdint.h>
#include <stdbool.h>
#include <list>
#include "RDualContainer.hpp"
#include "BlockPool.hpp"
#include <atomic>

#define DRQ_RING_SIZE 2048 
#define DRQ_STARVATION 2

#define DRQ_EMPTY 5


// location struct with 2 flags for 
// keeping track of valid/invalid 
// indices
class drq_idx{
public:
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
			volatile uint32_t antidata : 1;  // flag for data vs antidata
			volatile uint32_t closed : 1;  // flag for head or tail
			volatile uint32_t safe : 1;    // flag for regular nodes
			//};
		};
		#else
		struct {
			//union {
			volatile uint32_t antidata : 1;  // flag for data vs antidata
			volatile uint32_t closed : 1;  
			volatile uint32_t safe : 1;    
			//};
			uint32_t idx: 29;
		};
		#endif
	};

	// We need to define == for CAS
	bool operator==(const drq_idx  &x)
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
		//printf("l%d\n",this->closed);
		//closed = 1;
		//printf("closed %d\n",closed);
		//printf("index %d\n",idx);
	}
	#else
	void inline close(){
		closed = 1;
		//__sync_fetch_and_or(&ui,0b00100000000000000000000000000000);
		//__sync_fetch_and_or(&ui,0b00000000000000000000000000000011);
	}
	#endif
};

// wait structure
class drq_wait{
public:
	static const uint64_t IS_SAT = ((uint64_t)1)<<32;
	std::atomic<uint64_t> ui;

	drq_wait() : ui(0){
	}

	drq_wait& operator=(const drq_wait& x){
		ui.store(x.ui);
		return *this;
	}

	void set(uint32_t val, uint32_t sat){
		uint64_t u;
		u = val;
		if(sat){u=u|IS_SAT;}
		ui.store(u);
				//atomic_signal_fence(std::memory_order::memory_order_seq_cst);
				//__sync_synchronize();
	}

	bool satisfy(uint32_t old_val, uint32_t val){
		uint64_t u;
		u = val;
		u=u|IS_SAT;

		uint64_t old_ui = (uint64_t)old_val;

		return ui.compare_exchange_strong(old_ui,u);
	}

	bool is_sat(){
		return (ui.load() & IS_SAT)!=((uint64_t)0);
	}

	uint32_t val(){
		return ui.load() & 0x00000000ffffffff;
	}

	void wipe(){
		set(0,1);
	}
};

// The node struct is an entry in the queue
class drq_node{
public:
	union{
		volatile uint64_t ui; 
		struct{
			drq_idx loc;			// location struct, contains index and flag
			volatile int32_t val;  	//value of node
		};
	};
	//pad to cache line size
	uint8_t pad[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)];
};

// struct of circular ring queue
// it makes up one entry of the 
// linked circular ring queue
class DRQ{
public:
	drq_idx data_idx;
	char pad1[LEVEL1_DCACHE_LINESIZE-sizeof(drq_idx)]; // padding to cache line size

	drq_idx antidata_idx;
	char pad2[LEVEL1_DCACHE_LINESIZE-sizeof(drq_idx)]; // padding to cache line size

	drq_idx closedInfo;
	char pad3[LEVEL1_DCACHE_LINESIZE-sizeof(drq_idx)]; // padding to cache line size	

	struct DRQ* next;		//next: pointer to next CRQ in linked list, initially null
	char pad4[LEVEL1_DCACHE_LINESIZE-sizeof(struct DRQ*)]; // padding to cache line size

	uint64_t index;
	char pad5[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)]; // padding to cache line size

	uint32_t abandoned;
	char pad6[LEVEL1_DCACHE_LINESIZE-sizeof(uint32_t)]; // padding to cache line size

	drq_node ring[DRQ_RING_SIZE];		//ring: array of nodes, initially node (1,u,null)
};

class DRQ_ptr{
public:
	union{
		//__int128 ui;  // gonna need this type for 64 bit indexing, but it's not supported on my machine
		volatile uint64_t ui;// TODO: deal with atomic reads
		struct{
			volatile uint32_t cntr;			// location struct, contains index and flag // TODO: deal with atomic reads
			DRQ* ptr;  					//value of node
		};
	};
	//struct CRQ* ptr;  					//value of node
	// the struct is padded to cache line size in paper, 
	// we can fill the rest with junk
	// and it won't affect the CAS because we can just use ui
	//pad to cache line size
	char pad[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)];

	// We need to define == for CAS
	bool operator==(const drq_node  &x)
	{
		return ui==x.ui;
	}
};

void initDRQ(DRQ* drq);
void initDRQ(DRQ* drq,uint64_t index);
void inline initDRQNode(drq_node* n, uint32_t safe_closed, uint32_t idx, uint32_t val, bool antidata){
	n->loc.antidata=antidata;
	n->loc.safe=safe_closed;
	n->loc.closed=safe_closed;
	n->loc.idx=idx;
	n->val=val;
}
int32_t drqdequeue(DRQ* drq);
int32_t drqenqueue(DRQ* drq, int32_t arg);
int32_t drqdenqueue(DRQ* drq, int32_t arg, bool polarity);



// linked circular ring queue
class MPDQ: public RDualContainer{
public:
	DRQ_ptr data_head; // the head CRQ in the linked list
	DRQ_ptr antidata_head; // the tail CRQ in the linked list
	DRQ* hotdrq;
	bool lock_free;

	padded<drq_wait>* waiters;

	volatile uint64_t head_index;
	volatile_padded<uint64_t>* hazard;
	volatile_padded<std::list<struct DRQ*>*>* retired;
	int task_num;
	BlockPool<DRQ>* bp;
	int32_t denqueue(int32_t arg, bool polarity, int tid);
	void retire(int tid, struct DRQ* crq);


//public:
	MPDQ(int task_num, bool glibc_mem, bool lock_free);
	~MPDQ();

	
	int32_t remove(int tid);
	void insert(int32_t arg, int tid);

};


class MPDQFactory : public RContainerFactory{

	bool nonblocking;

public:
	MPDQFactory(bool nonblocking){
		this->nonblocking = nonblocking;
	}

	MPDQ* build(GlobalTestConfig* gtc){
		return new MPDQ(gtc->task_num, gtc->environment["glibc"]=="1",nonblocking);
	}

};


#endif
