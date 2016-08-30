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



#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "Harness.hpp" // main harness header

// local headers
#include "Tests.hpp"
#include "TreiberStack.hpp"
#include "MSQueue.hpp"
#include "MichaelOrderedSet.hpp"
#include "GenericDual.hpp"
#include "LCRQ.hpp"
#include "Trivial.hpp"
#include "FCDualQueue.hpp"
#include "SSDualQueue.hpp"
#include "MPDQ.hpp"
#include "SPDQ.hpp"

using namespace std;

GlobalTestConfig* gtc;

// the main function
// sets up output and tests
int main(int argc, char *argv[])
{

	gtc = new GlobalTestConfig();
	gtc->addRideableOption(new SGLQueueFactory(), "SGLQueue (linked list)");
	gtc->addRideableOption(new TreiberStackFactory(), "Treiber Stack");
	gtc->addRideableOption(new MSQueueFactory(), "MSQueue");
	gtc->addRideableOption(new MichaelPriorityQueueFactory(), "MH PriorityQueue");
	gtc->addRideableOption(new LCRQFactory(), "LCRQ");

	gtc->addRideableOption(new GenericDualFactory(new MSQueueFactory(), new MSQueueFactory(),false), "GenericDual (MSQ:MSQ)");
	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), new MSQueueFactory(),false), "GenericDual (LCRQ:MSQ)");
	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), new TreiberStackFactory(),false), "GenericDual (LCRQ:TStack)");
	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), 
	  new MichaelPriorityQueueFactory(),false), "GenericDual (LCRQ:MHOL)");

	gtc->addRideableOption(new GenericDualFactory(new MSQueueFactory(), new MSQueueFactory(),true), "GenericDualNB (MSQ:MSQ)");
	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), new MSQueueFactory(),true), "GenericDualNB (LCRQ:MSQ)");
	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), new TreiberStackFactory(),true), "GenericDualNB (LCRQ:TStack)");
	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), 
	  new MichaelPriorityQueueFactory(),true), "GenericDualNB (LCRQ:MHOL)");


	//gtc->addRideableOption(new TrivialFactory(), "Trivial");
	//gtc->addRideableOption(new GenericDualFactory(new TreiberStackFactory(), new TreiberStackFactory(),false), "GenericDual (TS:TS)");
	gtc->addRideableOption(new FCDualQueueFactory(), "FCDualQueue");
	gtc->addRideableOption(new SSDualQueueFactory(), "SSDualQueue");
	gtc->addRideableOption(new MPDQFactory(false), "MPDQ Blocking");
	gtc->addRideableOption(new MPDQFactory(true), "MPDQ Nonblocking");
	gtc->addRideableOption(new SPDQFactory(false), "SPDQ Blocking");
	gtc->addRideableOption(new SPDQFactory(true), "SPDQ Nonblocking");

	gtc->addRideableOption(new GenericDualFactory(new LCRQFactory(), 
	  new LCRQFactory(),false), "GenericDual (LCRQ:LCRQ)");


	gtc->addTestOption(new FAITest(), "FAI Test");
	gtc->addTestOption(new PotatoTest(0), "PotatoTest(0 ms delay)");
	gtc->addTestOption(new PotatoTest(1), "PotatoTest(1 ms delay)");
	gtc->addTestOption(new PotatoTest(2), "PotatoTest(2 ms delay)");
	gtc->addTestOption(new InsertRemoveTest(), "InsertRemoveTest");
	//gtc->addTestOption(new QueueVerificationTest(), "QueueVerification Test");
	//gtc->addTestOption(new StackVerificationTest(), "StackVerification Test");
	gtc->addTestOption(new NothingTest(), "Nothing Test");
	//gtc->addTestOption(new MarkedPtrTest(), "MarkedPtrTest");
	//gtc->addTestOption(new MapUnmapTest(), "MapUnmapTest");
	//gtc->addTestOption(new MapVerificationTest(), "MapVerificationTest");

	try{
		gtc->parseCommandLine(argc,argv);
	}
	catch(...){
		//delete gtc;
		//cout<<"exit"<<endl;
		return 0;
	}

	if(gtc->verbose){
		fprintf(stdout, "Testing:  %d threads for %lu seconds on %s using %s\n",
		  gtc->task_num,gtc->interval,gtc->getTestName().c_str(),gtc->getRideableName().c_str());
	}

	// register fancy seg fault handler to get some
	// info in case of crash
	signal(SIGSEGV, faultHandler);

	// do the work....
	try{
		gtc->runTest();
	}
	catch(...){
		//delete gtc;
		return 0;
	}


	// print out results
	if(gtc->verbose){
		printf("Operations/sec: %ld\n",gtc->total_operations/gtc->interval);
	}
	else{
		printf("%ld \t",gtc->total_operations/gtc->interval);
	}

	return 0;
}








