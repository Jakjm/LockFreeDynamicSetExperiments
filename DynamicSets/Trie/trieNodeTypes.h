#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "BoundedMinReg/minreg.h"
#include "../../common.h"
#include "../debra.h"
#include <iomanip>
#pragma once
using std::string;

enum TYPE {INS, DEL};
enum STATUS {INACTIVE, ACTIVE, STALE};
class InsNode;
class DelNode;
class NotifyNode;


#ifdef COUNT_CONTENTION
    struct UALLCounter{
        //Number of nodes traversed through UALL while inserting/removing
        uint64_t nodesTraversed = 0;
        //Number of times this thread backtracked when seeing a marked node.
        uint64_t numBacktracks = 0;
        //Number of inserts by other threads that this thread helped.
        uint64_t numInsertsHelped = 0; 
        //Number of UpdateNodes marked by other processes that this thread helped remove.
        uint64_t numMarksHelped = 0;
        //Number of removes by other threads that this thread helped.
        uint64_t numRemovesHelped = 0;
        //Number of failed insert/remove CASes
        uint64_t numFailedCASes = 0;
        //Number of desc nodes traversed while reading next.
        uint64_t numReadNexts = 0;
        uint64_t descsTraversedWhileReadingNext = 0;
        //volatile char padding[64 - 7 * sizeof(uint64_t)];

    };

    struct UALLContentionInfo{
        uint64_t minTrav, maxTrav, totalTrav;
        uint64_t minBacks, maxBacks, totalBacks;
        uint64_t minInsH, maxInsH, totalInsH;
        uint64_t minRemH, maxRemH, totalRemH;
        uint64_t minMrkH, maxMrkH, totalMrkH;
        uint64_t minFailCAS, maxFailCAS, totalFailCAS;
        uint64_t minReadNext, maxReadNext, totalReadNext;
        uint64_t minDescRN, maxDescRN, totalDescRN;
        int numProcs;

        UALLContentionInfo(UALLCounter *counters, int numThreads){
            numProcs = numThreads;
            UALLCounter &ctr = counters[0];
            minTrav = maxTrav = totalTrav = ctr.nodesTraversed;
            minBacks = maxBacks = totalBacks = ctr.numBacktracks;
            minInsH = maxInsH = totalInsH = ctr.numInsertsHelped;
            minRemH = maxRemH = totalRemH = ctr.numRemovesHelped;
            minMrkH = maxMrkH = totalMrkH = ctr.numMarksHelped;
            minFailCAS = maxFailCAS = totalFailCAS = ctr.numFailedCASes;
            minReadNext = maxReadNext = totalReadNext = ctr.numReadNexts;
            minDescRN = maxDescRN = totalDescRN = ctr.descsTraversedWhileReadingNext;

            for(int t = 1;t < numThreads;++t){
                ctr = counters[t];

                totalTrav += ctr.nodesTraversed;
                if(ctr.nodesTraversed < minTrav)minTrav = ctr.nodesTraversed;
                else if(ctr.nodesTraversed > maxTrav)maxTrav = ctr.nodesTraversed;

                totalBacks += ctr.numBacktracks;
                if(ctr.numBacktracks < minBacks)minBacks = ctr.numBacktracks;
                else if(ctr.numBacktracks > maxBacks)maxBacks = ctr.numBacktracks;

                totalInsH += ctr.numInsertsHelped;
                if(ctr.numInsertsHelped < minInsH)minInsH = ctr.numInsertsHelped;
                else if(ctr.numInsertsHelped > maxInsH)maxInsH = ctr.numInsertsHelped;

                totalRemH += ctr.numRemovesHelped;
                if(ctr.numRemovesHelped < minRemH)minRemH = ctr.numRemovesHelped;
                else if(ctr.numRemovesHelped > maxRemH)maxRemH = ctr.numRemovesHelped;

                totalMrkH += ctr.numMarksHelped;
                if(ctr.numMarksHelped < minMrkH)minMrkH = ctr.numMarksHelped;
                else if(ctr.numMarksHelped > maxMrkH)maxMrkH = ctr.numMarksHelped;

                totalFailCAS += ctr.numFailedCASes;
                if(ctr.numFailedCASes < minFailCAS)minFailCAS = ctr.numFailedCASes;
                else if(ctr.numFailedCASes > maxFailCAS)maxFailCAS = ctr.numFailedCASes;

                totalReadNext += ctr.numReadNexts;
                if(ctr.numReadNexts < minReadNext)minReadNext = ctr.numReadNexts;
                else if(ctr.numReadNexts > maxReadNext)maxReadNext = ctr.numReadNexts;

                totalDescRN += ctr.descsTraversedWhileReadingNext;
                if(ctr.descsTraversedWhileReadingNext < minDescRN)minDescRN = ctr.descsTraversedWhileReadingNext;
                else if(ctr.descsTraversedWhileReadingNext > maxDescRN)maxDescRN = ctr.descsTraversedWhileReadingNext;
            }
        }

        void printInfo(){
            cout << "UALL Contention Information:" << std::endl;
            cout << "Average nodes encountered while inserting/removing: " << std::setprecision(8) << (double)totalTrav / numProcs;
            cout << " Max: " << maxTrav <<  " Min: " << minTrav << std::endl;

            cout << "Average number of inserts helped: " << (double)totalInsH / numProcs;
            cout << " Max: " << maxInsH <<  " Min: " << minInsH << std::endl;

            cout << "Average number of removes helped: " << (double)totalRemH / numProcs;
            cout << " Max: " << maxRemH <<  " Min: " << minRemH << std::endl;

            cout << "Average number of marks helped: " << (double)totalMrkH / numProcs;
            cout << " Max: " << maxMrkH <<  " Min: " << minMrkH << std::endl;

            cout << "Average number of backtracks: " << (double)totalBacks / numProcs;
            cout << " Max: " << maxBacks <<  " Min: " << minBacks << std::endl;

            cout << "Average number of failed insert/remove CASes: " << (double)totalFailCAS / numProcs;
            cout << " Max: " << maxFailCAS<<  " Min: " << minFailCAS<< std::endl;

            cout << "Average number of readNexts: " << (double)totalReadNext/ numProcs;
            cout << " Max: " << maxReadNext<<  " Min: " << minReadNext<< std::endl;

            cout << "Average number of insert descs read while reading next: " << (double)totalDescRN / numProcs;
            cout << " Max: " << maxDescRN <<  " Min: " << minDescRN << std::endl;
            // cout << "Nodes traversed while inserting/removing: " << nodesTraversed;
            // cout << "(" << std::setprecision(5) << ((double)nodesTraversed / numOps) << " per op)" << std::endl;
            // cout << "Number of failed insert/removal CASes: " << numFailedCASes;
            // cout << "(" << std::setprecision(5) << ((double)numFailedCASes / numOps) << " per op)" << std::endl;

            // cout << "Inserts helped: " << numInsertsHelped; 
            // cout << "(" << std::setprecision(5) << ((double)numInsertsHelped / numOps) << " per op)" << std::endl;
            // cout << "Removes helped: " << numRemovesHelped;
            // cout << "(" << std::setprecision(5) << ((double)numRemovesHelped / numOps) << " per op)"<< std::endl;
            // cout << "Marks helped: " << numMarksHelped;
            // cout << "(" << std::setprecision(5) << ((double)numMarksHelped / numOps) << " per op)"<< std::endl;
            // cout << "Number of backtracks: " << numBacktracks;
            // cout << "(" << std::setprecision(5) << ((double)numBacktracks / numOps) << " per op)" <<  std::endl;

            // cout << "Number of read nexts: " << numReadNexts << std::endl;
            // cout << "Number of insert descs accessed while reading next: " << descsTraversedWhileReadingNext << std::endl << std::endl;
        }
    };

    UALLCounter uallCounter[MAX_THREADS];


    struct RUALLCounter{
        //Number of nodes traversed through RUALL while inserting/removing
        uint64_t nodesTraversed = 0;
        //Number of times this thread backtracked when seeing a marked node.
        uint64_t numBacktracks = 0;
        //Number of inserts by other threads that this thread helped.
        uint64_t numInsertsHelped = 0; 
        //Number of UpdateNodes marked by other processes that this thread helped remove.
        uint64_t numMarksHelped = 0;
        //Number of removes by other threads that this thread helped.
        uint64_t numRemovesHelped = 0;
        //Number of failed insert/remove CASes
        uint64_t numFailedCASes = 0;
        //Number of insert desc nodes traversed while copying.
        uint64_t descsTraversedWhileCopying = 0;
        uint64_t numAtomicCopies = 0;
        uint64_t numCopiesHelped = 0;
        volatile char padding[128 - 9 * sizeof(uint64_t)];
        void printInfo(uint64_t numOps){
            cout << "RUALL Contention Information:" << std::endl;
            cout << "Nodes traversed while inserting/removing: " << nodesTraversed;
            cout << "(" << std::setprecision(5) << ((double)nodesTraversed / numOps) << " per op)" << std::endl;
            cout << "Number of failed insert/removal CASes: " << numFailedCASes;
            cout << "(" << std::setprecision(5) << ((double)numFailedCASes / numOps) << " per op)" << std::endl;

            cout << "Inserts helped: " << numInsertsHelped; 
            cout << "(" << std::setprecision(5) << ((double)numInsertsHelped / numOps) << " per op)"<< std::endl;
            cout << "Removes helped: " << numRemovesHelped;
            cout << "(" << std::setprecision(5) << ((double)numRemovesHelped / numOps) << " per op)"<< std::endl;
            cout << "Marks helped: " << numMarksHelped;
            cout << "(" << std::setprecision(5) << ((double)numMarksHelped / numOps) << " per op)"<< std::endl;
            cout << "Number of backtracks: " << numBacktracks;
            cout << "(" << std::setprecision(5) << ((double)numBacktracks / numOps) << " per op)" <<  std::endl;


            cout << "Num atomic copies: " << numAtomicCopies << " num copies helped: " << numCopiesHelped << std::endl;
            cout << "Num insert descs accessed while copying: " << descsTraversedWhileCopying << std::endl << std::endl;
        }
    };
    RUALLCounter ruallCounter[MAX_THREADS];

    struct PALLCounter{
        //Number of failed CASes performed while inserting predecessor nodes....
        uint64_t numFailedInsertCASes = 0;
        uint64_t numFailedRemoveCASes = 0;
        uint64_t nodesTraversed = 0;

        uint64_t numBacktracks = 0;
        //Number of inserts by other threads that this thread helped.
        uint64_t numMarksHelped = 0;
        //Number of removes by other threads that this thread helped.
        uint64_t numRemovesHelped = 0;
        //Number of failed insert/remove CASes

        uint64_t numNotifyListCASFailures = 0;
        uint64_t numNotifyNodesEncountered = 0;
    };
    PALLCounter pallCounter[MAX_THREADS];

