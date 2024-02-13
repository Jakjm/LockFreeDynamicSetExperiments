#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "BoundedMinReg/minreg.h"
#include "common.h"
#include "debra.h"
#include "swCopy2.h"
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

//Used to implement the UALL and the RUALL based on swCopy.
struct InsertDesc: public BaseType{
    UpdateNode *newNode; //Either a pointer to an update node to be inserted or a pointer to a predecessor node.
    UpdateNode *next;
    volatile char padding[64 - 3 * sizeof(std::atomic<uintptr_t>)];
    InsertDesc(): newNode(nullptr), next(nullptr) {

    }
};


//NotifyThreshold field of a PredecessorNode based on a single writer atomic copy object.
//Used for the swCopy-based implementation of the RUALL.
struct AtomicCopyNotifyThreshold : private SW_AtomicCopy{
    AtomicCopyNotifyThreshold(uintptr_t u) : SW_AtomicCopy(u){

    }
    void swcopy(std::atomic<uintptr_t> *src){
        SW_AtomicCopy::swcopy(src);
    }
    void write(uintptr_t newVal){
        SW_AtomicCopy::write(newVal);
    }
    UpdateNode *read(){
        uintptr_t succ = SW_AtomicCopy::read();
        uintptr_t next = succ & NEXT_MASK;
        uint64_t state = succ & STATUS_MASK; 

        if(state == InsFlag){
            UpdateNode *n = ((InsertDesc*)next)->next;
            return n;
        }
        return (UpdateNode*)next;
    }
};


//NotifyThreshold field of a PredecessorNode based on a writable CAS object.
//Used for the NotifyDescNode implementation of the RUALL.
struct NotifDescNotifyThreshold{
	std::atomic<uintptr_t> updateNodePtr;
	NotifDescNotifyThreshold(uintptr_t initVal) : updateNodePtr(initVal){
	
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


template <typename NotifyThresholdType = AtomicCopyNotifyThreshold>
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

template <class NotifyThreshold>
class RU_ALL_TYPE;

template <typename NotifyThresholdType = AtomicCopyNotifyThreshold>
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
    PredecessorNode(int64_t k, UpdateNode *ruallHead): succ(0), backlink(nullptr), notifyListHead(nullptr), notifyThreshold((uintptr_t)ruallHead), key(k){
    
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


Debra<BaseType, 5> trieDebra;

//record_manager<reclaimer_debra<int>, allocator_new<int>, pool_none<int>, InsNode, DelNode, PredecessorNode> trieRecordManager(NUM_THREADS);



template <typename NotifyThresholdType = AtomicCopyNotifyThreshold>
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

template <typename NotifyThresholdType = AtomicCopyNotifyThreshold>
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

class LatestList{
    public:
        std::atomic<UpdateNode*> head;
        //volatile char padding[64-sizeof(UpdateNode*)];
        LatestList(): head((UpdateNode*)nullptr){

        }
};


