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



#ifndef TESTS_HPP
#define TESTS_HPP

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif
#include <atomic>
#include "Harness.hpp"
#include "RDualContainer.hpp"
#include "MichaelOrderedSet.hpp"

class PotatoTest : public Test{


private:
	UIDGenerator* ug;
	int hotPotatoPenalty=0;
	inline int executeQueue(GlobalTestConfig* gtc, LocalTestConfig* ltc);
	inline int executeDualQueue(GlobalTestConfig* gtc, LocalTestConfig* ltc);

public:
	PotatoTest(int delay){hotPotatoPenalty = delay;}
	PotatoTest(){}
	RContainer* q;
	RDualContainer* dq;
	void init(GlobalTestConfig* gtc);
	int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
	void cleanup(GlobalTestConfig* gtc);
};

class MarkedPtrTest : public SequentialTest{
public:
	void init(GlobalTestConfig* gtc){}
	int execute(GlobalTestConfig* gtc);
	void cleanup(GlobalTestConfig* gtc){}
};



















#endif
