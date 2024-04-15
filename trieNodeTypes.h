#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "BoundedMinReg/minreg.h"
#include "common.h"
#include "debra.h"
#pragma once
using std::string;

enum TYPE {INS, DEL};
enum STATUS {INACTIVE, ACTIVE, STALE};
template <typename>
class InsNode;
template <typename>
class DelNode;
template <typename>
class NotifyNode;
class BaseType{
    public:
    virtual ~BaseType(){
        
    }
};



class UpdateNode{
    public:
        std::atomic<uintptr_t> succ; //Successor for the UALL
        std::atomic<UpdateNode*> backlink; //Backlink for the UALL
        volatile char padding1[64 - sizeof(uintptr_t) - sizeof(UpdateNode*)];
        std::atomic<uintptr_t> rSucc; //Successor for the RUALL
        std::atomic<UpdateNode*> rBacklink; //Backlink for the RUALL.
        volatile char padding2[64 - sizeof(uintptr_t) - sizeof(UpdateNode*)];
        std::atomic<UpdateNode *> latestNext;
        int64_t key;
        const TYPE type; 
        std::atomic<STATUS> status; 
        UpdateNode(int64_t k, TYPE t) :  succ(0), backlink(0), rSucc(0), rBacklink(0), latestNext(nullptr), key(k), type(t), status(INACTIVE){
        
        }
        UpdateNode(TYPE t): UpdateNode(-1, t){
        
        }

};

//Definition of the Insert Descriptor Node object.
struct InsertDescNode{
    std::atomic<UpdateNode*> newNode;
    std::atomic<UpdateNode*> next;
    std::atomic<uint64_t> seqNum; //Sequence number.
    volatile char padding[64 - 3 * sizeof(std::atomic<uintptr_t>)];
    InsertDescNode():  newNode(nullptr), next(nullptr), seqNum(0){

    }
};

//This bit of the threshold will be set if an atomic copy is in progress.
const uintptr_t COPY_BIT = 0x1;  
const uintptr_t ANTI_COPY_BIT_MASK = 0xFFFFFFFFFFFFFFFE;

template <class NotifyThreshold>
class RU_ALL_TYPE;


