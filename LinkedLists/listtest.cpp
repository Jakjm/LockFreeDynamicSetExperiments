#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <random>
#include "../trieNodeTypes.h"
#include "P_ALL.h"
#include "RU_ALL_swCopy.h"
#include "RU_ALL_notifDesc.h"
using std::cout;

//Produces a random integer between 0 and range (inclusive).
inline int randomNum(int range){
    //A random number seed that is local to this thread.
	thread_local static std::random_device seed;
	//Random number generator using seed.
	thread_local static std::mt19937 rng(seed());
	//Ask for a distribution from this random number generator.
	std::uniform_int_distribution<int> dist(0,range);
	return dist(rng);
}

template <typename NotifThresholdType>
void randomTest(){
    threadID = 0;
    int arraySize = 8000;
    int testSize = 50000;
    InsNode<NotifThresholdType> *node[arraySize];
    RU_ALL_TYPE<NotifThresholdType> ruall;
    //Allobreak ate nodes...
    for(int i = 0;i < arraySize; ++i){
        node[i] = new InsNode<NotifThresholdType>(randomNum(i));
        ruall.insert(node[i]);
    }
    //Free nodes
    for(int i = 0;i < arraySize;++i){
        delete node[i];
    }
}

template <typename NotifThresholdType>
void basicTest(){
    threadID = 0;
    RU_ALL_TYPE<NotifThresholdType> ruall;

    UpdateNode *uNodeList[10];

    for(int i = 0; i < 10; ++i){
        uNodeList[i] = new InsNode(i);
        ruall.insert(uNodeList[i]);
    }
    for(int i = 9; i >= 0; --i){
        ruall.remove(uNodeList[i]);
    }




}


int main(){
    //randomTest();
    //basicTest<AtomicCopyNotifyThreshold>();
    randomTest<NotifDescNotifyThreshold>();
    
    return 0;
}
