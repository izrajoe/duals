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



#include "GenericDual.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <stdint.h>

using namespace std;

GenericDual::GenericDual(RContainer* dataqueue,RContainer* antidataqueue, bool nonBlocking, int task_num, bool glibc_mem){
	int i;
	int j;
	q_array[DATA] = dataqueue;
	q_array[ANTIDATA] = antidataqueue;
	this->task_num = task_num;

	if(nonBlocking){
		antiContainer = dynamic_cast<RPeekableContainer*>(antidataqueue);
	}

	// init block pool
	std::list<placeholder*> v;
	bp = new BlockPool<placeholder>(task_num,glibc_mem);
	for(i=0;i<task_num;i++){
		for(j=0; j<500000; j++){ //25000
			//v.push_back((placeholder*)bp->alloc(i));
		}
	}
	for(i=0;i<task_num;i++){
		for(j=0; j<500000; j++){
			//bp->free(v.front(),i);
			//v.pop_front();
		}
	}

	bpReq = new BlockPool<Request>(task_num,glibc_mem);

	this->hazPh = new HazardTracker(task_num, bp, 1, 5,true);
	this->hazReq = new HazardTracker(task_num, bpReq, 1, 5,true);
	this->nonBlocking=nonBlocking;
	this->activeRequest.storeNull();
}


void GenericDual::conclude(){
	int i;
	i = 0;
	while(true){
		placeholder* p = (placeholder*)(q_array[DATA]->remove(i%task_num));
		if(p==NULL){break;}
		bp->free(p,i%task_num);
		i++;
	}
	cout<<"data.size@End="<<i<<endl;
	i = 0;
	while(true){
		placeholder* p = (placeholder*)(q_array[ANTIDATA]->remove(i%task_num));
		if(p==NULL){break;}
		bp->free(p,i%task_num);
		i++;
	}
	cout<<"antidata.size@End="<<i<<endl;
}

inline int32_t GenericDual::finished_insert(placeholder* ph, bool polarity,int tid){
	int32_t val;
	if(polarity==DATA){// I am DATA
		return OK; // we're successfully insertd, so we're done
	}
	else{// I am ANTIDATA
		int i = 0;
		assert(ph->aborted() !=1);
		assert(ph->valid() ==1);
		while(ph->sat()==false){
			assert(ph->val()==0 || ph->sat());
		} // spin waiting for data

		val = ph->val();
		return val;
	}
}


inline int32_t GenericDual::mix(placeholder* ph, placeholder* opp_ph,bool polarity,int tid){
	int i;
	assert(opp_ph->valid());

	if(polarity==DATA){ // I am DATA
		bool b = opp_ph->satisfy(ph->val());
		assert(b);
		return OK;
	}
	else{// I am ANTIDATA
		int32_t val;
		val = opp_ph->val();
		return val;
	}
}

inline GenericDual::placeholder* GenericDual::allocPlaceholder(int32_t val, int tid){
	placeholder* ph = bp->alloc(tid);
	if(ph==NULL){// we ran out of memory...
		errexit("Out of memory on placeholder alloc!\n");
	}
	ph->init(val,INVALID); 
	return ph;
}

inline int32_t GenericDual::validateAndComplete(placeholder* ph, int32_t val, bool polarity, int tid){
	placeholder_local swap_old;
	placeholder_local swap_new;
	swap_old.init(val,INVALID);
	swap_new.init(val,VALID);
	if(ph->CAS(swap_old,swap_new)){
		assert(ph->aborted()==0);
		assert(ph->valid()==1);
		return finished_insert(ph,polarity,tid);
	}
	return EMPTY; // failed to validate
}

inline int32_t GenericDual::doOppositeCheck(placeholder* ph, bool polarity, bool nb, int tid){
	if(!nb){
		return oppositeCheck(ph,polarity,nb,tid);
	}
	else{
		return oppositeCheckNB(ph,polarity,nb,tid);
	}
}

