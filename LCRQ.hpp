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



#ifndef RING_QUEUE_H
#define RING_QUEUE_H

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif

#include <stdint.h>
#include <stdbool.h>
#include <list>
#include "ConcurrentPrimitives.hpp"
#include "BlockPool.hpp"
#include "RContainer.hpp"

#define RING_SIZE 2048
#define STARVATION 2


// implementation of the Linked Circular Ring Queue
// "Fast Concurrent Queues for x86 Processors"
// Adam Morrison and Yehuda Afek
// 2013
// http://www.cs.tau.ac.il/~adamx/ppopp2013-x86queues.pdf


// location struct with 2 flags for 
// keeping track of valid/invalid 
// indices
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
			volatile uint32_t idx: 30;  
			// had to get rid of this union because it didn't seem to function properly
			// closed and safe are always equal
			//union { 
			volatile uint32_t closed : 1;  // flag for head or tail
			volatile uint32_t safe : 1;    // flag for regular nodes
			//};
		};
		#else
		struct {
			//union {
			volatile uint32_t closed : 1;  
			volatile uint32_t safe : 1;    
			//};
			uint32_t idx: 30;
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
		closed = 1;
		//__sync_fetch_and_or(&ui,0b00000000000000000000000000000011);
	}
	#endif
};


// The node struct is an entry in the queue
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
	//char pad[1];



	// We need to define == for CAS
	bool operator==(const Node  &x)
	{
		return ui==x.ui;
	}
};

// struct of circular ring queue
// it makes up one entry of the 
// linked circular ring queue
struct CRQ{
	struct idx_struct head;			// the head index in ring
	char pad1[LEVEL1_DCACHE_LINESIZE-sizeof(struct idx_struct)]; // padding to cache line size

	struct idx_struct tail;		//the tail index in ring
	char pad2[LEVEL1_DCACHE_LINESIZE-sizeof(struct idx_struct)]; // padding to cache line size

	struct CRQ* next;		//next: pointer to next CRQ in linked list, initially null
	char pad3[LEVEL1_DCACHE_LINESIZE-sizeof(struct CRQ*)]; // padding to cache line size

	uint64_t index;
	char pad4[LEVEL1_DCACHE_LINESIZE-sizeof(uint64_t)]; // padding to cache line size

	struct Node ring[RING_SIZE];		//ring: array of nodes, initially node (1,u,null)
};

struct CRQ_ptr{
	union{
		//__int128 ui;  // gonna need this type for 64 bit indexing, but it's not supported on my machine
		volatile uint64_t ui;// TODO: deal with atomic reads
		struct{
			volatile uint32_t cntr;			// location struct, contains index and flag // TODO: deal with atomic reads
			struct CRQ* ptr;  					//value of node
		};
	};
	//struct CRQ* ptr;  					//value of node
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

void initRingQueue(struct CRQ* crq);
void initRingQueue(struct CRQ* crq,uint64_t index);
void inline initNode(struct Node* n, uint32_t safe_closed, uint32_t idx, uint32_t val);
int32_t crqdequeue(struct CRQ* crq);
int32_t crqenqueue(struct CRQ* crq, int32_t arg);

// linked circular ring queue
class LCRQ: public virtual RQueue, public Reportable{
public:
	CRQ_ptr head; // the head CRQ in the linked list
	CRQ_ptr tail; // the tail CRQ in the linked list

	volatile uint64_t head_index; // TODO: deal with atomic reads
	struct volatile_padded<uint64_t>* hazard;
	struct volatile_padded<std::list<struct CRQ*>*>* retired;
	int task_num;
	BlockPool<struct CRQ>* bp;

//public:
	LCRQ(int task_num);
	LCRQ(int task_num, bool glibc_mem);
	~LCRQ();

	int32_t dequeue(int tid);
	void enqueue(int32_t arg, int tid);
	int32_t verify();
	void retire(int tid, struct CRQ* crq);

	void conclude(){
		int i = 0;
		while(this->remove(i%task_num)!=EMPTY){
			i++;
		}
		std::cout<<"size@End="<<i<<std::endl;
	}

};


class LCRQFactory : public RContainerFactory{
	LCRQ* build(GlobalTestConfig* gtc){
		return new LCRQ(gtc->task_num,gtc->environment["glibc"]=="1");
	}
};

#endif
