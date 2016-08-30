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



#ifndef SIMPLE_RING_HPP
#define SIMPLE_RING_HPP

template <class T>
class SimpleRing{

	T* ring=NULL;
	int32_t sz;

	int32_t tail; // always points to next insert index
public:
	void resize(int32_t size){
		sz = size;
		if(ring!=NULL){free(ring);}
		ring = (T*)malloc(sizeof(T)*sz);
		ring[0]=0;
		tail = 0;
	}
	SimpleRing(){
		ring = NULL;
		//resize(10); // causes memory leak somehow
	}

	~SimpleRing(){
	}

	T inline back(){
		if(tail==0){return NULL;}
		return ring[tail-1];
	}

	void push_back(T val){
		ring[tail] = val;
		tail++;
		if(tail >= sz){
			assert(false);
		}
	}

	void pop_back(){
		if(tail==0){}
		else{tail--;}
	}

	void clear(){
		tail = 0;
	}

	uint32_t size(){
		return tail;
	}

};


#endif
