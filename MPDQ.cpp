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



#include "MPDQ.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <list>
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <algorithm>



void initDRQ(DRQ* drq, uint64_t index){
	int i;

	drq->data_idx.idx=0;
	drq->data_idx.closed=0;
	drq->data_idx.ui = 0; // unnecessary, but prevents valgrind errors
	drq->antidata_idx.closed=0;
	drq->antidata_idx.idx=0;
	drq->antidata_idx.ui = 0;// unnecessary, but prevents valgrind errors
	drq->closedInfo.ui = 0;
	drq->closedInfo.closed = 0;
	drq->closedInfo.idx=0;
	drq->next=NULL;
	drq->abandoned=0;
	drq->index=index;
	for(i=0;i<DRQ_RING_SIZE;i++){
		initDRQNode(&drq->ring[i],1,i,NULL_VAL,DATA);
	}
}

void initDRQ(DRQ* drq){
	initDRQ(drq,0);
}



int32_t mix(int32_t arg, int32_t val,bool polarity,drq_node* n){
	if(polarity==DATA){
		drq_wait* w = (drq_wait*) val;
		bool b = w->satisfy((int32_t)n,arg);
		assert(b);
		return OK;
	}
	else{
		//printf("dequeued %d\n",val);
		return val;
	}
}

int32_t finishedenqueue(int32_t arg, bool polarity){
	if(polarity==DATA){
		assert(OK!=CLOSED);
		return OK;
	}
	else{
		drq_wait* w = (drq_wait*) arg;
		while(w->is_sat() == false){}
		return w->val();
	}
}

uint32_t discovered_closing(DRQ* drq, uint32_t idx, bool polarity){
	drq_idx data_idx;
	drq_idx antidata_idx;	
	drq_idx p;	

	// check if already closed
	if(drq->closedInfo.closed==1){
		return drq->closedInfo.idx;
	}

	// check and close opposite
	if(polarity==DATA){
		if(!drq->antidata_idx.closed){drq->antidata_idx.close();}
	}
	else{
		if(!drq->data_idx.closed){drq->data_idx.close();}
	}

	drq_idx old_closed;
	old_closed.ui = 0;
	old_closed.idx = 0;
	old_closed.closed = 0;	
	drq_idx new_closed;
	new_closed.ui=0;

	// next read both indices and try to close queue
	data_idx.ui = drq->data_idx.ui;
	antidata_idx.ui = drq->antidata_idx.ui;
	if(data_idx.idx>antidata_idx.idx){new_closed.idx =data_idx.idx;}
	else{new_closed.idx=antidata_idx.idx;}
	new_closed.closed=1;
	__sync_bool_compare_and_swap(&drq->closedInfo.ui, old_closed.ui, new_closed.ui);
	assert(drq->closedInfo.closed==1);
	return drq->closedInfo.idx;

}



