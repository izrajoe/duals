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



#include "SPDQ.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <list>
#define __STDC_LIMIT_MACROS
#include <stdint.h>

// 1 is true
// 0 is false

void SPDQ::DCRQ::initRingQueue(uint64_t index, bool antidata, bool lock_free){
	int i=0;
	struct idx_struct loc;

	this->head.idx=0;
	this->head.closed=0;
	this->head.ui = 0; // unnecessary, but prevents valgrind errors
	this->tail.closed=0;
	this->tail.idx=0;
	this->tail.ui = 0;// unnecessary, but prevents valgrind errors
	this->next=NULL;
	this->index=index;
	this->antidata = antidata;
	this->lock_free = lock_free;
	this->sealed = false;

	if(lock_free && this->antidata){
		initNode(&this->ring[i],1,1,0,NULL_VAL);
		for(i=1;i<SPDQ::_RING_SIZE;i++){
			initNode(&this->ring[i],1,0,i,NULL_VAL);
		}
	}
	else{
		for(i=0;i<SPDQ::_RING_SIZE;i++){
			initNode(&this->ring[i],1,1,i,NULL_VAL);
		}
	}


}

void inline SPDQ::DCRQ::initNode(Node* n, uint32_t safe_closed, uint32_t ready, uint32_t idx, uint32_t val){
	n->initNode(safe_closed,ready,idx,val);
}


bool SPDQ::DCRQ::seal(){
	struct idx_struct h;
	struct idx_struct t;
	h.ui = this->head.ui;
	t.ui = this->tail.ui;
	if(t.closed == 1){
		if(h.idx>=t.idx){
			sealed = true;
			return true;
		}
	}
	while(true){
		if(sealed==true){
			return true;
		}
		//h.ui = __sync_fetch_and_add (&dcrq->head.ui, 0);	// not sure why we need fai here
		//t.ui = __sync_fetch_and_add (&dcrq->tail.ui, 0); // or here, they do it in the paper
		h.ui = this->head.ui;
		t.ui = this->tail.ui;

		if(h.idx<t.idx && sealed == false){
			return false;  // then queue is not empty, so return
		}
		h.closed=1;  // close the queue
		if(__sync_bool_compare_and_swap (&this->tail.ui, t.ui, h.ui)){ 
			sealed = true;
			return true;  // moved tail to head (queue has size zero), but is consistent
		}
	}
}

// in this method we are worried that tail<head,
// which is an inconsistent state
// in that case, we close the queue
void SPDQ::DCRQ::fixstate(){
	struct idx_struct h;
	struct idx_struct t;

	while(true){
		//h.ui = __sync_fetch_and_add (&dcrq->head.ui, 0);	// not sure why we need fai here
		//t.ui = __sync_fetch_and_add (&dcrq->tail.ui, 0); // or here, they do it in the paper
		// Because the concern is that the read is not atomic.  However, all updates to the 
		// these values are atomic, so it seems we can read them regularly.
		h.ui = this->head.ui;
		t.ui = this->tail.ui;

		if(this->tail.ui!=t.ui){ // paper compares entire tail struct..., this check seems irrelevant
			continue;// since if there is a race, this check does nothing // this prechecks for CAS 
		}
		if(h.idx<=t.idx){
			return;  // then queue is consistent, so return
		}

		h.closed=t.closed;  // h.closed is never used, so we make sure it's the same as the tail
		if(__sync_bool_compare_and_swap (&this->tail.ui, t.ui, h.ui)){ 
			return;  // moved tail to head (queue has size zero), but is consistent
		}
		return;
	}
}



int SPDQ::DCRQ::emptycheck(const struct idx_struct h){
	struct idx_struct t;
	//t.ui = __sync_fetch_and_add (&dcrq->tail.ui,0);
	t.ui = this->tail.ui;
	if( t.idx<= h.idx+1){
		fixstate ( );
		return EMPTY;
	}
	else{
		return OK;
	}
}


