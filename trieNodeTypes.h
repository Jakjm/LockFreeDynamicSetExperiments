#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "BoundedMinReg/minreg.h"
#include "common.h"
#include "debra.h"
#include "swCopy.h"
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

class UpdateNode{
    public:
        int64_t key;
        const TYPE type; 
        std::atomic<STATUS> status; 
        std::atomic<UpdateNode *> latestNext;
        volatile char padding1[64 - sizeof(int64_t) - sizeof(TYPE) - sizeof(std::atomic<STATUS>) - sizeof(std::atomic<UpdateNode*>)];
        std::atomic<uintptr_t> succ; //Successor for the UALL
        std::atomic<UpdateNode*> backlink; //Backlink for the UALL
        volatile char padding2[64 - sizeof(uintptr_t) - sizeof(UpdateNode*)];
        std::atomic<uintptr_t> rSucc; //Successor for the RUALL
        std::atomic<UpdateNode*> rBacklink; //Backlink for the RUALL.
        volatile char padding3[64 - sizeof(uintptr_t) - sizeof(UpdateNode*)];
        UpdateNode(int64_t k, TYPE t) : key(k), type(t), status(INACTIVE), latestNext(nullptr), succ(0), backlink(0), rSucc(0), rBacklink(0){
        
        }
        UpdateNode(TYPE t): UpdateNode(-1, t){
        
        }

};

//Customized version of PermaRemList that is specifically used for the RUALL of Jeremy's Trie.
//DescNode could either be an insert descriptor node or a notify descriptor node.
struct InsertDesc{
    UpdateNode *newNode; //Either a pointer to an update node to be inserted or a pointer to a predecessor node.
    UpdateNode *next;
    InsertDesc(): newNode(nullptr), next(nullptr) {

    }
    volatile char padding[64 - 3 * sizeof(std::atomic<uintptr_t>)];
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
    volatile char padding[64 - 4*sizeof(int64_t)];

    //Pointer to the next node in the notify list....
    NotifyNode *next;
    
    NotifyNode(UpdateNode *upNode, InsNode *upNodeMax, int64_t threshold) : 
        key(upNode->key), updateNode(upNode), updateNodeMax(upNodeMax), notifyThreshold(threshold), next(nullptr){
    }
};

struct UpdateNodeAtomicCopy : private SW_AtomicCopy{
    UpdateNodeAtomicCopy(uintptr_t u) : SW_AtomicCopy(u){

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
            return ((InsertDesc*)next)->next;
        }
        return (UpdateNode*)next;
    }
};

class PredecessorNode:  public BaseType{
    public:
    const int64_t key;
    std::atomic<uintptr_t> succ;
    std::atomic<PredecessorNode*> backlink;
    volatile char padding1[64 - 3*sizeof(UpdateNode*)];
    std::atomic<NotifyNode*> notifyListHead;
    volatile char padding2[64 - (sizeof(NotifyNode*))];
    UpdateNodeAtomicCopy notifyThreshold;
    PredecessorNode(int64_t k, UpdateNode *ruallHead): key(k), notifyListHead(nullptr), notifyThreshold((uintptr_t)ruallHead) {
    
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
        volatile char padding [64 - sizeof(int8_t)];
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
