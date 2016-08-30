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



#include "LCRQ.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <list>
#define __STDC_LIMIT_MACROS
#include <stdint.h>

#define NULL_VAL 0
#define CLOSED 0
#define VERIFY_FAIL 0

// 1 is true
// 0 is false

void initRingQueue(struct CRQ* crq, uint64_t index){
	int i;
	struct idx_struct loc;

	crq->head.idx=0;
	crq->head.closed=0;
	crq->head.ui = 0; // unnecessary, but prevents valgrind errors
	crq->tail.closed=0;
	crq->tail.idx=0;
	crq->tail.ui = 0;// unnecessary, but prevents valgrind errors
	crq->next=NULL;
	crq->index=index;
	for(i=0;i<RING_SIZE;i++){
		initNode(&crq->ring[i],1,i,NULL_VAL);
	}
}

void initRingQueue(struct CRQ* crq){
	initRingQueue(crq,0);
}

void inline initNode(struct Node* n, uint32_t safe_closed, uint32_t idx, uint32_t val){
	n->loc.safe=safe_closed;
	n->loc.closed=safe_closed;
	n->loc.idx=idx;
	n->val=val;
}

bool seal(struct CRQ* crq){
	struct idx_struct h;
	struct idx_struct t;
	h.ui = crq->head.ui;
	t.ui = crq->tail.ui;
	if(t.closed == 1){
		if(h.idx>=t.idx){
			return true;
		}
	}
	while(true){
		//h.ui = __sync_fetch_and_add (&crq->head.ui, 0);	// not sure why we need fai here
		//t.ui = __sync_fetch_and_add (&crq->tail.ui, 0); // or here, they do it in the paper
		h.ui = crq->head.ui;
		t.ui = crq->tail.ui;

		if(h.idx<t.idx){
			return false;  // then queue is not empty, so return
		}
		h.closed=1;  // close the queue
		if(__sync_bool_compare_and_swap (&crq->tail.ui, t.ui, h.ui)){ 
			return true;  // moved tail to head (queue has size zero), but is consistent
		}
	}
}

// in this method we are worried that tail<head,
// which is an inconsistent state
// in that case, we close the queue
void fixstate(struct CRQ* crq){
	struct idx_struct h;
	struct idx_struct t;

	while(true){
		//h.ui = __sync_fetch_and_add (&crq->head.ui, 0);	// not sure why we need fai here
		//t.ui = __sync_fetch_and_add (&crq->tail.ui, 0); // or here, they do it in the paper
		// Because the concern is that the read is not atomic.  However, all updates to the 
		// these values are atomic, so it seems we can read them regularly.
		h.ui = crq->head.ui;
		t.ui = crq->tail.ui;

		if(crq->tail.ui!=t.ui){ // paper compares entire tail struct..., this check seems irrelevant
			continue;// since if there is a race, this check does nothing // this prechecks for CAS 
		}
		if(h.idx<=t.idx){
			return;  // then queue is consistent, so return
		}

		h.closed=t.closed;  // h.closed is never used, so we make sure it's the same as the tail
		if(__sync_bool_compare_and_swap (&crq->tail.ui, t.ui, h.ui)){ 
			return;  // moved tail to head (queue has size zero), but is consistent
		}
		return;
	}
}



int dequeuefailed(struct CRQ* crq, const struct idx_struct h){
	struct idx_struct t;
	//t.ui = __sync_fetch_and_add (&crq->tail.ui,0);
	t.ui = crq->tail.ui;
	if( t.idx<= h.idx+1){
		fixstate ( crq );
		return EMPTY;
	}
	else{
		return OK;
	}
}

