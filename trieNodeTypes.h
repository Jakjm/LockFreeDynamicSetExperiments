#include <atomic>
#include <string>
#include "BoundedMinReg/minreg.h"
#include "FomitchevRuppert/ListNode.h"
#include "setbench/common/recordmgr/record_manager.h"
#include "common.h"
#pragma once
using std::string;


enum TYPE {INS, DEL};
enum STATUS {INACTIVE, ACTIVE, STALE};

class PredecessorNode;
class InsNode;
class DelNode;
class NotifyNode;


typedef record_manager<reclaimer_debra<int64_t>, allocator_new<int64_t>, pool_none<int64_t>, DelNode, InsNode, PredecessorNode, NotifyNode> NodeRecordManager;

class UpdateNode : public ListNode, public RU_ALL_Node{
    public:
        int64_t key;
        std::atomic<STATUS> status; 
        std::atomic<UpdateNode *> latestNext;
        std::atomic<bool> stop;
        std::atomic<DelNode *> target;
        const TYPE type; 

        UpdateNode(int64_t x, TYPE t) : key(x), status(INACTIVE), latestNext(nullptr), stop(false), target(nullptr), type(t), 
            ListNode(), RU_ALL_Node(){
        
        }
        virtual void retire(NodeRecordManager &recordMgr) = 0;
        virtual ~UpdateNode(){}

};






class InsNode : public UpdateNode{
    public:
        InsNode(int64_t key): UpdateNode(key, INS){
            
        }
        void retire(NodeRecordManager &recordMgr){
            recordMgr.retire(threadID, this);
        }
        ~InsNode(){

        }
    
};

#warning updateNodeMax is currently created as a copy node?, which is then retired when the notify node is retired. TODO is this even valid?
class NotifyNode{
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
    void retire(NodeRecordManager &recordMgr){
        //notList.removeKey(this);
        //int reclaim = updateNode->retireCounter.fetch_add(-1);
        //if(reclaim == 1)updateNode->retire(recordMgr);
        recordMgr.retire( threadID, this);
    }
};

//UpdateNodes that are meant to represent the values of infinity and zero for the notifyThreshold
InsNode INFINITY_THRES(INT64_MAX);
InsNode ZERO_THRES(0);

class PredecessorNode : public ListNode{
    public:
    const int64_t key;
    std::atomic<UpdateNode*> notifyThreshold;
    std::atomic<NotifyNode*> notifyListHead;
    
    PredecessorNode(int64_t k) : key(k), notifyThreshold(&INFINITY_THRES), notifyListHead(nullptr), ListNode() {
    
    }
    void retire(NodeRecordManager &recordMgr){
        NotifyNode *curNode = notifyListHead;
        while(curNode){
            NotifyNode *next = curNode->next;
            curNode->retire(recordMgr); //Retire notify node...
            curNode = next;
        }
        recordMgr.retire(threadID, this); //Retire the predecessor node.
    }
    ~PredecessorNode(){
        
    }
};




class DelNode : public UpdateNode{
    public:
        std::atomic<int> upper0Boundary;
        
        #warning 64 as maximum lower 1 boundary.
        MinReg64 lower1Boundary; 
        const int64_t delPred;
        int64_t delPred2;
        PredecessorNode * const delPredNode;
        std::atomic<int64_t> dNodeCount;
        DelNode(int64_t key, int b, UpdateNode *latest, int64_t delP, PredecessorNode *delPredN) : 
            UpdateNode(key, DEL), upper0Boundary(0), 
            lower1Boundary(b+1), delPred(delP), delPred2(-1), delPredNode(delPredN), dNodeCount(2){

                latestNext = latest;
        }
        DelNode(int64_t key, int b, UpdateNode *latest) : DelNode(key, b, latest, -1, nullptr){
            
        }
        //Should be used to retire a delete node.
        void retire(NodeRecordManager &recordMgr){
            delPredNode->retire(recordMgr);
            recordMgr.retire(threadID, this);
        }
        ~DelNode(){
        }
};


//Pred nodes can be reclaimed as soon as they're removed from P_ALL....
//Update nodes can be reclaimed upon removal from latest list...

class TrieNode{
    public:
        std::atomic<DelNode*>dNodePtr;
        TrieNode(){
        }
        TrieNode(const TrieNode &node){
            DelNode *ptr = node.dNodePtr;
            dNodePtr = ptr;
        }
};

class LatestList{
    public:
    std::atomic<UpdateNode*> head;
};