int32_t SPDQ::DCRQ::dequeue_lock_free(bool antidata, int32_t arg){

	// local variables
	struct idx_struct h;			//h, t : 64 bit int
	struct Node node_contents;	
	struct Node* node;
	uint32_t R = SPDQ::_RING_SIZE;
	struct idx_struct t;
	struct idx_struct loc;
	char closed; 				//closed : boolean
	char safe;					//safe : boolean

	// local nodes used for CAS swapping
	struct Node localnodecopy;
	struct Node emptynode;
	struct Node unsafenode;

	uint32_t idx; 		//val , idx : 64 bit int
	uint32_t ready;
	int32_t val;

	int rdy_after = (lock_free && this->antidata)?0:1;	

	assert(antidata!=this->antidata);


	h.ui = __sync_fetch_and_add (&this->head.ui, 1);

	bool paused = false;

	while(true){
		assert(h.idx<107374182); 
		node = &this->ring[h.idx%R];
		node_contents.ui = __sync_fetch_and_add (&node->ui,0);//node->ui;

		// reread node contents for next iteration
		val = node_contents.val;
		loc.ui = node_contents.loc.ui;
		safe = loc.safe;
		idx = loc.idx;	
		ready = loc.ready;

		// FIND THE WAVE FRONT ------
		// move up because we're behind the ready front
		if(idx > h.idx){
			h.idx++;
			continue;
		}

		// we're too far ahead and have lapped
		if(idx < h.idx){
			assert(h.idx>=R);
			h.idx = (h.idx-R)+1;
			continue;
		}

		// now we know we're equal

		// if not ready, this means either we're just on the wave front
		// or ahead of it
		if(!ready){
			if(h.idx==0 || ring[(h.idx-1)%R].loc.idx!=h.idx-1){
				node->set_ready(h.idx);
				continue;
			}
			else{
				if(!paused){
					usleep(1);
					paused = true;
					continue;
				}
				else{
					//puts("contending");
					h.idx--;
					continue;
				}
			}
		}

		// now we know we're ready
		// DO DEQUEUE------------
		if(val!=NULL_VAL){ // check if node is empty node
			initNode(&localnodecopy,safe,1,h.idx,val); // h == idx at this point
			initNode(&emptynode,safe,rdy_after,h.idx+R,NULL_VAL);

			DCRQ_wait* w = (DCRQ_wait*)val; 
			assert(node == &this->ring[h.idx%R]);
			__sync_synchronize();
			if(w->satisfy((uint32_t)node,arg)){
				__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui);
				(node+1)->set_ready(h.idx+1);
				return OK;
			}
			else{ // someone beat us to the wait structure - this can only happen on lock_free
				__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui);
				//puts("beaten");
				(node+1)->set_ready(h.idx+1);
				h.idx++;
				continue;
			}
		}
		else{	// idx ==h and val ==NULL, move past my index
			initNode(&localnodecopy,safe,1,idx,NULL_VAL);
			initNode(&emptynode,safe,rdy_after,h.idx+R,NULL_VAL);
			if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){
				//puts("done fail");
				(node+1)->set_ready(h.idx+1);
				if(emptycheck(h)==EMPTY){return EMPTY;}
				else{
					h.ui = __sync_fetch_and_add (&this->head.ui, 1); 
					continue;
				}
			}
		}  
	} // end outer loop

}

int32_t SPDQ::DCRQ::dequeue(bool antidata, int32_t arg){

	if(!lock_free || this->antidata == DATA){
		return dequeue_normal(antidata,arg);
	}
	else{
		return dequeue_lock_free(antidata,arg);
	}
}

