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



#ifndef RDDUALQUEUE_HPP
#define RDDUALQUEUE_HPP

#include <assert.h>
#include <stddef.h>
#include <atomic>
#include "Harness.hpp"
#include "ConcurrentPrimitives.hpp"

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif

#define NULL_VAL 0

#define CLOSED 0
#define VERIFY_FAIL 0


#define DONE_FAIL 2
#define INCREASE_FAIL 3
#define DECREASE_FAIL 4
#define RING_NOT_READY 7

#define DATA 0
#define ANTIDATA 1



class KeyVal{
public:
	int32_t val;
	uint64_t key;
	bool operator==(const struct KeyVal  &x){
		return (val==x.val) && (key==x.key);
	}
	bool operator!=(const struct KeyVal  &x){
		return !((*this)==x);
	}
};

class RPeekableContainer : public virtual RContainer{
public:
	virtual int32_t remove(int tid)=0;
	virtual void insert(int32_t val,int tid)=0;
	virtual KeyVal peek(int tid)=0;
	virtual bool remove_cond(uint64_t key, int tid)=0;
};

class RDualContainer : public virtual RContainer{
public:
	virtual int32_t remove(int tid)=0;
	virtual void insert(int32_t val,int tid)=0;
};

#endif