inline int32_t GenericDual::oppositeCheck(placeholder* ph, bool polarity, bool nb, int tid){

	placeholder_local swap_old;
	placeholder_local swap_new;
	placeholder* opp_ph=NULL;
	int32_t remove_val;
	placeholder_local opp_contents;

	int32_t ret = EMPTY;

	// loop to remove until empty or valid entry in opposite queue
	while(true){ 
		remove_val = q_array[!polarity]->remove(tid);

		if(remove_val == EMPTY){
			ret = EMPTY;
			break;
		}
		else{
			assert(remove_val!=NULL_VAL);
			assert(remove_val!=(int32_t)NULL);
			opp_ph = (placeholder*)remove_val;
			assert(polarity==ANTIDATA || opp_ph->val()==(int32_t)NULL);
			assert(opp_ph->aborted()!=true);
			assert(opp_ph->sat()!=true);

			opp_contents.init(opp_ph->all);

			// attempt abort operation
			swap_old.init(opp_contents.val(),INVALID);
			swap_new.init(opp_contents.val(),ABORTED);
			if(!opp_ph->CAS(swap_old,swap_new)){
				// opp_ph is valid
				ret = mix(ph,opp_ph,polarity,tid); // mix with opposites
				retire(opp_ph,tid);
				break; // return
			}

			assert(opp_contents.valid()==0);
			assert(opp_ph->valid()==0);
			assert(opp_ph->aborted()==1);
			// else, we removed an invalid (unverified) placeholder, loop to get next one
			// at this point, opp_ph is aborted, can't be verified or satisfied
			retire(opp_ph,tid);
		}
	} // end removing loop

	return ret;
}



inline int32_t GenericDual::oppositeCheckNB(placeholder* ph, bool polarity, bool nb, int tid){

	placeholder* opp_ph=NULL;
	int32_t remove_val;
	cptr_local<Request> activeCopy;
	Request* myRequest = bpReq->alloc(tid);
	myRequest->val = ph->val();
	KeyVal kv;


	int32_t ret = EMPTY;

	// loop to remove until empty or valid entry in opposite queue
	while(true){ 
		
		clearHazards(tid);

		// READ
		activeCopy = activeRequest; // read active request
		if(activeCopy.ptr()!=NULL){
			// helping some other thread here, since an active request exists
			reserveHazard(activeCopy.ptr(),tid);
			if(activeCopy.all() != activeRequest.all()){continue;} // snapshot request haz ptr
			reserveHazard(activeCopy.ptr()->ph,tid);
			if(activeCopy.all() != activeRequest.all()){continue;} // snapshot ph haz ptr
			helpRequestNB(activeCopy,tid); // after setting hazard pointers, help active request
			continue; // now loop back and try to complete myself
		}

		// PEEK
		kv = antiContainer->peek(tid); // peek opposite container
		remove_val = kv.val;
		if(remove_val == EMPTY){
			ret = EMPTY; 
			break; // if it's empty, our opposite empty check is complete
		}
		assert(kv.key!=(int32_t)NULL);
		myRequest->key = kv.key;
		assert(remove_val!=(int32_t)NULL);
		opp_ph = (placeholder*)remove_val;
		myRequest->ph = opp_ph;
		reserveHazard(opp_ph,tid); // all peekers need to reserve the placeholders they're viewing

		// POST
		reserveHazard(myRequest,tid);
		if(activeRequest.CAS(activeCopy,myRequest)){ // post my request as active
			// posted my request (and validate peek snapshot for GC)
			activeCopy.init(myRequest,activeCopy.sn()+1);
			assert(activeRequest.all()==activeCopy.all() || activeRequest.sn()>activeCopy.sn());
			//printf("doing %x\n",activeCopy.ptr());
			if(helpRequestNB(activeCopy,tid)!=ABORTED){
				// my request was satisfied (by someone)
				if(opp_ph->req()==myRequest){
					ret = OK;
					assert(opp_ph->state()==SATISFIED);
					assert(opp_ph->val()==ph->val());
					// the placeholder may have been satisfied a long time ago.
					// How do we know if my request worked or not?
					// because it was satisfied with MY unique value
					myRequest=NULL;
					break;
				}
			}
			// my request was either aborted or previously satisfied by another peeker.
			// so reallocate and retry
			hazReq->retire(myRequest,tid); // retire my request 
			myRequest = bpReq->alloc(tid); // myRequest was aborted, get new one
			myRequest->val = ph->val();
		}
	} // end removing loop

	assert(myRequest==NULL || myRequest->val==ph->val());
	hazReq->retire(myRequest,tid); 
	clearHazards(tid);
	return ret;
}


