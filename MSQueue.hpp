#ifndef MSQUEUE_H
#define MSQUEUE_H

#include "RDualContainer.hpp"
#include <cstdlib>
#include <cassert>
#include <queue>
#include <list>
#include <utility>
#include "BlockPool.hpp"

template<typename T>
struct qnode {
    cptr<qnode> next;
    T t;
} __attribute__(( aligned(8) ));

//  Class exists only as a (non-templated) base for concurrent_queue<T>
//
template<typename T>
class MS_queue {
	cptr<qnode<T>>  head
	__attribute__(( aligned(CACHE_LINE_SIZE) )); uint8_t pad1[CACHE_LINE_SIZE-sizeof(cptr<qnode<T>>)]; // pad
	cptr<qnode<T>>  tail
	__attribute__(( aligned(CACHE_LINE_SIZE) )); uint8_t pad2[CACHE_LINE_SIZE-sizeof(cptr<qnode<T>>)]; // pad
	BlockPool<qnode<T>>* bp = 0;
public:
	void enqueue(T item, const int tid);
	T dequeue(const int tid);
	MS_queue(const int task_num, bool glibc_mem);
	std::pair<uint64_t, T> peek(const int tid);
	bool dequeue_cond(uint64_t key,const int tid);
	virtual ~MS_queue() { assert(false); }
    // Destruction of concurrent queue not currently supported.
};


class MSQueue : public virtual RPeekableContainer, public virtual RQueue, public virtual Reportable{
public:
	MS_queue<int32_t> ms;

	MSQueue(const int task_num, bool glibc_mem):ms(task_num,glibc_mem){
	}

	int32_t dequeue(int tid){
		return ms.dequeue(tid);
	}
	void enqueue(int32_t val,int tid){
		ms.enqueue(val,tid);
	}
	int32_t remove(int tid){
		return dequeue(tid);
	}
	void insert(int32_t val,int tid){
		return enqueue(val,tid);
	}


	void introduce(){
		int i = 0;
		std::cout<<"intrdouce"<<std::endl;
		this->insert(1,0);
		this->insert(2,0);
		this->insert(3,0);
		this->insert(4,0);
	}



	void conclude(){
		int i = 0;
		std::cout<<"conc"<<std::endl;
		/*while(this->remove(0)!=EMPTY){
			i++;
		}*/
		//std::cout<<"size@End="<<i<<std::endl;
	}


	KeyVal peek(int tid){
		std::pair <uint64_t,int32_t> p = ms.peek(tid);
		KeyVal k;
		k.key = p.first;
		k.val = p.second;
		return k;
	}
	bool remove_cond(uint64_t key, int tid){
		return ms.dequeue_cond(key,tid);
	}
};




//////////////////////////////
//
//  Templated Non-Blocking Queue

//static block_pool<void*>* bp = 0;
    // Shared by all queue instances.  Note that all queue nodes are
    // the same size, because all payloads are pointers.

//  Following constructor should be called by only one thread, and there
//  should be a synchronization barrier (or something else that forces
//  a memory barrier) before the queue is used by other threads.
//  Paremeter is id of calling thread, whose subpool should be used to
//  allocate the initial dummy queue node.
//
/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */


template<typename T>
std::pair<uint64_t, T> MS_queue<T>::peek(const int tid){
	std::pair<uint64_t, T> p;

	cptr_local<qnode<T>> my_head, my_tail;
	qnode<T>* my_next;
	T rtn;

	while (true) {
		my_head.init(head);
		my_tail.init(tail);
		my_next = (qnode<T>*)my_head->next;
		cptr_local<qnode<T>>  my_head2;
		my_head2.init(head);

		if (my_head == my_head2) {
			// head, tail, and next are mutually consistent
			if (my_head.ptr() != my_tail.ptr()) {
				// Read value out of node before CAS. Otherwise another dequeue
				// might free the next node.
				rtn = my_next->t;
				// verify snapshot
				if (head==my_head) {
					break; // "dequeue" worked
				}
			}
			else {
				// queue is empty, or tail is falling behind
				if (my_next == NULL){
					// queue is empty
					rtn = 0;
					my_head.storeNull();
					break;
				}
				// try to swing tail to next node
				tail.CAS(my_tail, my_next);
			}
		}
	}

	p.second = rtn;
	p.first = my_head.all();
	return p;
}

