#pragma once
#include "common.h"
#include <atomic>
#include <stack>
using std::stack;

template <typename Type, int numBags>
struct ThreadData{
    stack<Type*> limboBag[numBags];
    std::atomic<uint64_t> announcement;
    int checkNext;
    int currentBag;
    volatile char padding[256 - (2*sizeof(int) + sizeof(std::atomic<uint64_t>) + 3 * sizeof(stack<Type*>))];
    ThreadData(): checkNext(0), currentBag(0){

    }
};


uint64_t QUIESCENT_BIT_FLAG = 0x1;
uint64_t ANNOUNCEMENT_FLAG = 0xFFFFFFFFFFFFFFFE;


template <typename Type, int numBags>
class Debra{
    std::atomic<uint64_t> epoch;
    volatile char padding[64 - sizeof(std::atomic<uint64_t>)];
    ThreadData<Type, numBags> data[MAX_THREADS]; //Data used for each thread
    public: 
    Debra(){
        //Initialize current bag for each process
    }
    ~Debra(){
        //Empty all limbo bags and reclaim everything.
        for(int i = 0;i < MAX_THREADS;++i){
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
        data[threadID].currentBag = newBag;
        while(!data[threadID].limboBag[newBag].empty()){
            Type *ptr = data[threadID].limboBag[newBag].top();
            data[threadID].limboBag[newBag].pop();
            delete ptr;
        }
    }
    void startOp(){
        uint64_t e = epoch;
        if((data[threadID].announcement >> 1) != e){
            data[threadID].checkNext = (threadID + 1) % MAX_THREADS;
            rotateAndReclaim();
        }
        int otherPID = data[threadID].checkNext;
        uint64_t other = data[otherPID].announcement;
        int otherQuiescent = (other & QUIESCENT_BIT_FLAG);
        uint64_t otherAnnouncement = other >> 1;
        if(otherQuiescent || otherAnnouncement == e){
            data[threadID].checkNext = (data[threadID].checkNext + 1) % MAX_THREADS;
            if(data[threadID].checkNext == threadID){
                data[threadID].checkNext = (threadID + 1) % MAX_THREADS;
                epoch.compare_exchange_strong(e, e+1);
            }
        }
        data[threadID].announcement = (e << 1); //Turn off quiescent bit.
    }

    void endOp(){
        data[threadID].announcement = data[threadID].announcement + 1; //Turn on quiescent bit.
    }
};


