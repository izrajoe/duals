/* Dualqueue: a non-blocking queue that spins on dequeue operations
   until data is available. Based heavily on an implementation of the
   Michael and Scott lock-free queue algorithm written by Michael
   Scott.

   Written by Bill Scherer
   Copyright (c) 2003, Bill Scherer

   Note that this version of dequeue assumes a cache-coherent
   architecture. For NUMA machines, we'd swap a pointer to a local
   variable into the data field so we could still have a local spin

	Updated to use C++ atomics by Joe Izraelevitz
	2014
*/

/* TBD: Add patience for dequeue operations */



#include "SSDualQueue.hpp"

/* ****************************************************************** */
/* Types                                                              */
/* ****************************************************************** */


/*bool cp_cas(volatile counted_ptr* target, volatile counted_ptr old_cp, void  *volatile new_ptr){
	volatile counted_ptr n;
	n.p.sn = old_cp.p.sn;
	n.p.ptr = new_ptr;
	return __sync_bool_compare_and_swap(&(target)->all, (old_cp).all, n.all);
}*/




/* ****************************************************************** */
/* Functions                                                          */
/* ****************************************************************** */


/*
    Following routine should be called by only one thread.
    Paremeter is pool from which to allocate blocks for queue.
    Return value is an opaque pointer.
*/
SSDualQueue::SSDualQueue(int t_num,bool glibc_mem)
{
	int i,j;
    dqnode_t *qn;

    this->bp = new BlockPool<dqnode_t>(t_num,glibc_mem);
    qn = (dqnode_t *) this->bp->alloc(0);
	//printf("%d\n",qn);
	//puts("next");
    qn->next.init(NULL,0,false,false);
    qn->request.init(NULL,0,false,false);          
	//puts("head");   
    this->head.init(qn,0,false,false);
	//puts("TAIL");
	this->tail.init(qn,0,false,false);
    atomic_thread_fence(std::memory_order_release);
	//printf("%d\n",sizeof(std::atomic<uint32_t>));
	

	// init block pool
	std::list<struct dqnode_t*> v;
	for(i=0;i<t_num;i++){
		for(j=0; j<30000; j++){
			v.push_back((dqnode_t*)this->bp->alloc(i));
		}
	}
	for(i=0;i<t_num;i++){
		for(j=0; j<30000; j++){
			this->bp->free(v.front(),i);
			v.pop_front();
		}
	}

	assert(sizeof(std::atomic<uint64_t>)==sizeof(uint64_t));
	assert(head.all.is_lock_free());
	assert(tail.all.is_lock_free());
    return;
}




/* Add a datum to the queue. If a waiter is repsent, fill the oldest
   outstanding request for data. */
void SSDualQueue::insert(int val, int tid)
{
    dqnode_t *newnode = (dqnode_t *)this->bp->alloc(tid);//
    cnt_ptr_local<dqnode_t> head, tail, next, request;
    dqnode_t *headptr, *tailptr;

    newnode->data = val;
	atomic_thread_fence(std::memory_order_release);
    newnode->next.init(NULL,0,0,0);
    newnode->request.init(NULL,0,0,0);
    
    while (1)
    {
		//atomic_thread_fence(std::memory_order_acquire);
		tail.all=this->tail.all.load();
		head.all=this->head.all.load();
		//atomic_thread_fence(std::memory_order_acquire);

		if ((tail.ptr() == head.ptr()) || !tail.is_request())
		{
			/* Queue empty, tail falling behind, or queue contains
			   data. (queue could also contain exactly one outstanding
			   request with tail pointer as yet unswung) */
			//atomic_thread_fence(std::memory_order_acquire);
			tailptr = tail.ptr();
			next.all=tailptr->next.all.load();
			//atomic_thread_fence(std::memory_order_acquire);
			if (tail.all == this->tail.all.load()){
				/* tail and next are consistent */
				if (NULL != next.ptr()){
					/* Tail falling behind; try to swing it */
					this->tail.CAS(tail, next.ptr());
				}
				else{
					/* Try to link in the new node */
					if (tailptr->next.CAS(next, newnode)){
						/* Linked in. Try to swing ptr and we're done */
						this->tail.CAS(tail, newnode);
						return;
					}
				}
			}
		}
		else
		{
			/* Queue consists of requests.  Give data to first. */
			//atomic_thread_fence(std::memory_order_acquire);
			headptr = head.ptr();
			next.all=headptr->next.all.load();
			//atomic_thread_fence(std::memory_order_acquire);
			if (tail.all != this->tail.all.load()){continue;}
			//atomic_thread_fence(std::memory_order_acquire);
		    request.all=headptr->request.all.load();

			//atomic_thread_fence(std::memory_order_acquire);
			if (head.all == this->head.all){
				/* head, tail, next, and req are all consistent.  */
				bool success = (NULL == request.ptr() &&
					headptr->request.CAS(request, newnode));
				this->head.CAS(head, next.ptr());
				if (success){return;}
			}
		}
    }
}