#endif



class UpdateNode : public ReclaimableBase{
    public:
        const TYPE type;
        int64_t key;
        std::atomic<STATUS> status; 
        std::atomic<UpdateNode *> latestNext;
        volatile char padding1[64 - 4*sizeof(int64_t) - sizeof(ReclaimableBase)];
        std::atomic<uintptr_t> succ; //Successor for the UALL
        UpdateNode *backlink;
        //std::atomic<UpdateNode*> backlink; //Backlink for the UALL
        volatile char padding2 [64 - 2*sizeof(uintptr_t)];
        std::atomic<uintptr_t> rSucc; //Successor for the RUALL
        UpdateNode *rBacklink;
        //std::atomic<UpdateNode*> rBacklink; //Backlink for the RUALL.
        //volatile char padding2[64 - 2*sizeof(uintptr_t)];
        volatile char padding3 [64 - 2*sizeof(uintptr_t)];
        //volatile char padding3[64 - sizeof(uintptr_t)];

        UpdateNode(int64_t k, TYPE t) :  type(t), key(k), status(INACTIVE), latestNext(nullptr),  succ(0), backlink(0), rSucc(0), rBacklink(0){
        
        }
        UpdateNode(TYPE t): UpdateNode(-1, t){
        
        }

};

//Definition of the Insert Descriptor Node object.
struct alignas(128) InsertDescNode{
    UpdateNode *newNode;
    //std::atomic<UpdateNode*> newNode;
    UpdateNode *next;
    //std::atomic<UpdateNode*> next;
    std::atomic<uint64_t> seqNum; //Sequence number.
    volatile char padding[64 - 3 * sizeof(std::atomic<uintptr_t>)];
    InsertDescNode():  newNode(nullptr), next(nullptr), seqNum(0){

    }
};

