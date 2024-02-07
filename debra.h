#pragma once
#include "common.h"
#include <atomic>
#include <stack>
//#include <iostream>
using std::stack;

template <typename Type, int numBags>
struct ThreadData{
    stack<Type*> limboBag[numBags];
    int checkNext;
    int currentBag;
    volatile long numRotations;
    volatile long numReclaimed;
    volatile long opStarted;
    volatile long opEnded;
    volatile char padding[512 - (2*sizeof(int) + 4*sizeof(std::atomic<uint64_t>) + numBags * sizeof(stack<Type*>))];
    std::atomic<uint64_t> announcement;
    volatile char padding2[64 - sizeof(uint64_t)];
    ThreadData(): checkNext(0), currentBag(0), numRotations(0), numReclaimed(0), opStarted(0), opEnded(0), announcement(0) {

    }
};


uint64_t QUIESCENT_BIT_FLAG = 0x1;
uint64_t ANNOUNCEMENT_FLAG = 0xFFFFFFFFFFFFFFFE;
int progressAmount = 1;
template <typename Type, int numBags>
class Debra{
    std::atomic<uint64_t> epoch;
    volatile char padding[64 - sizeof(std::atomic<uint64_t>)];
    ThreadData<Type, numBags> data[MAX_THREADS]; //Data used for each thread
    int numActiveThreads; //Number of threads participating in execution.
    public: 
    Debra(): epoch(0), numActiveThreads(MAX_THREADS){
        //Initialize current bag for each process
    }
	void setActiveThreads(int numThreads){
		numActiveThreads = numThreads;
	}
    ~Debra(){
        //Empty all limbo bags and reclaim everything.
        for(int i = 0;i < numActiveThreads;++i){
		//cout << i << " " <<  data[i].numRotations << " " << data[i].numReclaimed << "\n";
		//cout << "Started " << data[i].opStarted << " " << data[i].opEnded << "\n";
            for(int b = 0;b < numBags;++b){
                while(!data[i].limboBag[b].empty()){
                    Type *ptr = data[i].limboBag[b].top();
                    data[i].limboBag[b].pop();
                    delete ptr;
                }
            }
        }
    }
    void retire(Type* ptr){
        data[threadID].limboBag[data[threadID].currentBag].push(ptr);
    }

    void rotateAndReclaim(){
        int newBag = (data[threadID].currentBag + 1) % numBags;
	++data[threadID].numRotations;
        data[threadID].currentBag = newBag;
        while(!data[threadID].limboBag[newBag].empty()){
	    ++data[threadID].numReclaimed;
            Type *ptr = data[threadID].limboBag[newBag].top();
            data[threadID].limboBag[newBag].pop();
            delete ptr;
        }
    }
    void startOp(){
        uint64_t e = epoch;
        if((data[threadID].announcement >> 1) != e){
            data[threadID].checkNext = (threadID + 1) % numActiveThreads;
            rotateAndReclaim();
        }
	for(int i = 0;i < progressAmount;++i){
		int otherPID = data[threadID].checkNext;
		uint64_t other = data[otherPID].announcement;
		int otherQuiescent = (other & QUIESCENT_BIT_FLAG);
		uint64_t otherAnnouncement = other >> 1;
		if(otherQuiescent || otherAnnouncement == e){
		    data[threadID].checkNext = (data[threadID].checkNext + 1) % MAX_THREADS;
		    if(data[threadID].checkNext == threadID){
			data[threadID].checkNext = (threadID + 1) % numActiveThreads;
			epoch.compare_exchange_strong(e, e+1);
			break;
		    }
		}
		else{
			break;
		}
	}
        data[threadID].announcement = (e << 1); //Turn off quiescent bit.
	data[threadID].opStarted++;
    }

    void endOp(){
 	data[threadID].opEnded++;
    	data[threadID].announcement = data[threadID].announcement + 1; //Turn on quiescent bit.
    }
};