int32_t crqdequeue(struct CRQ* crq){
	// local variables
	uint32_t idx; 		//val , idx : 64 bit int
	int32_t val;
	struct idx_struct h;			//h, t : 64 bit int
	struct idx_struct t;
	struct idx_struct loc;
	struct Node* node;			//node : pointer to Node
	struct Node node_contents;
	char closed; 				//closed : boolean
	char safe;					//safe : boolean
	uint32_t R = RING_SIZE;

	// local nodes used for CAS swapping
	struct Node localnodecopy;
	struct Node emptynode;
	struct Node unsafenode;
	


	while(true){

		// empty state optimization
		if(crq->tail.idx<=crq->head.idx){
			fixstate(crq);
			return EMPTY;
		}

		h.ui = __sync_fetch_and_add (&crq->head.ui, 1);
		node = &crq->ring[h.idx%R];

		assert(h.idx<107374182);  // abort on overflow

		while(true){
			node_contents.ui = __sync_fetch_and_add (&node->ui,0);
			val = node_contents.val;
			loc.ui = node_contents.loc.ui;
			safe = loc.safe;
			idx = loc.idx;	

			if(idx>h.idx){
				if(dequeuefailed(crq,h)==EMPTY){
					return EMPTY;
				}
				else{
					break;  // tried to set queue to empty, but couldn't because it isn't
							// so I need to recheck the head node
				}
			}
			if(val!=NULL_VAL){ // check if node is empty node
				if(idx==h.idx){	// try dequeue transition
					initNode(&localnodecopy,safe,h.idx,val); // h == idx at this point
					initNode(&emptynode,safe,h.idx+R,NULL_VAL);
					if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){	// this cas could clobber one data pt
						assert(val!=EMPTY);
						assert(val!=NULL_VAL);
						return val;
					}
				}	
				else{ // not my node, mark node unsafe to prevent an enqueue here for me
					initNode(&unsafenode,0,idx,val); // this is typo in paper.  They say to use h instead of idx
					initNode(&localnodecopy,safe,idx,val); 
					if(__sync_bool_compare_and_swap(&(node->ui),localnodecopy.ui,unsafenode.ui)){							
						if(dequeuefailed(crq,h)==EMPTY){
							return EMPTY;
							
						}
						else{
							break;  // tried to set queue to empty, but couldn't because it isn't
									// so I need to recheck the head node
						}
					}
					// else{ we took no action this iteration, so continue}
				}			
			}


			else{	// idx <h and val ==NULL, try to empty the queue
				initNode(&localnodecopy,safe,idx,NULL_VAL);
				initNode(&emptynode,safe,h.idx+R,NULL_VAL);
				if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){
					if(dequeuefailed(crq,h)==EMPTY){
						return EMPTY;
					}
					else{
						break;  // tried to set queue to empty, but couldn't because it isn't
								// so I need to recheck the head node
					}
				}
			}   

		}// end inner while loop

	}// end outer while loop

}// end dequeue

int32_t crqenqueue(struct CRQ* crq, int32_t arg){
	int32_t val;
	uint32_t idx;
	struct idx_struct h;
	struct idx_struct t;
	struct idx_struct loc;
	struct Node* node;			//node : pointer to tail node
	struct Node node_contents;
	uint32_t R = RING_SIZE;

	struct Node localnodecopy;
	struct Node newnode;

	long starvation_level = 0;

	if(arg==0){
		printf("invalid enqueue argument (==0)\n");
		abort();
	}

	while(true){

		t.ui = __sync_fetch_and_add (&crq->tail.ui, 1);
		if(t.closed!=0){
			return CLOSED;
		}

		if(t.idx>107374182){  // abort on overflow
			abort();
		}

		node = &(crq->ring[t.idx%R]);  // read current tail
		node_contents.ui = __sync_fetch_and_add (&node->ui,0);//__sync_fetch_and_add (&(node->ui),0);  // TODO: switch from volatile //one 64 bit read
		val = node_contents.val;
		loc.ui = node_contents.loc.ui;
		
		if(loc.safe==0){
			//printf("found unsafe\n");
		}

		if(val==NULL_VAL){ // tail is empty, so we can try enqueue transition
			initNode(&localnodecopy,loc.safe,loc.idx,val);
			initNode(&newnode,1,t.idx,arg);
			if(loc.idx<=t.idx && (loc.safe==1 || crq->head.idx<=t.idx)){
				if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, newnode.ui)){  // enqueue
					assert(node->val == arg || node->loc.idx>t.idx);
					assert(node->val == arg || (crq->head.idx>t.idx));
					return OK;
				}
			}
		}
		// else, our copy of the tail index was stale, so we try again

		h = crq->head;
		// if we find ourselves overlapping head, we close the queue
		// everyone who discovers this closes the queue
		if((t.idx>=h.idx+R) || starvation_level>=STARVATION){ 
			crq->tail.close();
			return CLOSED; // we've closed this ring because it's full.
		}
		starvation_level++;
	}
}


LCRQ::LCRQ(int t_num){
	LCRQ(t_num,false);
}