int32_t _drqdenqueue(DRQ* drq, int32_t arg, bool polarity){
	// local variables
	uint32_t idx; 		//val , idx : 64 bit int
	int32_t val;
	drq_idx p;			//h, t : 64 bit int
	drq_idx* op;	
	drq_idx loc;
	drq_node* node;			//node : pointer to Node
	drq_node node_contents;
	bool closed; 				//closed : boolean
	bool safe;					//safe : boolean
	bool antidata;
	uint32_t R = DRQ_RING_SIZE;
	int starvationLevel=0;
	uint32_t closeIdx = 0;
	drq_idx op_idx;

	drq_wait* w = (drq_wait*) arg;

	// local nodes used for CAS swapping
	drq_node localnodecopy;
	drq_node emptynode;
	drq_node unsafenode;
	drq_node newnode;

	volatile uint32_t* ptr_ui;
	if(polarity==DATA){
		ptr_ui = &drq->data_idx.ui;
		op = &drq->antidata_idx;
	}
	else{
		ptr_ui = &drq->antidata_idx.ui;
		op = &drq->data_idx;
	}

	while(true){

		p.ui = __sync_fetch_and_add (ptr_ui, 1);
		if(p.closed==1){
			closeIdx = discovered_closing(drq,p.idx,polarity);
			assert(drq->closedInfo.closed==1);
			if(closeIdx <= p.idx){
				if(closeIdx<op->idx){return DRQ_EMPTY;}
				else{return CLOSED;}
			}
		}


		assert(p.idx<107374182);  // abort on overflow
		node = &drq->ring[p.idx%R];

		while(true){
			node_contents.ui = __sync_fetch_and_add (&node->ui,0);
			val = node_contents.val;
			loc.ui = node_contents.loc.ui;
			safe = loc.safe;
			idx = loc.idx;
			antidata = loc.antidata;

			// try to dequeue opposite
			if(val!=NULL_VAL){ 
				if(idx==p.idx && antidata != polarity){	// try dequeue transition
					initDRQNode(&localnodecopy,safe,p.idx,val,antidata); // h == idx at this point
					initDRQNode(&emptynode,safe,p.idx+R,NULL_VAL,antidata);
					if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui)){
						//puts("mix");		
						return mix(arg,val,polarity,node);
					}
				}	
				else{ // not my node, mark node unsafe to prevent opposite operation here for me
					initDRQNode(&unsafenode,0,idx,val,antidata); 
					initDRQNode(&localnodecopy,safe,idx,val,antidata); 
					if(__sync_bool_compare_and_swap(
						&(node->ui),localnodecopy.ui,unsafenode.ui)){	
						//puts("mark unsafe");						
						//continue;
						break;
					}
					// else{ we took no action this iteration, so continue}
				}			
			}

			// try to enqueue self
			else{
				//op_idx.ui = __sync_fetch_and_add (&op->ui,0);
				if(loc.safe==1){// || op_idx.idx<=p.idx){
					// enqueue
					initDRQNode(&localnodecopy,loc.safe,loc.idx,val,antidata);
					initDRQNode(&newnode,1,p.idx,arg,polarity);
					if(polarity==ANTIDATA){w->set((uint32_t)node,false);}
					if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, newnode.ui)){  
						//puts("enqueued");	
						return finishedenqueue(arg,polarity);
					}
				}
				else{break;}
			} 

		} // end inner while loop  
		starvationLevel++;

		// if fail to make progress
		op_idx.ui = op->ui;
		if( ( (((int)p.idx-op_idx.idx)>=R) || starvationLevel>DRQ_STARVATION) && !p.closed){
			if(polarity==DATA){
				drq->data_idx.close();
			}
			else{
				drq->antidata_idx.close();
			}
			closeIdx = discovered_closing(drq,p.idx,polarity);
			assert(drq->closedInfo.closed==1);
			if(closeIdx <= p.idx){
				if(closeIdx<op->idx){return DRQ_EMPTY;}
				else{return CLOSED;}
			}
		}

	}// end outer while loop

}// end dequeue



