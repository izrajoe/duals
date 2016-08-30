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





#include "FCDualQueue.hpp"
// TODO: sometimes hangs on potato test


using namespace std;

// constructor
FCDualQueue::FCDualQueue(int task_num, bool glibc_mem){
	this->task_num=task_num;
	fc_lock.store(0);
	main_ds.clear();
	consumers_caches = new padded<SimpleRing<ThreadNode*>>[task_num];
	int numlines;
	numlines = (task_num*sizeof(ThreadNode*))/LEVEL1_DCACHE_LINESIZE + 1;
	int size;
	size = numlines * (double)LEVEL1_DCACHE_LINESIZE / (sizeof(ThreadNode*));

	thread_requests = new ThreadNode[task_num];
	for(int i = 0; i<task_num; i++){
		//consumers_caches[i].ui = circular_buffer<ThreadNode*>(task_num!=1?task_num:2);
		consumers_caches[i].ui.resize(size);
		thread_requests[i].set(false,false,0);
	}

	/*for(int i = 0; i<300000; i++){
		main_ds.push_back(i);
	}
	main_ds.clear();*/


}


void FCDualQueue::doFlatCombining(int tid){       
	SimpleRing<ThreadNode*>* consumers_array = &(consumers_caches[tid].ui);
	bool finished = false;
	ThreadNode* my_node = &this->thread_requests[tid];

	assert(fc_lock==tid+1);

	for(int j =0; j<MAX_COMBINING_ROUNDS && !finished; j++){
		consumers_array->clear();
		for(int i = 0; i<task_num;i++){ // iterate over all requests
			ThreadNode* cur_node = &this->thread_requests[i];
			if(!cur_node->is_val()){continue;} // if it isn't valid, skip it

			// if it's valid, it can't be changed by anyone other than the combiner
			assert(cur_node->is_val());
			if(!cur_node->is_consumer()){
				// if producer, store the value in the data structure
				int32_t item = cur_node->item();
				//cout<<"ins"<<endl;
				//if(item<0){cout<<"hot ins"<<endl;}
				main_ds.push_back(item);
				assert(cur_node->is_val() && !cur_node->is_consumer());
				cur_node->set(false,false,0);
				if(cur_node==my_node){finished=true;}
				
			}
			else{
				// if consumer, cache request for later in traverse
				consumers_array->push_back(cur_node);
				assert(cur_node->is_val() && cur_node->is_consumer());
			}


			// match outstanding consumers
			if(consumers_array->size()!=0 && main_ds.size()!=0){
				int32_t item =main_ds.front();
				//if(item<0){cout<<"hot rem"<<endl;}
				//cout<<"rem"<<endl;
				ThreadNode* cons_node=consumers_array->back();
				consumers_array->pop_back();
				main_ds.pop_front();
				assert(cons_node->is_val());
				if(cons_node==my_node){finished=true;}
				cons_node->set(false,false,item);
			}	

		}// end inner loop
		assert(consumers_array->size()==0 || main_ds.size()==0);
	}

	assert(fc_lock==tid+1);
}


//void inline ThreadNode::set(bool is_consumer, bool is_val, int32_t item)

void FCDualQueue::insert(int32_t value,int tid){

	//if(cur_node->item()==1000){printf("hot:%d",tid);}

	// Initialize request
	ThreadNode* thread_node = &this->thread_requests[tid];
	assert(!thread_node->is_val());
	thread_node->set(false,true,value);
	//if(value<0){cout<<"hot post"<<endl;}
	int rounds = 0;

	// wait for combining
	while(thread_node->is_val()){

		// Try to combine
		if (rounds%COMBINING_LIST_CHECK_FREQUENCY==0
		 && fc_lock.load() == 0){
			int a = 0;                
			if (fc_lock.compare_exchange_strong(a, tid+1)){
				 // This thread is now the combiner
				 doFlatCombining(tid);
				 fc_lock.store(0);
			}
		}
		rounds++;
	}

	return;

}

int32_t FCDualQueue::remove(int tid){

	// Initialize request
	ThreadNode* thread_node = &this->thread_requests[tid];
	assert(!thread_node->is_val());
	thread_node->set(true,true,0);
	int rounds =0;

	// wait for combining
	while(thread_node->is_val()){

		// Try to combine
		if (rounds%COMBINING_LIST_CHECK_FREQUENCY==0
		 && fc_lock.load() == 0){
			int a = 0;                
			if (fc_lock.compare_exchange_strong(a, tid+1)){
				 // This thread is now the combiner
				 doFlatCombining(tid);
				 fc_lock.store(0);
			}
		}
		rounds++;
	}

	return thread_node->item();
}