LCRQ::LCRQ(int t_num, bool glibc_mem){
	int i,j;
	// init block pool
	bp = new BlockPool<struct CRQ>(t_num,glibc_mem);
	std::list<struct CRQ*> v;
	/*for(i=0;i<task_num;i++){
		for(j=0; j<100000; j++){
			v.push_back((struct CRQ*)bp->alloc(i));
		}
	}
	for(i=0;i<task_num;i++){
		for(j=0; j<100000; j++){
			bp->free(v.front(),i);
			v.pop_front();
		}
	}*/


	head.ptr = (struct CRQ*)bp->alloc(0); //(malloc(sizeof(struct CRQ));
	head.cntr=0;
	initRingQueue(head.ptr,0);
	head_index = 0;
	tail=head;
	tail.cntr = 10;
	hazard = new struct volatile_padded<uint64_t>[t_num];
	task_num = t_num;
	retired = new struct volatile_padded<std::list<struct CRQ*>*>[t_num];
	for(i=0;i<task_num;i++){
		retired[i].ui=new std::list<struct CRQ*>();
		hazard[i].ui=UINT64_MAX;
	}

}

LCRQ::~LCRQ(){
	int i;
	struct CRQ* garbage;
	struct CRQ* next_crq;
	/*for(i=0;i<task_num;i++){
		while(retired[i]->size()>0){
			garbage = *retired[i]->begin();
			retired[i]->pop_front();
			free(garbage);
		}
	}
	for(i=0;i<task_num;i++){
		delete retired[i];
	}
	garbage = head.ptr;
	while(garbage!=NULL){
		next_crq = garbage->next;
		free(garbage);
		garbage = next_crq;
	}*/
	//while(this->dequeue()!=EMPTY){}
	delete[] retired;
	delete[] hazard;
}

void LCRQ::retire(int tid, struct CRQ* crq){
	int i;
	uint64_t min_hazard;
	min_hazard = UINT64_MAX;
	long idx;
	struct CRQ old;
	struct CRQ* garbage;
	old = *crq;
	for(i=0;i<task_num;i++){
		if(hazard[i].ui<min_hazard){
			min_hazard=hazard[i].ui;
		}
	}

	if(min_hazard>crq->index){
		// crq is already clear,
		// we can free it
		bp->free(crq,tid);
	}
	else{
		// crq is not clear
		// append it to the retired list
		retired[tid].ui->push_back(crq);
		
	}

	// while we're here, lets empty our retired list
	// anything on our list is unique,
	// since it got there by us successfully removing it from
	// the head pointer
	while(retired[tid].ui->size()>0 && (*retired[tid].ui->begin())->index<min_hazard){

		garbage = *retired[tid].ui->begin();
		idx = garbage->index;
		retired[tid].ui->pop_front(); 
		bp->free(garbage,tid);
	}

}

int32_t LCRQ::dequeue(int tid){
	// local variables
	CRQ_ptr crq;
	CRQ_ptr crq_next;
	int32_t v;
	uint64_t haz;


	while(true){
		hazard[tid].ui= head_index; // get head_index, it is guaranteed to be 
								// less than or equal to the actual head index
								// we set it as our hazard index.  Nothing
								// above it can be freed
		crq = head;

		v = crqdequeue(crq.ptr);
		if(v!= EMPTY){
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			return v;  // dequeued successfully, return
		} 
		if(crq.ptr->next==NULL){
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			return EMPTY; // queue is totally empty, return
		} 
		if(!seal(crq.ptr)){
			continue;
		}
		// SOMEONE ENQUEUES NOW IN OLD AND NEW!!!!!!
		crq_next.ptr = crq.ptr->next;
		crq_next.cntr = crq.cntr+1;
		if(__sync_bool_compare_and_swap(&head.ui, crq.ui,crq_next.ui)){ // this crq is empty, try the next and loop

			// Our CAS was successful, we might be able to garbage collect the old queue
			// however, if several threads are behind trying to dequeue at once, the 
			// one in the back can't catch up, and will be accessing freed memory on
			// crq->next, which may get reallocated....

			// so we need to verify we are the last person to be looking at head
			// Morrison and Afek use hazard pointers.  

			// I use a variation in hazard pointers that looks at the head_index.
			// the head_index is guaranteed to be no larger than the actual head
			// index.  Thus, so long as everyone's hazard index is above a crq's index
			// we can free it
			__sync_fetch_and_add (&head_index, 1);  // update head index
			hazard[tid].ui=UINT64_MAX; // this line breaks things (does it still?)
			retire(tid,crq.ptr);
		}
	}

}