//NotifyThreshold atomic copy implementation from CAS.
struct AtomicCopyNotifyThreshold{
    std::atomic<uintptr_t> threshold;
    InsertDescNode *descNodes;
    volatile char padding [64 - 2*sizeof(uintptr_t)];
    AtomicCopyNotifyThreshold(UpdateNode *initialValue) : threshold((uintptr_t)initialValue){

    }
    UpdateNode *copyNext(UpdateNode *prev, InsertDescNode *descs){
        descNodes = descs;
        uintptr_t result = (((uintptr_t)prev) + COPY_BIT);
        threshold = result;

        uintptr_t r = prev->rSucc; 
        uintptr_t next = (r & NEXT_MASK);
        uintptr_t state = (r & STATUS_MASK); //Read the rNext and rState fields of prev.
        while(state == InsFlag){  //If rState = InsDesc, rNext points to an InsertDesc with the next UpdateNode in the list.
            uint64_t seq = (next & SEQ_MASK) >> 12;
            uint64_t proc = (next & PROC_MASK) >> 4;
            InsertDescNode *desc = &descs[proc];
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
        if(v & COPY_BIT){ //If an atomic copy is in progress
            UpdateNode *prev = (UpdateNode*)(v - COPY_BIT);
            uintptr_t r = prev->rSucc; //Read the successor 
            uintptr_t next = (r & NEXT_MASK);
            uintptr_t state = (r & STATUS_MASK); //Read the rNext and rState fields of prev.
            
            while(state == InsFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                InsertDescNode *desc = &descNodes[proc];
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
struct NotifDescNotifyThreshold{
	std::atomic<uintptr_t> updateNodePtr;
	NotifDescNotifyThreshold(UpdateNode *ruallHead) : updateNodePtr((uintptr_t)ruallHead){
	
	}
	uintptr_t CAS(uintptr_t prev, uintptr_t newVal){
		uintptr_t oldVal = prev;
		updateNodePtr.compare_exchange_strong(oldVal, newVal);
		return oldVal;
	}
	void write(uintptr_t newVal){	
		updateNodePtr = newVal;
	}
	
	UpdateNode *read(){
		uintptr_t ptr = updateNodePtr;
		return (UpdateNode*)ptr;
	} 
};


template <typename NotifyThresholdType>
class NotifyNode {
    public:
    int64_t key;
    UpdateNode *updateNode;
    InsNode<NotifyThresholdType> *updateNodeMax;
    int64_t notifyThreshold;
    NotifyNode<NotifyThresholdType> *next; //Pointer to the next node in the notify list....
    volatile char padding[64 - 5*sizeof(int64_t)];
    NotifyNode(){
    }
};

template <typename NotifyThresholdType>
class PredecessorNode:  public BaseType{
    public:
    std::atomic<uintptr_t> succ;
    std::atomic<PredecessorNode*> backlink;
    volatile char padding1[64 - 2*sizeof(UpdateNode*)];
    std::atomic<NotifyNode<NotifyThresholdType>*> notifyListHead;
    volatile char padding2[64 - (sizeof(NotifyNode<NotifyThresholdType>*))];
    NotifyThresholdType notifyThreshold;
    const int64_t key;
    volatile char padding3[64 - 2*sizeof(int64_t)];
    PredecessorNode(int64_t k, UpdateNode *ruallHead): succ(0), backlink(nullptr), notifyListHead(nullptr), notifyThreshold(ruallHead), key(k){
    
    }
    ~PredecessorNode(){
        NotifyNode<NotifyThresholdType> *curNode = notifyListHead;
        while(curNode){
            NotifyNode<NotifyThresholdType> *next = curNode->next;
            delete curNode; //Retire notify node...
            curNode = next;
        }
    }
};

Debra<BaseType, 7> trieDebra;

//record_manager<reclaimer_debra<int>, allocator_new<int>, pool_none<int>, InsNode, DelNode, PredecessorNode> trieRecordManager(NUM_THREADS);

template <typename NotifyThresholdType>
class InsNode : public UpdateNode, public BaseType{
    public:
        std::atomic<DelNode<NotifyThresholdType> *> target;
        char padding[192 - sizeof(DelNode<NotifyThresholdType>*) - sizeof(UpdateNode) - sizeof(BaseType)];
        InsNode(int64_t key): UpdateNode(key, INS),  target(nullptr){
            
        }
        InsNode(): UpdateNode(INS),  target(nullptr){
            
        }
        ~InsNode(){

        }
    
};

template <typename NotifyThresholdType>
class DelNode : public UpdateNode, public BaseType{
    public:
        PredecessorNode<NotifyThresholdType> *delPredNode;
        int64_t delPred;
        MinReg lower1Boundary; //A 65-bounded min register, which is sufficient for tries whose height is at most 63.
        volatile char padding1[192-sizeof(UpdateNode)-sizeof(delPredNode)-sizeof(int64_t)-sizeof(MinReg)];
        std::atomic<int64_t> delPred2;
        std::atomic<int> upper0Boundary;
        std::atomic<int8_t> dNodeCount;
        std::atomic<bool> stop;
        volatile char padding2[64 - sizeof(int64_t) - sizeof(int) - sizeof(int8_t) - sizeof(bool)- sizeof(BaseType)];

        DelNode(int64_t key, int trieHeight) : 
            UpdateNode(key, DEL), delPredNode(nullptr), delPred(-1), lower1Boundary(trieHeight+1),  
              delPred2(-1), upper0Boundary(0), dNodeCount(2), stop(false){
        
        }
        DelNode(int trieHeight) :  DelNode(0, trieHeight) {
        
        }
        ~DelNode(){
        }
};


//Pred nodes can be reclaimed as soon as they're removed from P_ALL....
//Update nodes can be reclaimed upon removal from latest list...
template <typename NotifyThresholdType>
class TrieNode{
    public:
        std::atomic<DelNode<NotifyThresholdType>*> dNodePtr;
        volatile char padding [64 - sizeof(UpdateNode*)];
        TrieNode(): dNodePtr((DelNode<NotifyThresholdType>*)nullptr){

        }
        TrieNode(const TrieNode &copy) : dNodePtr((DelNode<NotifyThresholdType>*)copy.dNodePtr){

        }
};

