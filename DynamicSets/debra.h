#pragma once
#include "../common.h"
#include <atomic>
#include <stack>
//#include <iostream>
using std::stack;

template <typename Type, int numBags>
struct alignas(64) ThreadData{
    stack<Type*> limboBag[numBags];
    int checkNext;
    int currentBag;
    int64_t currentEpoch;
    volatile char padding[512 - (2*sizeof(int) + sizeof(std::atomic<uint64_t>) + (numBags + 1) * sizeof(stack<Type*>))];
    std::atomic<int64_t> announcement;
    volatile char padding2[64 - sizeof(int64_t)];
    ThreadData(): checkNext(-1), currentBag(0), currentEpoch(-1), announcement(0) {

    }
    void cleanup(){
        for(int b = 0;b < numBags;++b){
            while(!limboBag[b].empty()){
                Type *ptr = limboBag[b].top();
                limboBag[b].pop();
                delete ptr;
            }
            stack<Type*>().swap(limboBag[b]);   //Force limbo bag memory to be reclaimed.
        }
    }
    ~ThreadData(){
        cleanup();
    }
};

uint64_t QUIESCENT_BIT_FLAG = 0x1;
uint64_t ANNOUNCEMENT_FLAG = 0xFFFFFFFFFFFFFFFE;
int progressAmount = 1;
template <typename Type, int numBags>
struct alignas(64) Debra{
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

    }
    void reclaimLater(Type* ptr){
        ThreadData<Type, numBags> &threadData = data[threadID];
        threadData.limboBag[threadData.currentBag].push(ptr);
    }
    void rotateAndReclaim(){
        ThreadData<Type, numBags> &threadData = data[threadID];
        int newBag = (threadData.currentBag + 1) % numBags;
        threadData.currentBag = newBag;
        while(!threadData.limboBag[newBag].empty()){
            //Put the record into the freelist
            Type *ptr = threadData.limboBag[newBag].top();
            threadData.limboBag[newBag].pop();
            delete ptr;
        }
    }
    //If the freelist is not empty, free an element.
    void startOp(){
        ThreadData<Type, numBags> &threadData = data[threadID];
        int64_t e = epoch;
        if(threadData.currentEpoch != e){
            rotateAndReclaim();
            threadData.checkNext = 1;
            threadData.currentEpoch = e;
        }
        for(int i = 0;i < progressAmount;++i){
            int otherPID = (threadID + threadData.checkNext) % numActiveThreads;
            int64_t other = data[otherPID].announcement;

            int otherQuiescent = (other & QUIESCENT_BIT_FLAG);
            int64_t otherAnnouncement = other >> 1;
            if(otherQuiescent || otherAnnouncement == e){
                ++threadData.checkNext;
                if(threadData.checkNext == numActiveThreads){
                    epoch.compare_exchange_strong(e, e+1);
                    break;
                }
            }
            else{
                break;
            }
        }
        threadData.announcement = (e << 1); //Turn off quiescent bit.
    }

    void endOp(){
        ThreadData<Type, numBags> &threadData = data[threadID];
    	threadData.announcement = data[threadID].currentEpoch + 1; //Turn on quiescent bit.
    }
};


