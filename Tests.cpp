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




#include "Tests.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <climits>

using namespace std;

// PotatoTest methods
void PotatoTest::init(GlobalTestConfig* gtc){
	Rideable* ptr = gtc->allocRideable();
	this->dq = dynamic_cast<RDualContainer*>(ptr);
	if (!dq) {
		this->q = dynamic_cast<RContainer*>(ptr);
		if(!q){
			errexit("PotatoTest must be run on RQueue or RDualQueue type object.");
		}
		if(gtc->verbose){
			cout<<"Running PotatoTest on total container."<<endl;
		}
	}
	else{
		if(gtc->verbose){
			cout<<"Running PotatoTest on partial container."<<endl;
		}
	}
	gtc->recorder->addThreadField("insOps",&Recorder::sumInts);
	gtc->recorder->addThreadField("insOps_stddev",&Recorder::stdDevInts);
	gtc->recorder->addThreadField("insOps_each",&Recorder::concat);
	gtc->recorder->addThreadField("remOps",&Recorder::sumInts);
	gtc->recorder->addThreadField("remOps_stddev",&Recorder::stdDevInts);
	gtc->recorder->addThreadField("remOps_each",&Recorder::concat);
	ug = new UIDGenerator(gtc->task_num);
}

int PotatoTest::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
	if(dq!=NULL){
		return executeDualQueue(gtc,ltc);
	}
	else{
		return executeQueue(gtc,ltc);
	}
}

int PotatoTest::executeQueue(GlobalTestConfig* gtc, LocalTestConfig* ltc){

	struct timeval time_up = gtc->finish;
	struct timeval now;
	gettimeofday(&now,NULL);
	int ops = 0;

	int insOps = 0;
	int remOps = 0;

	unsigned int r = ltc->seed;
	int tid = ltc->tid;
	bool hot = false;
	int j;

	if(tid == 0){
		hot = true;
	}
	
	int32_t inserting = ug->initial(tid);


	while(now.tv_sec < time_up.tv_sec 
		|| (now.tv_sec==time_up.tv_sec && now.tv_usec<time_up.tv_usec) ){
		r = nextRand(r);

		if(hot || r%2==0){
			insOps++;
			inserting = ug->next(inserting,tid);
			if(inserting==0){
				cout<<"Overflow on thread "<<tid<<". Terminating its execution."<<endl;
				break;
			}
		}

		if(hot){
			usleep(hotPotatoPenalty);
			q->insert(-1*inserting,tid);
			hot = false;
			insOps++;
		}
		else if(r%2==0){
			q->insert(inserting,tid);
			insOps++;
		}
		else{
			j=EMPTY;
			while(j==EMPTY){
				j=q->remove(tid);
			}
			if(j<0){
				hot = true;
			}
			remOps++;
		}
		ops++;
		gettimeofday(&now,NULL);
	}
	inserting = ug->next(inserting,tid);
	q->insert(inserting,tid);


	gtc->recorder->reportThreadInfo("insOps",insOps,ltc->tid);
	gtc->recorder->reportThreadInfo("insOps_stddev",insOps,ltc->tid);
	gtc->recorder->reportThreadInfo("insOps_each",insOps,ltc->tid);
	gtc->recorder->reportThreadInfo("remOps",remOps,ltc->tid);
	gtc->recorder->reportThreadInfo("remOps_stddev",remOps,ltc->tid);
	gtc->recorder->reportThreadInfo("remOps_each",remOps,ltc->tid);

	return ops;

}

int PotatoTest::executeDualQueue(GlobalTestConfig* gtc, LocalTestConfig* ltc){

	struct timeval time_up = gtc->finish;
	struct timeval now;
	gettimeofday(&now,NULL);
	int ops = 0;
	int insOps=0;
	int remOps = 0;
	unsigned int r = ltc->seed;
	int tid = ltc->tid;
	bool hot = false;
	int j;

	if(tid == 0){
		hot = true;
	}

	int32_t inserting = ug->initial(tid);


	while(now.tv_sec < time_up.tv_sec 
		|| (now.tv_sec==time_up.tv_sec && now.tv_usec<time_up.tv_usec) ){
		r = nextRand(r);

		if(hot || r%2==0){
			insOps++;
			inserting = ug->next(inserting,tid);
			if(inserting==0){
				cout<<"Overflow on thread "<<tid<<". Terminating its execution."<<endl;
				break;
			}
		}

		if(hot){
			usleep(hotPotatoPenalty);
			dq->insert(-1*inserting,tid);
			hot = false;
		}
		else if(r%2==0){
			dq->insert(inserting,tid);

		}
		else{
			j=dq->remove(tid);
			if(j<0){
				hot = true;
				//printf("hot %d ==> %d\n",(j>>1)-1,tid);
			}
			remOps++;
		}
		ops++;
		gettimeofday(&now,NULL);
	}
	inserting = ug->next(inserting,tid);
	dq->insert(inserting,tid);

	gtc->recorder->reportThreadInfo("insOps",insOps,ltc->tid);
	gtc->recorder->reportThreadInfo("insOps_stddev",insOps,ltc->tid);
	gtc->recorder->reportThreadInfo("insOps_each",insOps,ltc->tid);
	gtc->recorder->reportThreadInfo("remOps",remOps,ltc->tid);
	gtc->recorder->reportThreadInfo("remOps_stddev",remOps,ltc->tid);
	gtc->recorder->reportThreadInfo("remOps_each",remOps,ltc->tid);

	return ops;
}

void PotatoTest::cleanup(GlobalTestConfig* gtc){}



int MarkedPtrTest::execute(GlobalTestConfig* gtc){
	mptr_local<int32_t> ml2,ml1;
	mptr<int32_t> m;
	int32_t* p = (int32_t*)1;
	int32_t sn = 2;
	bool marked = false;

	// init nonlocal
	m.init(marked,p,sn);
	assert(m.sn()==sn);
	assert(m.ptr()==p);
	assert(m.marked()==marked);

	// init local 1
	ml1.init(m.all());
	assert(ml1.sn()==sn);
	assert(ml1.ptr()==p);
	assert(ml1.marked()==marked);
	assert(m.all() == ml1.all());

	// init local 2
	int32_t* p2 = (int32_t*)10;
	int32_t sn2 = 20;
	bool marked2 = true;

	ml2.init(marked2,p2,sn2);
	assert(ml2.sn()==sn2);
	assert(ml2.ptr()==p2);
	assert(ml2.marked()==marked2);

	// CAS test
	assert(m.CAS(ml1,ml2));
	assert(m.sn()==sn+1);
	assert(m.ptr()==p2);
	assert(m.marked()==marked2);
	ml2.init(m);
	assert(ml2.all() == m.all());	

	assert(m.CAS(ml2,ml1));
	assert(m.sn()==ml2.sn()+1);
	assert(m.ptr()==p);
	assert(m.marked()==marked);

	return 1;
}