inline uint64_t GenericDual::helpRequestNB(cptr_local<Request> req, int tid){

	uint64_t ret;
	placeholder_local swap_old;
	placeholder_local swap_new;

	placeholder* opp_ph = req->ph;

	// dereference opposite placeholder
	placeholder_local opp_contents;
	opp_contents.init(opp_ph->all);

	// attempt abort opposite operation
	swap_old.init(opp_contents.val(),INVALID);
	swap_new.init(opp_contents.val(),ABORTED);
	if(opp_ph->CAS(swap_old,swap_new)){
		// abort succeeded
		ret = ABORTED;
		assert(opp_ph->state()==ABORTED);
		assert(opp_contents.state()==INVALID);
	}	
	else if(opp_ph->state()==ABORTED){
		// someone else aborted, so return
		ret = ABORTED;
		assert(opp_ph->state()==ABORTED);
	}
	else{
		// opp_ph must be valid or satisfied
		if(opp_ph->satisfy(req->val,req.ptr())){
			// satisfied opposite
			ret = SATISFIED;
			assert(opp_ph->state()==SATISFIED);
			assert(opp_ph->val()!=0);
			assert(opp_ph->req()==req.ptr());
		}
		else{
			// someone else satisfied, so return
			ret = SATISFIED;
			assert(opp_ph->state()==SATISFIED);
			assert(opp_ph->val()!=0);
		}
	}
	// take down posted request
	if(activeRequest.CAS(req, NULL)){
		//printf("takedown %x\n",req.ptr());
	}
	// remove placeholder from opposite
	if(antiContainer->remove_cond(req->key,tid)){
		//printf("remove %x\n",req.ptr());
		hazReq->retire(opp_ph->req(),tid); // retire satisfying request
		retire(opp_ph,tid); // retire opposite placeholder
		
	}

	return ret;
}



inline int32_t GenericDual::remsert(int32_t val,bool polarity,int tid){
	placeholder* ph=NULL;
	bool nb = nonBlocking && (polarity == DATA);
	int contentioncounter=0;
	int32_t ret=EMPTY;

	// allocate placeholder
	ph = allocPlaceholder(val, tid);// reusing this is NOT SAFE, we are guaranteed to be on a retired list

	// precheck optimization 
	ret = doOppositeCheck(ph, polarity, nb, tid);
	if(ret!=EMPTY){
		retire(ph, tid); // extra retire because we didn't insert our placeholder into the queue
	}

	// actual transaction attempt
	while(ret==EMPTY){
		// begin transaction by emplacing placeholder
		q_array[polarity]->insert((int32_t)ph,tid);		

		// do empty check on opposite ...
		ret = doOppositeCheck(ph, polarity, nb, tid);
		if(ret!=EMPTY){break;} // satisfied opposite, so done.

		// empty check failed ....
		// so now we now try to validate our placeholder
		ret = validateAndComplete(ph, val, polarity, tid);
		if(ret!=EMPTY){break;} // validated self and waited if necessary, so done.

		assert(ph->val() == val);

		// else, we couldn't validate our placeholder
		// which means someone aborted us....
		// we need to retry the transaction
		
		// so clean up old placeholder
		//clearHazards(tid);
		retire(ph,tid);
		// manage contention if necessary
		contentioncounter++;
		if(contentioncounter>0){contention_manager(polarity,tid);}

		// get a new placeholder and try again
		ph = allocPlaceholder(val,tid);
	}
	//clearHazards(tid);
	retire(ph, tid);
	

	return ret;
}




inline void GenericDual::reserveHazard(placeholder* ph, int tid){
	if(nonBlocking){
		hazPh->reserve(ph,0,tid);
	}
}
inline void GenericDual::reserveHazard(Request* req, int tid){
	if(nonBlocking){
		hazReq->reserve(req,0,tid);
	}
}
inline void GenericDual::clearHazards(int tid){
	if(nonBlocking){
		hazPh->clearAll(tid);
		hazReq->clearAll(tid);
	}
}


inline void GenericDual::retire(placeholder* ph, int tid){
	// no precondition can change once we are retired,
	// so if cas fails, someone else abandoned the placeholder
	if(ph->abandon()){
		// we successfully abandoned the placeholder.
		// the opposite thread must clean up
		return;
	}

	if(nonBlocking){
		hazPh->retire(ph,tid);
	}
	else{
		bp->free(ph,tid);
	}
}



int32_t GenericDual::remove(int tid){
	int32_t rtn;
	rtn =  remsert((int32_t)NULL,ANTIDATA,tid);
	return rtn;
}
void GenericDual::insert(int32_t val, int tid){
	int32_t rtn;
	rtn= remsert(val,DATA,tid);
	return;
}



inline void GenericDual::contention_manager(bool polarity,int tid){
	//cout<<"contention"<<endl;
	if(ANTIDATA==polarity){
		//usleep(1);
		//usleep(task_num/2);
		//contention = antidata;
	}	
	return;
}

GenericDual::~GenericDual(){
	//delete[] retired;
	//delete[] hazard;

}


