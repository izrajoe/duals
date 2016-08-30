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



// Michael's Ordered Set.
//
// See:
// 

#ifndef MICHAEL_ORDERED_SET_H
#define MICHAEL_ORDERED_SET_H

#include "RDualContainer.hpp"
#include "BlockPool.hpp"
#include "RMap.hpp"
#include "HazardTracker.hpp"

template <class T>
class mptr;

template <class T>
class mptr_local{
private:
	uint64_t ui;
public:
	void init(bool marked, T* ptr, uint32_t sn){
		uint64_t a;
		a = 0;
		a = sn & 0x7fffffff;
		a = a<<32;
		a += (uint32_t)ptr;
		if(marked){a=a|0x8000000000000000;}
		ui=a;
	}
	void init(uint64_t initer){
		ui=initer;
	}
	void init(mptr<T>& p){
		init(p.all());
	}
	void init(mptr_local<T>& p){
		init(p.all());
	}

	uint64_t all() const{ return ui;}
	T operator *(){return *this->ptr();}
	T* operator ->(){return this->ptr();}
	T* ptr() const{return (T*)(ui&0x00000000ffffffff);}
	uint32_t sn() const{return (ui&0x7fffffff00000000) >>32;}	
	bool marked() const{return (ui&0x8000000000000000)!=0;}

	mptr_local(){init(false,NULL,0);}
	mptr_local(mptr<T>& cp){
		init(cp.all());
	}
	mptr_local(mptr_local<T>& cp){
		init(cp.all());
	}
	mptr_local(uint64_t initer){
		init(initer);
	}
	mptr_local(bool marked, T* ptr, uint32_t sn){
		init(marked, ptr,sn);
	}

	// conversion from T (constructor):
	mptr_local<T> (const T*& val) {init(val,0);}
	// conversion to T (type-cast operator)
	operator T*() {return this->ptr();}


};

template <class T>
class mptr{
private:
	std::atomic<uint64_t> ui;

public:
	void init(bool marked,T* ptr, uint32_t sn){
		uint64_t a;
		a = 0;
		a = sn & 0x7fffffff;
		a = a<<32;
		a += (uint32_t)ptr;
		if(marked){a=a|0x8000000000000000;}
		ui.store(a,std::memory_order::memory_order_release);
	}
	void init(uint64_t initer){
		ui.store(initer,std::memory_order::memory_order_release);
	}
	void init(mptr<T>& p){
		init(p.all());
	}
	void init(mptr_local<T>& p){
		init(p.all());
	}

	// conversion from T (constructor):
	mptr<T> (const T*& val) {init(val,0);}
	// conversion to T (type-cast operator)
	operator T*() {return this->ptr();}

	T operator *(){return *this->ptr();}
	T* ptr(){return (T*)((ui.load(std::memory_order::memory_order_acquire)) &0x00000000ffffffff);}
	uint32_t sn() const{return ((ui.load(std::memory_order::memory_order_acquire))&0x7fffffff00000000) >> 32;}	
	bool marked() const{return ((ui.load(std::memory_order::memory_order_acquire))&0x8000000000000000)!=0;}
	uint64_t all() const{ return ui.load();}

	bool CAS(mptr_local<T> &oldval,T* newval){
		mptr_local<T> replacement;
		replacement.init(newval,oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_acq_rel);
	}
	bool CAS(mptr_local<T> &oldval,mptr_local<T> &newval){
		mptr_local<T> replacement;
		replacement.init(newval.marked(),newval.ptr(),oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_acq_rel);
	}
	bool CAS(mptr<T> &oldval,T* newval){
		mptr_local<T> replacement;
		replacement.init(newval,oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_acq_rel);
	}
	bool CAS(mptr<T> &oldval,mptr_local<T> &newval){
		mptr_local<T> replacement;
		replacement.init(newval.ptr(),oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_acq_rel);
	}

	void storeNull(){
		init(false,NULL,0);
	}

	void storePtr(T* ptr){
		mptr_local<T> oldval;
		mptr_local<T> newval;
		while(true){
			oldval.init(all());
			newval.init(false,ptr,oldval.sn()+1);
			if(CAS(oldval,newval)){break;}
		};
	}