int32_t SPDQ::DCRQ::dequeue_normal(bool antidata, int32_t arg){
	// local variables
	struct idx_struct h;			//h, t : 64 bit int
	struct Node node_contents;	
	struct Node* node;
	uint32_t R = SPDQ::_RING_SIZE;
	struct idx_struct t;
	struct idx_struct loc;
	char closed; 				//closed : boolean
	char safe;					//safe : boolean

	// local nodes used for CAS swapping
	struct Node localnodecopy;
	struct Node emptynode;
	struct Node unsafenode;

	uint32_t idx; 		//val , idx : 64 bit int
	uint32_t ready;
	int32_t val;

	int rdy_after = (lock_free && this->antidata)?0:1;	

	assert(antidata!=this->antidata);

	// empty state optimization
	if(this->tail.idx<=this->head.idx){
		fixstate();
		return EMPTY;
	}
	

	while(true){

		h.ui = __sync_fetch_and_add (&this->head.ui, 1);
		assert(h.idx<107374182);  // abort on overflow

		node = &this->ring[h.idx%R];
		node_contents.ui = node->ui;

		while(true){
			// reread node contents for next iteration
			node_contents.ui = __sync_fetch_and_add (&node->ui,0);//node->ui;	
			val = node_contents.val;
			loc.ui = node_contents.loc.ui;
			safe = loc.safe;
			idx = loc.idx;	
			ready = loc.ready;
			assert(ready);

			if(idx>h.idx){ // we might be falling behind - check for empty
				if(emptycheck(h)==EMPTY){return EMPTY;}
				else{break;}
			}
			if(val!=NULL_VAL){ // check if node is empty node
				if(idx==h.idx){	// try dequeue transition
					initNode(&localnodecopy,safe,1,h.idx,val); // h == idx at this point
					initNode(&emptynode,safe,rdy_after,h.idx+R,NULL_VAL);
					if(antidata == DATA){
						DCRQ_wait* w = (DCRQ_wait*)val; 
						assert(node == &this->ring[h.idx%R]);
						__sync_synchronize();
						if(w->satisfy((uint32_t)node,arg)){
							__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui);
							return OK;
						}
						else{ // someone beat us to the wait structure - this can only happen on lock_free
							__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui);
							printf("beaten wval: %d, wptr: %x, this:%x",w->val(),(uint32_t)w,(uint32_t)this);
							assert(false); 
							exit(-1);
						}
					}
					else if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){	
						return val;
					}
				}	
				else{ // not my node, mark node unsafe to prevent an enqueue here for me
					initNode(&localnodecopy,safe,1,idx,val); 
					initNode(&unsafenode,0,rdy_after,idx,val); // this is typo in paper.  They say to use h instead of idx
					if(__sync_bool_compare_and_swap(&(node->ui),localnodecopy.ui,unsafenode.ui)){							
						if(emptycheck(h)==EMPTY){return EMPTY;}
						else{break;}
					}
					// else{ we took no action this iteration, so continue}
				}			
			}
			else{	// idx <h and val ==NULL, move past my index
				initNode(&localnodecopy,safe,1,idx,NULL_VAL);
				initNode(&emptynode,safe,rdy_after,h.idx+R,NULL_VAL);
				if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){
					if(emptycheck(h)==EMPTY){return EMPTY;}
					else{break;}
				}
			}   

		}// end inner while loop
	}// end outer while loop
}

int32_t SPDQ::DCRQ::enqueue(bool antidata, int32_t arg){
	int32_t val;
	struct idx_struct h;
	struct idx_struct t;
	struct idx_struct loc;
	struct Node* node;			//node : pointer to tail node
	struct Node node_contents;
	uint32_t R = SPDQ::_RING_SIZE;

	struct Node localnodecopy;
	struct Node newnode;

	long starvation_level = 0;
	uint32_t rdy_before;//(lock_free && this->antidata)?0:1; // TODO verify for !lock_free
	int rdy_after = (lock_free && this->antidata)?0:1;

	if(arg==0){
		printf("invalid enqueue argument (==0)\n");
		abort();
	}
	assert(antidata==this->antidata);

	while(true){

		t.ui = __sync_fetch_and_add (&this->tail.ui, 1);
		if(t.closed!=0){
			return CLOSED;
		}

		if(t.idx>107374182){  // abort on overflow
			abort();
		}

		node = &(this->ring[t.idx%R]);  // read current tail
		node_contents.ui = __sync_fetch_and_add (&node->ui,0);//node->ui; // TODO: switch from volatile //one 64 bit read
		val = node_contents.val;
		loc.ui = node_contents.loc.ui;
		rdy_before = loc.ready;		

		if(loc.safe==0){
			//printf("found unsafe\n");
		}

		if(val==NULL_VAL){ // tail is empty, so we can try enqueue transition
			initNode(&localnodecopy,loc.safe,rdy_before,loc.idx,val);
			initNode(&newnode,1,rdy_after,t.idx,arg);
			
			// prep DCRQ_wait with unique tag
			if(antidata){
				DCRQ_wait* w = (DCRQ_wait*)arg;
				//assert(w->is_sat());
				w->set((uint32_t)node,0);
				//printf("node %x, idx %d, wait %x, this %x\n",node,loc.idx,w,this);
			}

			if(loc.idx<=t.idx && (loc.safe==1 || this->head.idx<=t.idx)){
				if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, newnode.ui)){  // enqueue
					assert(node->val == arg || node->loc.idx>t.idx);
					assert(node->val == arg || (this->head.idx>t.idx));

					return OK;
				}
			}
		}
		// else, our copy of the tail index was stale, so we try again

		h = this->head;
		// if we find ourselves overlapping head, we close the queue
		// everyone who discovers this closes the queue
		if((t.idx>=h.idx+R) || starvation_level>=_STARVATION){ 
			this->tail.close();
			return CLOSED; // we've closed this ring because it's full.
		}
		starvation_level++;
	}
}


