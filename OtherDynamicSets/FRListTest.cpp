#include "FRSkipList.h"
#include "../common.h"
#include "FRList.h"
#include "../DynamicSet.h"
#include <thread>
void testList(int64_t tid, LinkedListSet *list){
	threadID = tid;
	for(int b = 0; b < 8000;++b){
		for(int i = 0; i < 32;++i){
			list->insert(rng(31));
		}
		for(int i = 0; i < 32;++i){
			list->remove(rng(31));
		}
	}
	list->printList();
	for(int b = 0; b < 8000;++b){
		for(int i = 0; i < 32;++i){
			list->insert(rng(31));
		}
		for(int i = 0; i < 32;++i){
			list->remove(rng(31));
		}
	}
	list->printList();
}
int main(){
	threadID = 0;
	//SkipListSet<10> list;
	//list.insert(1);
	LinkedListSet list;
	std::thread *threads[4];
	for(int tid = 0; tid < 4;++tid){
		threads[tid] = new std::thread(testList, tid, &list);
	}

	for(int tid=0; tid< 4;++tid){
		threads[tid]->join();
		delete threads[tid];
	}
	return 0;
}
