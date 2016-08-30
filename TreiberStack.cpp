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


#include <list>
#include <iostream>
#include "RContainer.hpp"
#include "TreiberStack.hpp"

using namespace std;

// TODO runs out of memeory on potato test...

TreiberStack::TreiberStack(int task_num, bool glibc_mem){
	top.init(NULL,0);
	bp = new BlockPool<Node>(task_num,glibc_mem);
	this->task_num = task_num;

	// preheat block pools
	std::list<Node*> v;
	for(int i=0;i<task_num;i++){
		for(int j=0; j<25000; j++){
			//v.push_back((Node*)bp->alloc(i));
		}
	}
	for(int i=0;i<task_num;i++){
		for(int j=0; j<25000; j++){
			//bp->free(v.front(),i);
			//v.pop_front();
		}
	}

	this->maxSize = 0;
}

void TreiberStack::conclude(){
	//dummy = top.ptr();
	cout<<"maxSize="<<maxSize<<endl;
	cout<<"dummy="<<dummy<<endl;
	int i = 0;
	while(this->remove(0)!=EMPTY){
		i++;
	}
	cout<<"size@End="<<i<<endl;
}
void TreiberStack::push(int32_t val,int tid) {
	Node* newNode;
	cptr_local<Node> topCopy; 
	newNode = bp->alloc(tid);
	newNode->init(val,0,NULL);
	int size=0;
	while(true) {
		topCopy.init(top.all()); // read top pointer
		//newNode->down.storePtr(top.ptr()); // set newNode's down to top
		newNode->down = top.ptr();
		if(newNode->down!=NULL){
			size = newNode->down->size+1;
			newNode->size = size;
		}
		else{size=1;newNode->size = size;}
		if(top.CAS(topCopy,newNode)){// swing top
			while(true){
				int oldSize = maxSize.load();
				if(oldSize>=size){break;}
				if(maxSize.compare_exchange_strong(oldSize,size)){break;}
			}
			return; // finished if succeed
		} 
	}
}

int32_t TreiberStack::pop(int tid) {
	cptr_local<Node> topCopy;
	Node* newTop;

	while(true){
		topCopy.init(top.all()); // read top pointer
		if (topCopy.ptr()==NULL) { // check if empty
			return EMPTY;
		} 
		else {
			newTop = topCopy->down; // get new top
			int topSize = topCopy->size;
			int newSize = 0;
			if(newTop!=NULL){newSize = newTop->size;}
			if(top.CAS(topCopy,newTop)){// swing top
				assert((newTop==NULL && topSize==1)|| (newTop!=NULL && topSize-1==newSize));
				int32_t val = topCopy->val;
				bp->free(topCopy.ptr(),tid);
				return val; // finished if succeed
			}
		}
	}
}

KeyVal TreiberStack::peek(int tid){
	cptr_local<Node> topCopy;
	KeyVal kv;
	do{
		topCopy.init(top.all()); // read top pointer
		kv.key = topCopy.all();
		if(topCopy.ptr()!=NULL){kv.val = topCopy->val;}
		else{kv.val=EMPTY;}
	}while(kv.key!=top.all());
	return kv;
}

bool TreiberStack::remove_cond(uint64_t key, int tid){
	cptr_local<Node> topCopy;
	Node* newTop;
	topCopy.init(key);
	if(topCopy.all()!=top.all()){return false;} // precheck for failure
	assert(topCopy.ptr()!=NULL);

	newTop = topCopy->down; // get new top
	if(top.CAS(topCopy,newTop)){// swing top
		int32_t val = topCopy->val;
		bp->free(topCopy.ptr(),tid);
		return true; // finished if succeed
	}
	return false;
}