SPDQ::SPDQ(int t_num, bool glibc_mem,bool lock_free){
	int i,j;
	// init block pool
	bp = new BlockPool<struct DCRQ>(t_num,glibc_mem);
	std::list<struct DCRQ*> v;
	this->lock_free=lock_free;

	/*for(i=0;i<task_num;i++){
		for(j=0; j<100000; j++){
			v.push_back((struct DCRQ*)bp->alloc(i));
		}
	}
	for(i=0;i<task_num;i++){
		for(j=0; j<100000; j++){
			bp->free(v.front(),i);
			v.pop_front();
		}
	}*/


	head.ptr = (struct DCRQ*)bp->alloc(0); //(malloc(sizeof(struct DCRQ));
	head.cntr=0;
	head.ptr->initRingQueue(0,true,lock_free);
	head_index = 0;
	tail=head;
	tail.cntr = 0;
	hazard = new struct volatile_padded<uint64_t>[t_num];
	task_num = t_num;
	retired = new struct volatile_padded<std::list<struct DCRQ*>*>[t_num];
	waiters = new struct padded<DCRQ_wait>[t_num];
	for(i=0;i<task_num;i++){
		retired[i].ui=new std::list<struct DCRQ*>();
		hazard[i].ui=UINT64_MAX;
		waiters[i].ui.set(0,1);
	}


	
	//printf("hi: %d",head_index);
}

SPDQ::~SPDQ(){
	int i;
	struct DCRQ* garbage;
	struct DCRQ* next_dcrq;
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
		next_dcrq = garbage->next;
		free(garbage);
		garbage = next_dcrq;
	}*/
	//while(this->dequeue()!=EMPTY){}
	delete[] retired;
	delete[] hazard;
}

