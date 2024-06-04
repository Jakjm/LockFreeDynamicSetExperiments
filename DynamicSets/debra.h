#pragma once
#include "../common.h"
#include <atomic>
#include <stack>
//#include <iostream>
using std::stack;

template <typename Type, int numBags>
struct ThreadData{
    stack<Type*> limboBag[numBags];
    int checkNext;
    int currentBag;
    int64_t currentEpoch;
    volatile long numRotations;
    volatile long numReclaimed;
    volatile long opStarted;
    volatile long opEnded;
    volatile char padding[1024 - (2*sizeof(int) + 5*sizeof(std::atomic<uint64_t>) + numBags * sizeof(stack<Type*>))];
    std::atomic<int64_t> announcement;
    volatile char padding2[64 - sizeof(int64_t)];
    ThreadData(): checkNext(-1), currentBag(0), currentEpoch(-1), numRotations(0), numReclaimed(0), opStarted(0), opEnded(0), announcement(0) {

    }
};

uint64_t QUIESCENT_BIT_FLAG = 0x1;
uint64_t ANNOUNCEMENT_FLAG = 0xFFFFFFFFFFFFFFFE;
int progressAmount = 1;
template <typename Type, int numBags>
class Debra{
    std::atomic<int64_t> epoch;
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
    void reclaimLater(Type* ptr){
        ThreadData<Type, numBags> &threadData = data[threadID];
        threadData.limboBag[threadData.currentBag].push(ptr);
    }

    void rotateAndReclaim(){
        ThreadData<Type, numBags> &threadData = data[threadID];
        int newBag = (threadData.currentBag + 1) % numBags;
	    ++threadData.numRotations;
        threadData.currentBag = newBag;
        while(!threadData.limboBag[newBag].empty()){
	    ++threadData.numReclaimed;
            Type *ptr = threadData.limboBag[newBag].top();
            threadData.limboBag[newBag].pop();
            delete ptr;
        }
    }
    void startOp(){
        ThreadData<Type, numBags> &threadData = data[threadID];
        int64_t e = epoch;
        if(threadData.currentEpoch != e){
            rotateAndReclaim();
            threadData.checkNext = (threadID + 1) % numActiveThreads;
            threadData.currentEpoch = e;
        }
        for(int i = 0;i < progressAmount;++i){
            int otherPID = threadData.checkNext;
            int64_t other = data[otherPID].announcement;

            int otherQuiescent = (other & QUIESCENT_BIT_FLAG);
            int64_t otherAnnouncement = other >> 1;
            if(otherQuiescent || otherAnnouncement == e){
                threadData.checkNext = (threadData.checkNext + 1) % MAX_THREADS;
                if(threadData.checkNext == threadID){
                    //data[threadID].checkNext = (threadID + 1) % numActiveThreads;
                    epoch.compare_exchange_strong(e, e+1);
                    break;
                }
            }
            else{
                break;
            }
        }
        threadData.announcement = (e << 1); //Turn off quiescent bit.
	    threadData.opStarted++;
    }

    void endOp(){
        ThreadData<Type, numBags> &threadData = data[threadID];
 	    threadData.opEnded++;
    	threadData.announcement = data[threadID].currentEpoch + 1; //Turn on quiescent bit.
    }
};


