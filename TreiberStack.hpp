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



// Treiber's stack.
//
// See:
//   R. K. Treiber. Systems programming: Coping with parallelism.
//   Technical report RJ 5118. IBM Almaden Research Center. April 1986.
// 


#ifndef TREIBER_STACK_H
#define TREIBER_STACK_H

#include "RDualContainer.hpp"
#include "BlockPool.hpp"

class TreiberStack : public virtual RPeekableContainer, public virtual RStack, public Reportable{

	class Node {
		public:
		Node* down;
		int32_t val;
		int64_t size;
		//uint8_t pad1[CACHE_LINE_SIZE-(sizeof(cptr<Node>)-sizeof(int32_t))]; //pad
		void init(int32_t v, int32_t sz, Node* d){size = sz; down=d;val=v;}
	};//__attribute__(( aligned(CACHE_LINE_SIZE) ));

	cptr<Node> top
 	__attribute__(( aligned(CACHE_LINE_SIZE) )); uint8_t pad1[CACHE_LINE_SIZE-sizeof(cptr<Node>)]; //pad

	BlockPool<Node>* bp;
	int task_num;
	std::atomic<int> maxSize;
	Node* dummy; // for valgrind stupidity (it seems to be confused by cptr's)

	public:
	TreiberStack(int task_num, bool glibc_mem);

	void conclude();

	void push(int32_t e,int tid);
	int32_t pop(int tid);
	void insert(int32_t e,int tid){return push(e,tid);}
	int32_t remove(int tid){return pop(tid);}
	KeyVal peek(int tid);
	bool remove_cond(uint64_t key, int tid);

};

class TreiberStackFactory : public RContainerFactory{
	TreiberStack* build(GlobalTestConfig* gtc){
		return new TreiberStack(gtc->task_num,gtc->environment["glibc"]=="1");
	}
};

#endif