template<typename T>
bool MS_queue<T>::dequeue_cond(uint64_t key,const int tid){
	cptr_local<qnode<T>> my_head, my_tail;
	qnode<T>* my_next;
	T rtn;

	my_head.init(head);	
	if(my_head.all()!=key){return false;}
	my_tail.init(tail);
	my_next = my_head->next.ptr();
	cptr_local<qnode<T>>  my_head2(head);

	if (my_head == my_head2) {
		// head, tail, and next are mutually consistent
		if (my_head.ptr() != my_tail.ptr()) {
			// Read value out of node before CAS. Otherwise another dequeue
			// might free the next node.
			rtn = my_next->t;
			// try to swing head to next node
			if (head.CAS(my_head, my_next)) {
				bp->free(my_head.ptr(), tid);
				return true;
			}
		}
		else {
			// queue is empty, or tail is falling behind
			if (my_next == 0){
				// queue is empty
				return false;
			}
			// try to swing tail to next node
			tail.CAS(my_tail, my_next);
		}
	}
	return false;
}



template<typename T>
MS_queue<T>::MS_queue(const int task_num, bool glibc_mem)
{
	int i,j;
	bp = new BlockPool<qnode<T>>(task_num,glibc_mem);
	int tid=0;
	qnode<T>* qn = bp->alloc(0);
	qn->next.init(NULL,0);
	head.init(qn,0);
	tail.init(qn,100);


	// preheat block pool
	std::list<qnode<T>*> v;
	for(i=0;i<task_num;i++){
		for(j=0; j<2500; j++){
			v.push_back((qnode<T>*)bp->alloc(i));
		}
	}
	for(i=0;i<task_num;i++){
		for(j=0; j<2500; j++){
			bp->free(v.front(),i);
			v.pop_front();
		}
	}

}

//  M&S lock-free queue
//
template<typename T>
void MS_queue<T>::enqueue(T t, const int tid){

	qnode<T>* qn = bp->alloc(tid);
	cptr_local<qnode<T>> my_tail,my_tail2;
	qn->t = t;
	qn->next.storePtr(NULL);
	//qn->next.storeNull();
	// leave sn where it is! - important for garbage collection issues
	while (true) {
		cptr_local<qnode<T>>  my_next;
		my_tail.init(tail);
		my_next.init(my_tail->next);
		my_tail2.init(tail);
		if (my_tail == my_tail2) {
			// my_tail and my_next are mutually consistent
			if (my_next.ptr() == NULL) {
				// last node; try to link new node after this
				if (my_tail->next.CAS(my_next, qn)) {
					// enqueue worked
					break;              
				}
			}
			else {
				// try to swing B->tail to next node
				tail.CAS(my_tail, my_next.ptr());
			}
		}
	}
	// try to swing B->tail to newly inserted node
	tail.CAS(my_tail, qn);
}

// Returns 0 if queue was empty.  Since payloads are required to be
// pointers, this is ok.
//
template<typename T>
T MS_queue<T>::dequeue(const int tid)
{
	cptr_local<qnode<T>> my_head, my_tail;
	qnode<T>* my_next;
	T rtn;

	while (true) {
		my_head.init(head);
		my_tail.init(tail);
		my_next = my_head->next.ptr();
		cptr_local<qnode<T>> my_head2;
		my_head2.init(head);
		if (my_head == my_head2) {
			// head, tail, and next are mutually consistent
			if (my_head.ptr() != my_tail.ptr()) {
				if(my_next==NULL){continue;} // this seems necessary, but isn't in the paper
				// Read value out of node before CAS. Otherwise another dequeue
				// might free the next node.
				rtn = my_next->t;
				// try to swing head to next node
				if (head.CAS(my_head, my_next)) {
					break;                  // dequeue worked
				}
			}
			else {
				// queue is empty, or tail is falling behind
				if (my_next == NULL){
					// queue is empty
					return T(0);
				}
				// try to swing tail to next node
				tail.CAS(my_tail, my_next);
			}
		}
	}
	bp->free(my_head.ptr(), tid);
	return rtn;
}


class MSQueueFactory : public RContainerFactory{
	MSQueue* build(GlobalTestConfig* gtc){
		MSQueue* q = new MSQueue(gtc->task_num,gtc->environment["glibc"]=="1");
		return q;
	}
};


#endif