//This bit of the threshold will be set if an atomic copy is in progress.
const uintptr_t COPY_BIT = 0x1;  
const uintptr_t ANTI_COPY_BIT_MASK = 0xFFFFFFFFFFFFFFFE;

class RUALL;
InsertDescNode insert_descs[MAX_THREADS]; //descriptors used for the UALL and RUALL

//NotifyThreshold atomic copy implementation from CAS.
struct alignas(64) AtomicCopyNotifyThreshold{
    std::atomic<uintptr_t> threshold;
    volatile char padding [64 - 2*sizeof(uintptr_t)];
    AtomicCopyNotifyThreshold(UpdateNode *initialValue) : threshold((uintptr_t)initialValue){

    }
    UpdateNode *copyNext(UpdateNode *prev){
        uintptr_t result = (((uintptr_t)prev) + COPY_BIT);
        threshold = result;

        uintptr_t r = prev->rSucc; 
        uintptr_t next = (r & NEXT_MASK);
        uintptr_t state = (r & STATUS_MASK); //Read the rNext and rState fields of prev.
        
        #ifdef COUNT_CONTENTION
            RUALLCounter &counter = ruallCounter[threadID];
            ++counter.numAtomicCopies;
        #endif
        while(state == InsFlag){  //If rState = InsDesc, rNext points to an InsertDesc with the next UpdateNode in the list.
            #ifdef COUNT_CONTENTION
                ++counter.descsTraversedWhileCopying;
            #endif
            uint64_t seq = (next & SEQ_MASK) >> 12;
            uint64_t proc = (next & PROC_MASK) >> 4;
            InsertDescNode *desc = &insert_descs[proc];
            next = (uintptr_t)(UpdateNode*)desc->next;
            if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
            
            r = prev->rSucc; //Read node->rSucc again.
            next = r & NEXT_MASK;
            state = r & STATUS_MASK;
        }

        threshold.compare_exchange_strong(result, next);
        if(result == (((uintptr_t)prev) + COPY_BIT)){
            return (UpdateNode*)next;
        }
        else{
            return (UpdateNode*)result;
        }
    }
    // void write(uintptr_t v){
    //     threshold = v;
    // }
    UpdateNode *read(){
        uintptr_t v = threshold;

        #ifdef COUNT_CONTENTION
            RUALLCounter &counter = ruallCounter[threadID];
        #endif
        if(v & COPY_BIT){ //If an atomic copy is in progress
            #ifdef COUNT_CONTENTION
                ++counter.numCopiesHelped;
            #endif
            UpdateNode *prev = (UpdateNode*)(v - COPY_BIT);
            uintptr_t r = prev->rSucc; //Read the successor 
            uintptr_t next = (r & NEXT_MASK);
            uintptr_t state = (r & STATUS_MASK); //Read the rNext and rState fields of prev.
            
            while(state == InsFlag){
                #ifdef COUNT_CONTENTION
                    ++counter.descsTraversedWhileCopying;
                #endif
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                InsertDescNode *desc = &insert_descs[proc];
                next = (uintptr_t)(UpdateNode*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
                
                r = prev->rSucc; //Read node->succ again.
                next = r & NEXT_MASK;
                state = r & STATUS_MASK;
            }
            
            uintptr_t result = v;
            threshold.compare_exchange_strong(result, next); 
            if(result == v)return (UpdateNode*)next;
            else return (UpdateNode*)(result & ANTI_COPY_BIT_MASK);
        }
        else return (UpdateNode*)v;
    }
};


//NotifyThreshold field of a PredecessorNode based on a writable CAS object.
//Used for the NotifyDescNode implementation of the RUALL.
// struct NotifDescNotifyThreshold{
// 	std::atomic<uintptr_t> updateNodePtr;
// 	NotifDescNotifyThreshold(UpdateNode *ruallHead) : updateNodePtr((uintptr_t)ruallHead){
	
// 	}
// 	uintptr_t CAS(uintptr_t prev, uintptr_t newVal){
// 		uintptr_t oldVal = prev;
// 		updateNodePtr.compare_exchange_strong(oldVal, newVal);
// 		return oldVal;
// 	}
// 	void write(uintptr_t newVal){	
// 		updateNodePtr = newVal;
// 	}
	
// 	UpdateNode *read(){
// 		uintptr_t ptr = updateNodePtr;
// 		return (UpdateNode*)ptr;
// 	} 
// };



class alignas(64)NotifyNode: public ReclaimableBase {
    public:
    int64_t key;
    UpdateNode *updateNode;
    InsNode *updateNodeMax;
    int64_t notifyThreshold;
    NotifyNode *next; //Pointer to the next node in the notify list....
    volatile char padding[64 - 5*sizeof(int64_t)];
    NotifyNode(){
    }
    ~NotifyNode(){
        
    }
};


class alignas(128)PredecessorNode:  public ReclaimableBase{
    public:
    const int64_t key;
    std::atomic<uintptr_t> succ;
    PredecessorNode* backlink;
    volatile char padding1[64 - 3*sizeof(PredecessorNode*)];
    std::atomic<NotifyNode*> notifyListHead;
    volatile char padding2[64 - (sizeof(NotifyNode*))];
    AtomicCopyNotifyThreshold ruallPosition;
    PredecessorNode(int64_t k, UpdateNode *ruallHead): key(k), succ(0), backlink(nullptr), notifyListHead(nullptr), ruallPosition(ruallHead){
    
    }
    ~PredecessorNode(){
        NotifyNode *curNode = notifyListHead;
        while(curNode){
            NotifyNode *next = curNode->next;
            delete curNode;
            curNode = next;
        }
        
    }
};



//record_manager<reclaimer_debra<int>, allocator_new<int>, pool_none<int>, InsNode, DelNode, PredecessorNode> trieRecordManager(NUM_THREADS);


class alignas(128) InsNode : public UpdateNode{
    public:
        std::atomic<DelNode *> target;
        std::atomic<int64_t> target_key;
        char padding[256 - 2*sizeof(uintptr_t) - sizeof(UpdateNode)];
        InsNode(int64_t key): UpdateNode(key, INS),  target(nullptr), target_key(-1){
            
        }
        InsNode(): UpdateNode(INS),  target(nullptr), target_key(-1){
            
        }
        ~InsNode(){
             
        }
    
};


class alignas(128) DelNode : public UpdateNode{
    public:
        PredecessorNode *delPredNode;
        int64_t delPred;
        int64_t delPred2;
        //std::atomic<int64_t> delPred2;
        std::atomic<int> upper0Boundary;
        std::atomic<bool> stop;
        MinReg lower1Boundary; //A 65-bounded min register, which is sufficient for tries whose height is at most 63.
        std::atomic<int> dNodeCount;
        DelNode(int trieHeight) : UpdateNode(-1, DEL), delPredNode(nullptr), delPred(-1), delPred2(-1),  
               upper0Boundary(0),  stop(false), lower1Boundary(trieHeight+1), dNodeCount(2){
        }
        ~DelNode(){
             
        }
};


//Pred nodes can be reclaimed as soon as they're removed from P_ALL....
//Update nodes can be reclaimed upon removal from latest list...
class TrieNode{
    public:
        std::atomic<DelNode*> dNodePtr;
        //volatile char padding [64 - sizeof(UpdateNode*)];
        TrieNode(): dNodePtr((DelNode*)nullptr){

        }
        TrieNode(const TrieNode &copy) : dNodePtr((DelNode*)copy.dNodePtr){

        }
};