int32_t _drqdenqueue_lockfree(DRQ* drq, int32_t arg, bool polarity){
	//puts("lockfree");
	// local variables
	uint32_t idx; 		//val , idx : 64 bit int
	int32_t val;
	drq_idx p;			//h, t : 64 bit int
	drq_idx* op;	
	drq_idx loc;
	drq_node* node;
	drq_node* node_prev;			//node : pointer to Node
	drq_node node_contents;
	drq_node node_contents_prev;
	bool closed; 				//closed : boolean
	bool safe;					//safe : boolean
	bool antidata;
	uint32_t R = DRQ_RING_SIZE;
	int starvationLevel=0;
	uint32_t closeIdx = 0;

	// local nodes used for CAS swapping
	drq_node localnodecopy;
	drq_node emptynode;
	drq_node unsafenode;
	drq_node newnode;

	// get heads
	volatile uint32_t* ptr_ui;
	ptr_ui = &drq->data_idx.ui;
	op = &drq->antidata_idx;
	assert(polarity==DATA);

	p.ui = __sync_fetch_and_add (ptr_ui, 1);
	while(true){
		// check for closed
		if(p.closed==1 || drq->data_idx.closed==1){
			closeIdx = discovered_closing(drq,p.idx,polarity);
			assert(drq->closedInfo.closed==1);
			if(closeIdx <= p.idx){
				if(closeIdx<op->idx){return DRQ_EMPTY;}
				else{return CLOSED;}
			}
		}
		// if fail to make progress
		if( ( (((int)drq->data_idx.idx-(int)op->idx)>=(R-2*48)) || starvationLevel>DRQ_STARVATION) && !p.closed){
			drq->data_idx.close();
			closeIdx = discovered_closing(drq,drq->data_idx.idx,polarity);
			assert(drq->closedInfo.closed==1);
			if(closeIdx <= p.idx){
				if(closeIdx<op->idx){return DRQ_EMPTY;}
				else{return CLOSED;}
			}
		}

		node = &drq->ring[p.idx%R];
		node_contents.ui = node->ui;
		val = node_contents.val;
		loc.ui = node_contents.loc.ui;
		safe = loc.safe;
		idx = loc.idx;
		antidata = loc.antidata;

		// find wavefront
		if(p.idx<idx){ // behind
			p.idx++;
			continue;
		}
		if(idx<p.idx){ // lapped
			p.idx=(p.idx-R)+1;
			//printf("p.idx:%d\n",p.idx);
			//assert(drq->data_idx.closed!=1);
			continue;
		}
		node_prev = &drq->ring[(p.idx-1)%R];
		node_contents_prev.ui = node_prev->ui;
		if(p.idx!=0 && ((node_contents_prev.loc.idx == idx-1) && 
				node_contents_prev.val==NULL_VAL)){ // ahead
			p.idx--;
			//assert(drq->data_idx.closed!=1);
			continue;
		}
		// on the wavefront, can operate	

		

		// try to dequeue opposite
		if(val!=NULL_VAL){ 
			if(idx==p.idx && antidata != polarity){	// try dequeue transition
				initDRQNode(&localnodecopy,safe,p.idx,val,antidata); // h == idx at this point
				initDRQNode(&emptynode,safe,p.idx+R,NULL_VAL,antidata);
				//atomic_signal_fence(std::memory_order::memory_order_seq_cst);
				//__sync_synchronize();
				if(((drq_wait*)val)->satisfy((int32_t)node,arg)){
					//printf("Won @ %x\n",(drq_wait*)val);
					__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui);
					return OK;
				}
				else{
					__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, emptynode.ui);
					//printf("Beaten @ %x\n",(drq_wait*)val);
					assert(((drq_wait*)val)->val()!=(int32_t)node);
					p.idx++;
					continue;
				}
			}
			else if(antidata == polarity){
				p.idx++;
				continue;
			}
		}

		// try to enqueue self
		else{
			if(loc.safe==1 || op->idx<=p.idx){
				// enqueue
				initDRQNode(&localnodecopy,loc.safe,loc.idx,val,antidata);
				initDRQNode(&newnode,1,p.idx,arg,polarity);
				if(__sync_bool_compare_and_swap (&(node->ui), localnodecopy.ui, newnode.ui)){  
					//printf("enqueued %d@%d\n",arg,p.idx);	
					return finishedenqueue(arg,polarity);
				}
			}
			else{continue;} // something got in my way - either my counterpart or competition
		} 


		//starvationLevel++;


	}// end outer while loop

}// end dequeue


int32_t drqenqueue(DRQ* drq, int32_t arg){
	return _drqdenqueue(drq,arg, DATA);
}
int32_t drqdequeue(DRQ* drq){
	drq_wait* w;
	w->set(0,false);// TODO use location
	assert(false);
	return _drqdenqueue(drq,(int32_t)w, ANTIDATA);
}

int32_t drqdenqueue(DRQ* drq, int32_t arg,bool polarity){
	if(polarity==DATA){
		return _drqdenqueue(drq,arg, DATA);
	}
	else{
		return _drqdenqueue(drq,arg, ANTIDATA);
	}
}