void LCRQ::enqueue(int32_t arg, int tid){
	// local variables
	CRQ_ptr crq;
	CRQ_ptr crq_next;
	CRQ_ptr newcrq;

	newcrq.ptr=NULL;
	while(true){ 
		hazard[tid].ui=head_index; ; // get head_index, it is guaranteed to be 
								// less than or equal to the actual head and tail index
								// we set it as our hazard index.  Nothing
								// above it can be freed
		crq.ui = tail.ui;
		if(crq.ptr->next!=NULL){
			// tail wasn't actually the tail, try the next one and loop
			crq_next.ptr = crq.ptr->next;
			crq_next.cntr = crq.cntr+1;
			__sync_bool_compare_and_swap (&tail.ui, crq.ui,crq_next.ui); 
			continue;
		}
		if(crqenqueue(crq.ptr,arg)==OK){ // successfully enqueued
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			if(newcrq.ptr!=NULL){
				bp->free(newcrq.ptr,tid);
			} 
			return;
			//return (int32_t)crq.ptr;
		}
		// else, the tail is closed
		// we need to make a new tail
		// and enqueue the arg onto it
		if(newcrq.ptr==NULL){
			newcrq.ptr=(struct CRQ*)bp->alloc(tid);
			if(newcrq.ptr==NULL){// we ran out of memory...
				fprintf(stderr,"Out of memory on CRQ alloc!\n");
				abort();
			}
			initRingQueue(newcrq.ptr,0);
			if(crqenqueue(newcrq.ptr, arg)!=OK){
				bp->free(newcrq.ptr,tid);
				//puts("b");
				continue;
			}

		}
		newcrq.ptr->index = crq.ptr->index+1; // TODO: write after write issue?
		newcrq.cntr = crq.cntr+1; // TODO: write after write issue?
		if(__sync_bool_compare_and_swap (&(crq.ptr->next), NULL,newcrq.ptr)){//add new tail to list
			__sync_bool_compare_and_swap (&tail.ui, crq.ui,newcrq.ui); // update tail pointer
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			return;
			//return (int32_t)newcrq.ptr;
		}
	}

}

bool verifyCRQ(struct CRQ* crq){
	int i;
	int nodecount=0;
	bool finished = false;

	if(crq->head.idx>crq->tail.idx){
		fprintf(stderr,"FAILED CRQ INVARIANT: head.idx greater than tail.idx\n");
		abort();
	}
	if(crq->tail.idx-crq->head.idx > RING_SIZE && crq->tail.closed!=1){
		fprintf(stderr,"FAILED CRQ INVARIANT: tail.idx-head.idx >= RING_SIZE and queue still open\n");
		abort();
	}
	for(i=crq->head.idx; i<crq->head.idx+RING_SIZE; i++){
		if(crq->ring[i%RING_SIZE].val==NULL_VAL && finished==false){
			finished=true;
		}
		else if(crq->ring[i%RING_SIZE].val!=NULL_VAL && finished == true){
			fprintf(stderr,"FAILED CRQ INVARIANT: Found valid node after end of queue\n");
			abort();
		}
		else if(crq->ring[i%RING_SIZE].val!=NULL_VAL && finished == false){
			nodecount++;
		}
	}

	//printf("nodecount: %d\n",nodecount);

	return finished;

}

int32_t LCRQ::verify(){
		// verify queue
		struct CRQ* crq;

		unsigned long crqcount=0;
		bool finished = false;

		//puts("\nVERIFYING QUEUE\n");

		// iterate over entire queue (could be inifinite....)
		crq=head.ptr;
		while(crq!=NULL){
			crqcount++;
			//printf("\ncrqcount: %ld\n",crqcount);

			finished = verifyCRQ(crq);
			if(finished == true && crq!=tail.ptr){
				fprintf(stderr,"FAILED LCRQ INVARIANT: Tail pointer does not point to last queue\n");
				abort();
			}
			crq=crq->next;
			if(finished == true && crq!=NULL){
				fprintf(stderr,"FAILED LCRQ INVARIANT: Found another CRQ after tail\n");
				abort();
			}
			
		}
		
		return OK;
}




			/*if(val!=NULL_VAL){ // check if node is empty node
				if(idx==h.idx){	// try dequeue transition
					initNode(&localnodecopy,safe,h.idx,val); // h == idx at this point
					initNode(&emptynode,safe,h.idx+R,NULL_VAL);
					if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){	
						return val;
					}
					else{ // CAS failed, mark node unsafe to prevent further enqueue
						initNode(&unsafenode,0,h.idx,val);
						if(__sync_bool_compare_and_swap(&(node->ui),localnodecopy.ui,unsafenode.ui)){
							if(dequeuefailed(crq,h)==EMPTY){
								return EMPTY;
								
							}
							else{
								break;  // tried to set queue to empty, but couldn't because it isn't
										// so I need to recheck the head node
							}
						}
						// else{ we took no action this iteration, so continue}
					}
				}	
				// else {we took no action this iteration , so continue}			
			}*/



















