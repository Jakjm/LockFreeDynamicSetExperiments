#include "common.h"
#include <atomic>
#include <stack>
using std::stack;


std::atomic<uint64_t> epoch;
std::atomic<uint64_t> announcement[NUM_THREADS];
uint64_t QUIESCENT_BIT_FLAG = 0x1;
uint64_t ANNOUNCEMENT_FLAG = 0xFFFFFFFFFFFFFFFE;

template <typename Type, int numBags>
class Debra{
    stack<Type*> limboBag[NUM_THREADS][numBags]; //Not using block bags initially....
    int currentBag[NUM_THREADS];
    int checkNext[NUM_THREADS];
    public: 
    Debra(){
        //Initialize current bag for each process
        for(int i = 0;i < NUM_THREADS;++i){
            currentBag[i] = 0;
            checkNext[i] = (threadID + 1) % NUM_THREADS;
        }
    }
    ~Debra(){
        //Empty all limbo bags and reclaim everything.
        for(int i = 0;i < NUM_THREADS;++i){
            for(int b = 0;b < numBags;++b){
                while(!limboBag[i][b].empty()){
                    Type *ptr = limboBag[i][b].top();
                    limboBag[i][b].pop();
                    delete ptr;
                }
            }
        }
    }
    void retire(Type* ptr){
        limboBag[threadID][currentBag[threadID]].push(ptr);
    }

    void rotateAndReclaim(){
        int newBag = (currentBag[threadID] + 1) % numBags;
        currentBag[threadID] = newBag;
        while(!limboBag[threadID][newBag].empty()){
            Type *ptr = limboBag[threadID][newBag].top();
            limboBag[threadID][newBag].pop();
            delete ptr;
        }
    }
    void startOp(){
        uint64_t e = epoch;
        if((announcement[threadID] >> 1) != e){
            checkNext[threadID] = (threadID + 1) % NUM_THREADS;
            rotateAndReclaim();
        }

        uint64_t other = announcement[checkNext[threadID]];
        int otherQuiescent = (other & QUIESCENT_BIT_FLAG);
        uint64_t otherAnnouncement = other >> 1;
        if(otherQuiescent || otherAnnouncement == e){
            checkNext[threadID] = (checkNext[threadID] + 1) % NUM_THREADS;
            if(checkNext[threadID] == threadID){
                checkNext[threadID] = (threadID + 1) % NUM_THREADS;
                epoch.compare_exchange_strong(e, e+1);
            }
        }
        announcement[threadID] = (e << 1); //Turn off quiescent bit.
    }

    void endOp(){
        announcement[threadID] = announcement[threadID] + 1; //Turn on quiescent bit.
    }
};