	// shortcut method if lock is held
	//void set(T* newval){
	//	this->init(newval,this->sn()+1);
	//}
	mptr(){
		init(false,NULL,0);
	}
	mptr(mptr<T>& cp){
		init(cp.all());
	}
	mptr(mptr_local<T>& cp){
		init(cp.all());
	}
	mptr(uint64_t initer){
		init(initer);
	}
	mptr(bool marked, T* ptr, uint32_t sn){
		init(marked, ptr,sn);
	}


	/*bool operator==(mptr<T> &other){
		return other.all==this->all;
	}*/
};



class MichaelOrderedMap : public RMap{

	class Node {
	public:
		mptr<Node> next;
		std::atomic<int32_t> val;
		std::atomic<int32_t> key;
		void init(int32_t k, int32_t v, Node* d){
			val.store(v,std::memory_order::memory_order_release);
			key.store(k,std::memory_order::memory_order_release);
			next.storePtr(d);
		}
	};

	class findInfo{
	public:
		mptr<Node>* prev;
		mptr_local<Node> cur;
		mptr_local<Node> next;	
		bool found;
		findInfo(){
			prev = NULL;
			found = false;
			cur.init(0);
			next.init(0);
		}
		findInfo(const findInfo& f){
			prev= f.prev;
			cur.init(f.cur.all());
			next.init(f.next.all());
			found = f.found;
		}
	};

	private:
	mptr<Node> head;
	BlockPool<Node>* bp;
	int task_num;
	int duplicatePolicy;
	MichaelOrderedMap::findInfo find(int32_t key, bool findMin, int tid);
	MichaelOrderedMap::findInfo find(int32_t key, int tid);
	MichaelOrderedMap::findInfo findMin(int tid);
	void deleteNode(Node* ptr);


	HazardTracker* haz;


	public:
	enum {appendDuplicates, rejectDuplicates, replaceDuplicates};	


	MichaelOrderedMap(int task_num, int duplicatePolicy, bool glibc_mem);
	int32_t get(int32_t key, int tid);
	bool map(int32_t key, int32_t val,int tid);
	int32_t unmap(int32_t key,int tid);
	int32_t minKey(int tid);

	KeyVal peekMin(int tid);
	bool removeMin_cond(uint64_t peekKey, int tid); 	

};


class MichaelPriorityQueue : public virtual RPeekableContainer, public virtual RPriorityQueue, public Reportable {

	MichaelOrderedMap map;

public:
	MichaelPriorityQueue(int task_num, bool glibc_mem) : 
		map(task_num, MichaelOrderedMap::appendDuplicates, glibc_mem)
	{
	}

	void insert(int32_t e,int tid);
	int32_t remove(int tid);
	KeyVal peek(int tid);
	bool remove_cond(uint64_t key, int tid);

};

class MichaelOrderedSet : public virtual Rideable{

	MichaelOrderedMap map;

public:
	MichaelOrderedSet(int task_num, bool glibc_mem) :
		map(task_num, MichaelOrderedMap::rejectDuplicates, glibc_mem)
	{
	}

	bool insert(int32_t e, int tid);
	int32_t remove(int32_t e, int tid);
	bool contains(int32_t e, int tid);
};


class MichaelPriorityQueueFactory : public RContainerFactory{
	MichaelPriorityQueue* build(GlobalTestConfig* gtc){
		return new MichaelPriorityQueue(gtc->task_num,gtc->environment["glibc"]=="1");
	}
};


class MichaelOrderedSetFactory : public RideableFactory{
	MichaelOrderedSet* build(GlobalTestConfig* gtc){
		return new MichaelOrderedSet(gtc->task_num,gtc->environment["glibc"]=="1");
	}
};

class MichaelOrderedMapFactory : public RideableFactory{
	MichaelOrderedMap* build(GlobalTestConfig* gtc){
		return new MichaelOrderedMap(gtc->task_num,MichaelOrderedMap::replaceDuplicates,gtc->environment["glibc"]=="1");
	}
};




#endif