int32_t MPDQ::denqueue(int32_t arg, bool polarity, int tid){
	// local variables
	DRQ_ptr drq;
	DRQ_ptr drq_next;
	DRQ_ptr newdrq;
	int32_t v;
	uint64_t haz;

	DRQ_ptr* head;
	newdrq.ptr=NULL;

	if(polarity==DATA){head = &data_head;}
	else{head = &antidata_head;}


	while(true){
		hazard[tid].ui= head_index; // get head_index, it is guaranteed to be 
								// less than or equal to the actual head index
								// we set it as our hazard index.  Nothing
								// above it can be freed
		drq = *head;

		if(polarity==DATA && lock_free){
			v = _drqdenqueue_lockfree(drq.ptr,arg,polarity);
		}
		else{
			v = drqdenqueue(drq.ptr,arg,polarity);
		}
		
		// successful dequeue
		if(v!=CLOSED && v!=DRQ_EMPTY){
			hazard[tid].ui=UINT64_MAX; // reset our hazard index
			//if(arg==1000 && v==OK){
			//	this->hotdrq = drq.ptr;
			//}
			return v;  // dequeued successfully, return
		}

		// head is closed, attach next DRQ if necessary
		if(v==CLOSED || v== DRQ_EMPTY){
			assert(drq.ptr->closedInfo.closed);
			// check if next drq exists
			if(drq.ptr->next!=NULL){
				drq_next.ptr = drq.ptr->next;
				drq_next.cntr = drq.cntr+1;
				__sync_bool_compare_and_swap (&head->ui, drq.ui,drq_next.ui); 
			}
			else{
				// if not, add it
				if(newdrq.ptr==NULL){
					newdrq.ptr=(DRQ*)bp->alloc(tid);
					if(newdrq.ptr==NULL){// we ran out of memory...
						fprintf(stderr,"Out of memory on drq alloc!\n");
						abort();
					}
					initDRQ(newdrq.ptr,0);
				}
				newdrq.ptr->index = drq.ptr->index+1;
				newdrq.cntr = drq.cntr+1;
				if(__sync_bool_compare_and_swap (&(drq.ptr->next), NULL,newdrq.ptr)){//add new tail to list
					__sync_bool_compare_and_swap (&head->ui, drq.ui,newdrq.ui); // update head pointer
					newdrq.ptr=NULL;
				}
				else{
					bp->free(newdrq.ptr,tid);
					newdrq.ptr=NULL;
				}
			}
		}

		// remove head if empty and closed
		if(v==DRQ_EMPTY){
			if(__sync_bool_compare_and_swap(&(drq.ptr->abandoned), 0,1)){ 
				__sync_fetch_and_add (&head_index, 1);  // update head index
				hazard[tid].ui=UINT64_MAX; // this line breaks things (does it still?)
				retire(tid,drq.ptr);
			}
		}
	}

}

void MPDQ::insert(int32_t arg, int tid){
	denqueue(arg,DATA,tid);
}
int32_t MPDQ::remove(int tid){
	drq_wait* w = &(waiters[tid].ui);
	return denqueue((int32_t)w,ANTIDATA,tid);
}


MPDQ::MPDQ(int t_num, bool glibc_mem,bool lock_free){
	
	int i,j;
	this->lock_free = lock_free;
	// init block pool
	bp = new BlockPool<DRQ>(t_num,glibc_mem);
	std::list<DRQ*> v;
	data_head.ptr = (DRQ*)bp->alloc(0); //(malloc(sizeof(DRQ));
	data_head.cntr=0;
	initDRQ(data_head.ptr,0);
	head_index = 0;
	antidata_head=data_head;
	antidata_head.cntr = 0;
	hazard = new volatile_padded<uint64_t>[t_num];
	task_num = t_num;
	retired = new volatile_padded<std::list<DRQ*>*>[t_num];
	waiters = new padded<drq_wait>[t_num];
	for(i=0;i<task_num;i++){
		retired[i].ui=new std::list<DRQ*>();
		hazard[i].ui=UINT64_MAX;
		waiters[i].ui.set(0,1);
	}


}

MPDQ::~MPDQ(){
	int i;
	DRQ* garbage;
	DRQ* next_drq;

	delete[] retired;
	delete[] hazard;
}

void MPDQ::retire(int tid, DRQ* drq){
	int i;
	uint64_t min_hazard;
	min_hazard = UINT64_MAX;
	long idx;
	DRQ old;
	DRQ* garbage;
	old = *drq;
	for(i=0;i<task_num;i++){
		if(hazard[i].ui<min_hazard){
			min_hazard=hazard[i].ui;
		}
	}

	if(min_hazard>drq->index){
		// drq is already clear,
		// we can free it
		bp->free(drq,tid);
	}
	else{
		// drq is not clear
		// append it to the retired list
		retired[tid].ui->push_back(drq);
		
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












