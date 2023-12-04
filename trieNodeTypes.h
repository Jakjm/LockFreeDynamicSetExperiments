#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "BoundedMinReg/minreg.h"
#include "LinkedLists/ListNode.h"
#include "common.h"
#include "debra.h"
#pragma once
using std::string;

enum TYPE {INS, DEL};
enum STATUS {INACTIVE, ACTIVE, STALE};

class PredecessorNode;
class InsNode;
class DelNode;
class NotifyNode;
class BaseType{
    public:
    virtual ~BaseType(){
        
    }
};


Debra<BaseType, 4> trieDebra;

//record_manager<reclaimer_debra<int>, allocator_new<int>, pool_none<int>, InsNode, DelNode, PredecessorNode> trieRecordManager(NUM_THREADS);

class UpdateNode : public ListNode, public RU_ALL_Node{
    public:
        int64_t key;
        const TYPE type; 
        std::atomic<STATUS> status; 
        std::atomic<UpdateNode *> latestNext;

        UpdateNode(int64_t k, TYPE t) : ListNode(), RU_ALL_Node(),  key(k), type(t), status(INACTIVE), latestNext(nullptr){
        
        }
        UpdateNode(TYPE t) : ListNode(), RU_ALL_Node(),  key(-1), type(t), status(INACTIVE), latestNext(nullptr){
        
        }

};

class InsNode : public UpdateNode, public BaseType{
    public:
        std::atomic<DelNode *> target;
        InsNode(int64_t key): UpdateNode(key, INS),  target(nullptr){
            
        }
        InsNode(): UpdateNode(INS),  target(nullptr){
            
        }
        ~InsNode(){

        }
    
};

class NotifyNode {
    public:
    const int64_t key;
    UpdateNode * const updateNode;
    InsNode * const updateNodeMax;
    const int64_t notifyThreshold;
    

    //Pointer to the next node in the notify list....
    NotifyNode *next;
    
    NotifyNode(UpdateNode *upNode, InsNode *upNodeMax, int64_t threshold) : 
        key(upNode->key), updateNode(upNode), updateNodeMax(upNodeMax), notifyThreshold(threshold), next(nullptr){
    }
};

//UpdateNodes that are meant to represent the values of infinity and zero for the notifyThreshold
InsNode INFINITY_THRES(INT64_MAX);
InsNode ZERO_THRES(0);

class PredecessorNode :  public ListNode, public BaseType{
    public:
    const int64_t key;
    std::atomic<UpdateNode*> notifyThreshold;
    std::atomic<NotifyNode*> notifyListHead;
    volatile char padding[64 - (sizeof(ListNode) + 3 * sizeof(int64_t) + sizeof(BaseType))];
    PredecessorNode(int64_t k) :  ListNode(), key(k), notifyThreshold(&INFINITY_THRES), notifyListHead(nullptr) {
    
    }
    ~PredecessorNode(){
        NotifyNode *curNode = notifyListHead;
        while(curNode){
            NotifyNode *next = curNode->next;
            delete curNode; //Retire notify node...
            curNode = next;
        }
    }
};

class DelNode : public UpdateNode, public BaseType{
    public:
        std::atomic<int> upper0Boundary;
        MinReg64 lower1Boundary; //A 65-bounded min register, which is sufficient for trees whose height is 63 or smaller.
        PredecessorNode *delPredNode;
        int64_t delPred;
        std::atomic<int64_t> delPred2;
        std::atomic<bool> stop;
        std::atomic<int8_t> dNodeCount;
        

        DelNode(int64_t key, int trieHeight) : 
            UpdateNode(key, DEL), upper0Boundary(0), 
            lower1Boundary(trieHeight+1), delPredNode(nullptr), delPred(-1), delPred2(-1), stop(false), dNodeCount(2){
        
        }
        DelNode(int trieHeight) :  DelNode(0, trieHeight) {
        
        }
        ~DelNode(){
        }
};


//Pred nodes can be reclaimed as soon as they're removed from P_ALL....
//Update nodes can be reclaimed upon removal from latest list...

class TrieNode{
    public:
        std::atomic<DelNode*>dNodePtr;
        TrieNode(): dNodePtr((DelNode*)nullptr){

        }
        TrieNode(const TrieNode &copy) : dNodePtr((DelNode*)copy.dNodePtr){

        }
};

class LatestList{
    public:
        std::atomic<UpdateNode*> head;
        LatestList(): head((UpdateNode*)nullptr){

        }
};