void SPDQ::retire(int tid, struct DCRQ* dcrq){
	int i;
	uint64_t min_hazard;
	min_hazard = UINT64_MAX;
	long idx;
	struct DCRQ old;
	struct DCRQ* garbage;
	old = *dcrq;
	for(i=0;i<task_num;i++){
		if(hazard[i].ui<min_hazard){
			min_hazard=hazard[i].ui;
		}
	}

	if(min_hazard>dcrq->index){
		// dcrq is already clear,
		// we can free it
		bp->free(dcrq,tid);
	}
	else{
		// dcrq is not clear
		// append it to the retired list
		retired[tid].ui->push_back(dcrq);
		
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


int32_t SPDQ::_dequeue(DCRQ_ptr h, bool antidata, int32_t arg, int tid){
	// local variables
	DCRQ_ptr dcrq;
	DCRQ_ptr dcrq_next;
	int32_t v;
	uint64_t haz;
	DCRQ_ptr newdcrq;
	DCRQ_wait* w = &(waiters[tid].ui);
	int32_t newWaitVal;

	newdcrq.ptr=NULL;

	while(true){
		hazard[tid].ui= head_index; // get head_index, it is guaranteed to be 
								// less than or equal to the actual head index
								// we set it as our hazard index.  Nothing
								// above it can be freed
		dcrq = head;
		if(dcrq.ptr->antidata==antidata){// && 
			// then head changed beneath us
			hazard[tid].ui=UINT64_MAX;
			//w->wipe();
			return EMPTY;
		}

		v = dcrq.ptr->dequeue(antidata, arg); // dequeue from head
		if(v==RING_NOT_READY){
			assert(lock_free);
			continue;
		}
		else if(v!= EMPTY){
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			//w->wipe();
			return v;  // dequeued successfully, return
		} 
		// seal empty DCRQ so we can remove it
		else if(!dcrq.ptr->seal()){
			hazard[tid].ui=UINT64_MAX;
			continue;
		}
		assert(dcrq.ptr->seal());

		// at this point head dcrq is sealed
		// we need to add a tail
		// of our polarity
		if(dcrq.ptr->next==NULL){

			// create new ring
			if(newdcrq.ptr==NULL){ // TODO can't recycle these because could end up with wrong node in wait
				newdcrq.ptr=(struct DCRQ*)bp->alloc(tid);
				if(newdcrq.ptr==NULL){// we ran out of memory...
					fprintf(stderr,"Out of memory on DCRQ alloc!\n");
					abort();
				}
				newdcrq.ptr->initRingQueue(0,antidata,lock_free);
				// enqueue me
				if(antidata){
					arg = (int32_t) w;
					w->set(0,1);
				}
				if(newdcrq.ptr->enqueue(antidata, arg)!=OK){
					bp->free(newdcrq.ptr,tid);
					newdcrq.ptr=NULL;
					assert(false);
					continue;
				}
				newWaitVal = w->val();
			}

			// append our new ring to list
			assert(dcrq.ptr->seal());
			w->set(newWaitVal,0);
			if(appendRing(dcrq,newdcrq)){
				
				// swing head
				swingHead(dcrq,tid);
				
				// wait until satisfied, then return the value
				if(antidata){
					//printf("%d: apwait\n",tid);
					while(!w->is_sat()){} 
					//printf("%d: done\n",tid);
					hazard[tid].ui=UINT64_MAX; 
					uint32_t v = w->val();
					//w->wipe();
					return v;
				}
				else{	
					hazard[tid].ui=UINT64_MAX;
					return OK;
				}
			}
			else{
				bp->free(newdcrq.ptr,tid); 
				newdcrq.ptr=NULL;
			}
		} 
		else{
			// else, the head is sealed, but has a next, so swing it and try again
			swingHead(dcrq,tid);
			hazard[tid].ui=UINT64_MAX;
		}
	}

}

int32_t SPDQ::_enqueue(DCRQ_ptr h, bool antidata, int32_t arg, int tid){
	// local variables
	DCRQ_ptr dcrq;
	DCRQ_ptr dcrq_next;
	DCRQ_ptr newdcrq;

	newdcrq.ptr=NULL;
	while(true){ 
		hazard[tid].ui=head_index; // get head_index, it is guaranteed to be 
								// less than or equal to the actual head and tail index
								// we set it as our hazard index.  Nothing
								// above it can be freed
		dcrq = tail;
		if(dcrq.ptr->next!=NULL){
			newdcrq.ptr = dcrq.ptr->next;
			newdcrq.ptr->index = dcrq.ptr->index+1; 
			newdcrq.cntr = dcrq.cntr+1; 
			__sync_bool_compare_and_swap (&tail.ui, dcrq.ui,newdcrq.ui); // update tail pointer
			newdcrq.ptr=NULL;
			continue;
		}
		if(dcrq.ptr->antidata!=antidata){
			// enqueueing wrong polarity (head is out of date)
			// that means it must be sealed, or I am out of date
			if(head.ptr==h.ptr && h.ptr->seal()){
				swingHead(h,tid);
			}
			hazard[tid].ui=UINT64_MAX;
			return CLOSED;
		}
		if(dcrq.ptr->next!=NULL){ 
			// tail wasn't actually the tail, try the next one and loop
			dcrq_next.ptr = dcrq.ptr->next;
			dcrq_next.cntr = dcrq.cntr+1;
			__sync_bool_compare_and_swap (&tail.ui, dcrq.ui,dcrq_next.ui); 
			continue;
		}
		if(dcrq.ptr->enqueue(antidata,arg)==OK){ // successfully enqueued
			//hazard[tid].ui=UINT64_MAX; // reset our hazard index 
			if(newdcrq.ptr!=NULL){
				bp->free(newdcrq.ptr,tid);
			} 
			return OK;
			//return (int32_t)dcrq.ptr;
		}
		// else, the tail is closed
		// we need to make a new tail
		// and enqueue the arg onto it
		if(newdcrq.ptr==NULL){
			newdcrq.ptr=(struct DCRQ*)bp->alloc(tid);
			if(newdcrq.ptr==NULL){// we ran out of memory...
				fprintf(stderr,"Out of memory on DCRQ alloc!\n");
				abort();
			}
			newdcrq.ptr->initRingQueue(0,antidata,lock_free);
			if(newdcrq.ptr->enqueue( antidata, arg)!=OK){
				bp->free(newdcrq.ptr,tid);
				newdcrq.ptr=NULL;
				assert(false); 
				continue;
			}

		}
		// append ring
		if(appendRing(dcrq,newdcrq)){
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			return OK;
		}
		else{
			bp->free(newdcrq.ptr,tid);
			newdcrq.ptr=NULL;
		}
	}

}

// head must be sealed prior to calling this
bool SPDQ::swingHead(DCRQ_ptr h, int tid){
	if(head.ui!=h.ui){
		return false;
	}
	assert(h.ptr!=NULL);
	assert(h.ptr->seal());

	DCRQ_ptr dcrq_next;
	DCRQ_ptr dcrq=h;
	__sync_synchronize();
	assert(NULL!=dcrq.ptr); 
	// swing head
	dcrq_next.ptr = dcrq.ptr->next;
	dcrq_next.cntr = dcrq.cntr+1;
	assert(dcrq.ptr!=dcrq_next.ptr);

	// check for swinging to NULL
	if(dcrq_next.ptr==NULL){assert(h.ui!=head.ui);}
	else{assert(dcrq.ptr->index+1==dcrq_next.ptr->index);}
	
	if(__sync_bool_compare_and_swap(&head.ui, dcrq.ui,dcrq_next.ui)){ 
		__sync_fetch_and_add (&head_index, 1);  // update head index
		//printf("hi: %d",head_index);
		retire(tid,dcrq.ptr);
		return true;
	}
	return false;
}


bool SPDQ::appendRing(DCRQ_ptr prev, DCRQ_ptr next){

	assert(prev.ptr->antidata==next.ptr->antidata ||
	prev.ptr->seal());

	assert(prev.ptr!=next.ptr);

	DCRQ_ptr& dcrq = prev;
	DCRQ_ptr& newdcrq = next;

	next.ptr->index= prev.ptr->index+1;

	newdcrq.ptr->index = dcrq.ptr->index+1;
	newdcrq.cntr = dcrq.cntr+1; 
	newdcrq.ptr->prev = dcrq.ptr;
	if(__sync_bool_compare_and_swap (&(dcrq.ptr->next), NULL,newdcrq.ptr)){//add new tail to list
		__sync_bool_compare_and_swap (&tail.ui, dcrq.ui,newdcrq.ui); // update tail pointer
		return true;
	}
	return false;
}

int32_t SPDQ::remove(int tid){
	DCRQ_ptr dcrq;
	DCRQ_ptr dcrq_next;
	int32_t v;
	bool antidata = ANTIDATA;

	// keep trying to operate on queue
	while(true){
		dcrq = head;
		// if head polarity matches operation polarity (holds -), enqueue
		if(dcrq.ptr->antidata == ANTIDATA){
			DCRQ_wait* w = &(waiters[tid].ui);
			//assert(w->is_sat());
			w->set(0,1);
			v = _enqueue(dcrq, antidata, (int32_t)w, tid);
			if(v==OK){
				//puts("neg thoughts");
				while(!w->is_sat()){}
				//puts("neg en");
				uint32_t v = w->val();
				//w->wipe();
				return v;
			}

		}
		// else, dequeue
		else{
			v = _dequeue(dcrq, antidata, 0,tid);
			if(v!=EMPTY){
				return v;
			}
			// if got empty from the head ring, we 
			// need to swing head to a new ring
		}
	}
}

void SPDQ::insert(int32_t arg, int tid){
	DCRQ_ptr dcrq;
	DCRQ_ptr dcrq_next;
	int32_t v;
	bool antidata = DATA;

	// keep trying to operate on head
	while(true){
		dcrq = head; // TODO verify, was tail
		// if head polarity matches operation polarity (holds +), enqueue
		if(dcrq.ptr->antidata == DATA){
			v = _enqueue(dcrq, antidata, arg, tid);
			if(v==OK){
				return;
			}
		}
		else{
		// else, dequeue
			//puts("neg de thoughts");
			v = _dequeue(dcrq, antidata, arg,tid);
			if(v!=EMPTY){
				//puts("neg de");
				return;
			}

			// if got empty from the head ring, try again.  
			// _deque got rid of head if it could.
		}
	}
}


















