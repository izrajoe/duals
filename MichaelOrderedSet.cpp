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



//Michael's Ordered Set.
//
// See:

// based on pseudo code from 

#include <list>
#include "MichaelOrderedSet.hpp"

// TODO: Garbage collection

MichaelOrderedMap::MichaelOrderedMap(int task_num, int duplicatePolicy, bool glibc_mem){
	head.init(false,NULL,0);
	bp = new BlockPool<Node>(task_num,glibc_mem);
	this->task_num = task_num;
	this->duplicatePolicy = duplicatePolicy;

	// init block pool
	std::list<Node*> v;
	for(int i=0;i<task_num;i++){
		for(int j=0; j<2500; j++){
			v.push_back((Node*)bp->alloc(i));
		}
	}
	for(int i=0;i<task_num;i++){
		for(int j=0; j<2500; j++){
			bp->free(v.front(),i);
			v.pop_front();
		}
	}
	haz = new HazardTracker(task_num, bp, 3, 3);
}

MichaelOrderedMap::findInfo MichaelOrderedMap::find(int32_t key, bool findMin, int tid){
	findInfo f;
	mptr_local<Node> oldptr;
	mptr_local<Node> newptr;
	while(true){
		f.prev = &head;
		f.cur.init(f.prev->all());
		f.next.init(0);

		haz->reserve(f.cur.ptr(),1,tid); // reserve cur node
		if(*f.prev!=f.cur){break;} // snapshot after reserve

		while(true){
			if(f.cur.ptr()==NULL){f.found=false;return f;}  // reached end of list
			f.next.init(f.cur.ptr()->next.all()); // read next node's next pointer

			haz->reserve(f.next.ptr(),0,tid); // reserve next
			if(f.cur->next!=f.next){break;} // snapshot after reserve

			int32_t ckey = f.cur.ptr()->key; // copy current key
			oldptr.init(false,f.cur.ptr(),f.cur.sn()); // get snapshot of prv, cur
			if(f.prev->all() != oldptr.all()){break;} // verify snapshot of prev, cur

			if(!f.next.marked()){ // check if cur deleted
				if(ckey>=key || findMin){ // check if found key's slot, or if finding min
					f.found = (ckey==key) || findMin; // check if actually found key
					return f; 
				}
				f.prev = &(f.cur.ptr()->next); // iterate prev forward
				haz->reserve(f.cur.ptr(),2,tid);
			}
			else{  // cur is deleted, we should clean
				oldptr.init(false,f.cur.ptr(),f.cur.sn());
				newptr.init(false,f.next.ptr(),f.cur.sn()+1);
				if(f.prev->CAS(oldptr,newptr)){
					haz->retire(f.cur.ptr(),tid);
					f.next.init(f.next.marked(),f.next.ptr(),f.cur.sn()+1); // increment sn since we CAS'd
				}
				else{break;} // our CAS to remove failed, so try again at current location
			}
			f.cur.init(f.next); // iterate cur forward
			haz->reserve(f.next.ptr(),1,tid);
		}
	}
}

MichaelOrderedMap::findInfo MichaelOrderedMap::find(int32_t key, int tid){
	return find(key,false,tid);
}
MichaelOrderedMap::findInfo MichaelOrderedMap::findMin(int tid){
	return find(0,true,tid);
}
int32_t MichaelOrderedMap::minKey(int tid){
	int32_t ret;
	findInfo f = findMin(tid);
	if(f.cur.ptr()==NULL){
		ret = EMPTY;
	}
	else{
		ret = f.cur.ptr()->key;
	}
	haz->clearAll(tid);
	return ret;
}


KeyVal MichaelOrderedMap::peekMin(int tid){
	findInfo f = findMin(tid);
	int32_t val;
	if(f.cur.ptr()!=NULL){
		val = f.cur.ptr()->val;
	}
	else{
		val = EMPTY;
	}
	KeyVal kv;
	kv.key = f.cur.all(); 
	kv.val = val;
	haz->clearAll(tid);
	return kv;
}

// NOTE: remove_cond will still remove in the 
// case of ABA problem with head of map.
bool MichaelOrderedMap::removeMin_cond(uint64_t peekKey, int tid){
	mptr_local<Node> oldptr;
	mptr_local<Node> newptr;
	findInfo f;	
	// find node
	f = findMin(tid);
	if((!f.found) || (f.cur.all()!=peekKey)){
		// no longer head or empty
		haz->clearAll(tid);
		return false;
	} 

	// mark as deleted
	oldptr.init(false,f.next.ptr(),f.next.sn());
	newptr.init(true,f.next.ptr(),f.next.sn()+1);
	if(!f.cur->next.CAS(oldptr,newptr)){
		haz->clearAll(tid);
		return false;
	}

	// remove
	oldptr.init(false,f.cur.ptr(),f.cur.sn());
	newptr.init(false,f.next.ptr(),f.cur.sn()+1);
	if(f.prev->CAS(oldptr,newptr)){
		haz->retire(f.cur.ptr(),tid);
	}
	else{
		findMin(tid); // clean up if necessary
	}
	haz->clearAll(tid);
	return true;
}

