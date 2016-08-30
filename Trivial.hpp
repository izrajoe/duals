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



// Treiber's stack.
//
// See:
//   R. K. Treiber. Systems programming: Coping with parallelism.
//   Technical report RJ 5118. IBM Almaden Research Center. April 1986.
// 

// based on pseudo code from 
// http://www.mpi-sws.org/~viktor/cave/examples/Treiber.cav

#ifndef TRIVIAL_HPP
#define TRIVIAL_HPP

#include "Rideable.hpp"
#include "ConcurrentPrimitives.hpp"
#include <stdio.h>
#include <list>

using namespace std;

class Trivial : public virtual Rideable, public Reportable{

	padded<std::list<void*>>* retired;

	public:
	Trivial(int task_num, bool glibc_mem){
		retired = new padded<list<void*>>[task_num];
		for (int i = 0; i<task_num; i++){
			retired[i].ui = list<void*>();
		}
		retired[0].ui.push_back(new int(3));
	}

	void conclude(){

	}

};

class TrivialFactory : public RideableFactory{
	Trivial* build(GlobalTestConfig* gtc){
		return new Trivial(gtc->task_num,gtc->environment["glibc"]=="1");
	}
};

#endif