int SSDualQueue::remove(int tid){

    dqnode_t *newreq = (dqnode_t *)this->bp->alloc(tid);
    cnt_ptr_local<dqnode_t> head, tail, next;
    int result = 0xDEADBEEF; /* placeholder value */
    dqnode_t *headptr, *tailptr, *nextptr, *dataptr;

    newreq->data = 0;
    newreq->next.init(NULL,0,false,false);
    newreq->request.init(NULL,0,true,false);
    //atomic_thread_fence(std::memory_order_release);

    while (1)
    {
		//atomic_thread_fence(std::memory_order_acquire);
		head.all=this->head.all.load();
		tail.all=this->tail.all.load();
		//atomic_thread_fence(std::memory_order_acquire);
		if ((tail.ptr() == head.ptr()) || tail.is_request())
		{
			/* Queue empty, tail falling behind, or queue contains
			   requests. (queue could also contain exactly one
			   outstanding datum with tail pointer as yet unswung) */
			//atomic_thread_fence(std::memory_order_acquire);
			tailptr = tail.ptr();
			next.all=tailptr->next.all.load();
			//atomic_thread_fence(std::memory_order_acquire);
			if (tail.all == this->tail.all.load()){
				/* tail and next are consistent */
				if (NULL != next.ptr()){
					/* Tail falling behind; try to swing it */
					this->tail.CAS(tail, next.ptr());
				}
				else{
					/* Try to link in a request for data. We tag our pointer 
					   to make it clear that we're a request, not data. */
					if (this->tail.CAS(next, newreq)){
						/* Linked in. Try to swing the tail ptr. */
						this->tail.CAS(tail, newreq);

						/* Help someone else if I need to */
						if (head.all == this->head.all){
							//atomic_thread_fence(std::memory_order_acquire);
							headptr = (dqnode_t *)head.ptr();
							nextptr = (dqnode_t *)headptr->next.ptr();
							//atomic_thread_fence(std::memory_order_acquire);
							if (NULL != headptr->request.ptr()){
								this->head.CAS( head, nextptr);
							}
						}

						/* Spin until data is ready. */
						while (NULL == tailptr->request.ptr()){}

						/* Help snip my node */
						//atomic_thread_fence(std::memory_order_acquire);
						head.all=this->head.all.load();
						//atomic_thread_fence(std::memory_order_acquire);
						if (head.ptr() == tailptr){
							this->head.CAS(head, newreq);}

						/* Data is now available. Read it out and go home */
						//atomic_thread_fence(std::memory_order_acquire);
						dataptr = (dqnode_t *)tailptr->request.ptr();
						atomic_thread_fence(std::memory_order_acquire);						
						result = dataptr->data;
						this->bp->free(dataptr, tid);
						this->bp->free(tailptr, tid);
						return result;
					}
				}
	    	}
		}
		else{
			/* Queue consists of real data. Dequeue a node to get some */
			//atomic_thread_fence(std::memory_order_acquire);
			headptr = (dqnode_t *)head.ptr();
			next.all=headptr->next.all.load();
			//atomic_thread_fence(std::memory_order_acquire);
			if (head.all == this->head.all){
				/* head and next are consistent.  */
				nextptr = (dqnode_t *)next.ptr();
				if(nextptr==NULL){
					//puts(" avoidedsegfault ");
					continue;} // if queue was emptied after we decided it had data

				/* Read result first because a subsequent dequeue could
				   free the next node */
				result = nextptr->data;

				/* try to snip out the head of the queue */
				if (this->head.CAS(head, next.ptr())){
					/* Success! */
					this->bp->free(headptr, tid);
					this->bp->free(newreq, tid);
					return result;
				}
			}
		}
    }// end while
}