bool MichaelOrderedMap::map(int key, int32_t val,int tid) {
	assert(val!=0); // 0 used as EMPTY signal
	assert(key!=0); // 0 used as EMPTY signal

	// init node
	Node* node;
	node = (Node*)bp->alloc(tid);
	findInfo f;	

	mptr_local<Node> oldptr;
	mptr_local<Node> newptr;

	bool ret;

	// insert node
	while(true){
		f = find(key,tid);
		if(f.found){
			if(duplicatePolicy == rejectDuplicates){
				ret = false; break;
			}
			else if(duplicatePolicy == replaceDuplicates){
				int32_t oldval = f.cur.ptr()->val;
				int32_t newval = val;
				if(f.cur.ptr()->val.compare_exchange_strong(oldval,newval,
				  std::memory_order::memory_order_acq_rel)){
					ret = true; break;
				}
				else{continue;}
			}
			// else if (duplicatePolicy == appendDuplicates), fall through and append
		}
		// insert new node
		node->init(key,val,f.cur.ptr());
		oldptr.init(false,f.cur.ptr(),f.cur.sn());
		newptr.init(false,node,f.cur.sn()+1);
		if(f.prev->CAS(oldptr,newptr)){ret = true; break;}
	}
	haz->clearAll(tid);
	return ret;
}

int32_t MichaelOrderedMap::get(int32_t key, int tid){
	assert(key!=0); // 0 used as EMPTY signal
	int32_t ret;

	findInfo f;	
	f = find(key,tid);
	if(f.found){
		ret = f.cur.ptr()->val;
	}
	else{
		ret = EMPTY;
	}
	haz->clearAll(tid);
	return ret;
}

int32_t MichaelOrderedMap::unmap(int key,int tid) {

	mptr_local<Node> oldptr;
	mptr_local<Node> newptr;
	findInfo f;	
	int32_t ret;

	// find node
	while(true){
		f = find(key,tid);
		if(!f.found){ret = EMPTY; break;} // didn't find key

		// mark as deleted
		oldptr.init(false,f.next.ptr(),f.next.sn());
		newptr.init(true,f.next.ptr(),f.next.sn()+1);
		ret = f.cur.ptr()->val;
		int k = f.cur.ptr()->key;
		if(!f.cur->next.CAS(oldptr,newptr)){continue;}
		assert(k==key);

		// remove
		oldptr.init(false,f.cur.ptr(),f.cur.sn());
		newptr.init(false,f.next.ptr(),f.cur.sn()+1);
		if(f.prev->CAS(oldptr,newptr)){
			haz->retire(f.cur.ptr(),tid);
		}
		else{
			find(key,tid); // clean up if necessary
		}
		break;
	}
	haz->clearAll(tid);
	return ret;
}

int32_t MichaelPriorityQueue::remove(int tid){
	/*int32_t rtn;
	int32_t key=map.minKey(tid);
	while(key!=0){
		rtn = map.unmap(key,tid); // fails if key changes value in between peek and remove
		if(rtn!=0){return rtn;}
		key=map.minKey(tid);
	}
	return EMPTY;*/
	KeyVal kv = map.peekMin(tid); // key here is NOT normal key, but peek key describing head of queue
	while(kv.val!=EMPTY){
		if(map.removeMin_cond(kv.key, tid)){return kv.val;}
		kv = map.peekMin(tid);
	}
	return EMPTY;
}
void MichaelPriorityQueue::insert(int32_t e, int tid){
	map.map(e,e,tid);
}

KeyVal MichaelPriorityQueue::peek(int tid){
	KeyVal kv = map.peekMin(tid);
	return kv;
}
bool MichaelPriorityQueue::remove_cond(uint64_t peekKey, int tid){
	return map.removeMin_cond(peekKey, tid);
}


int32_t MichaelOrderedSet::remove(int32_t e, int tid){
	return map.unmap(e,tid);
}
bool MichaelOrderedSet::insert(int32_t e, int tid){
	return map.map(e,e,tid);
}
bool MichaelOrderedSet::contains(int32_t e, int tid){
	return map.get(e,tid)!=0;
}
