#include <cstdint>
#include <sstream>
#include <stdint.h>
#include <vector>
#include "BoundedMinReg/minreg.h"
#include "FomitchevRuppert/list.h"
#include "FR_Extension/list_extension.h"

//TODO ifdef
#include <deque>
#include <thread>
#include <string>
#include <ostream>
#include <set>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include "common.h"
#include "setbench/common/recordmgr/allocator_new.h"
#include "setbench/common/recordmgr/pool_none.h"
#include "setbench/common/recordmgr/reclaimer_debra.h"

//Jeremy's trie structure source code.
//TODO add comments everywhere
using std::vector;
using std::unordered_set;
using std::string;
enum TYPE {INS, DEL};
enum STATUS {INACTIVE, ACTIVE, STALE};

int compareKey(int64_t val1, int64_t val2){
    if(val1 > val2)return 1;
    else if(val1 < val2)return -1;
    else return 0;
}

class UpdateNode;
class PredecessorNode;
class InsNode;
class DelNode;
class NotifyNode;


typedef record_manager<reclaimer_debra<int64_t>, allocator_new<int64_t>, pool_none<int64_t>, DelNode, InsNode, PredecessorNode, NotifyNode> recordMgr_t;

class UpdateNode : ListNode<int64_t>{
    public:
        int64_t key;
        std::atomic<STATUS> status; 
        std::atomic<UpdateNode *> latestNext;
        std::atomic<bool> stop;
        
        std::atomic<DelNode *> target;

        //Used to count the number of places in which the UpdateNode is stored (or will be stored) in shared memory.
        //It should be safe to retire an update node when retireCounter is 0.
        std::atomic<int> retireCounter; 

        UpdateNode(int64_t x, UpdateNode *latest) : key(x), status(INACTIVE), latestNext(latest), stop(false), target(nullptr), retireCounter(0){
        
        }
        UpdateNode(int64_t x) : UpdateNode(x, nullptr) {

        }
        virtual void retire(recordMgr_t &recordMgr) = 0;
        virtual ~UpdateNode(){}
        virtual TYPE type() = 0; 

        //TODO used for debugging only.
        virtual string toString() = 0;
};
int compareUpdate(UpdateNode *u1, UpdateNode *u2){
    if(u1->key > u2->key)return 1;
    else if(u1->key < u2->key)return -1;
    else return 0;
}



class InsNode : public UpdateNode{
    public:
        InsNode(int64_t key): UpdateNode(key){

        }
        void retire(recordMgr_t &recordMgr){
            //insList.removeKey(this);
            recordMgr.retire(threadID(), this);
        }
        ~InsNode(){

        }
        TYPE type(){
            return INS;
        }
        string toString(){
            return std::to_string((uintptr_t)this) + ": INS Node, retCounter:" + std::to_string(retireCounter) + " State:" + std::to_string(status);
        }
    
};

#warning updateNodeMax is currently created as a copy node?, which is then retired when the notify node is retired. TODO is this even valid?
class NotifyNode{
    public:
    UpdateNode * const updateNode;
    InsNode * const updateNodeMax;
    const int64_t notifyThreshold;

    //Pointer to the next node in the notify list....
    NotifyNode *next;
    
    NotifyNode(UpdateNode *upNode, InsNode *upNodeMax, int64_t threshold) : 
        updateNode(upNode), updateNodeMax(upNodeMax), notifyThreshold(threshold){
    }
    void retire(recordMgr_t &recordMgr){
        //notList.removeKey(this);
        //int reclaim = updateNode->retireCounter.fetch_add(-1);
        //if(reclaim == 1)updateNode->retire(recordMgr);
        if(updateNodeMax)updateNodeMax->retire(recordMgr);
        recordMgr.retire( threadID(), this);
    }
};
int compareNotify(NotifyNode *n1, NotifyNode *n2){
    return n1 - n2;
}

typedef record_manager<reclaimer_debra<NotifyNode*>, allocator_new<NotifyNode*>, pool_none<NotifyNode*>, ListNode<NotifyNode*,false>> NotifyListRecordMgr_t;
class PredecessorNode{
    public:
    const int64_t key;
    std::atomic<bool> traversedTrie;

    NotifyNode *notifyListHead;
    
    PredecessorNode(int64_t k, NotifyListRecordMgr_t *listRecMgr) : key(k), traversedTrie(false) {
    
    }
    void retire(recordMgr_t &recordMgr){
        NotifyNode *curNode = notifyListHead;
        while(curNode){
            NotifyNode *next = curNode->next;
            curNode->retire(recordMgr); //Retire notify node...
            curNode = next;
        }
        recordMgr.retire(threadID(), this); //Retire the predecessor node.
    }
    ~PredecessorNode(){
        
    }
};
inline int __attribute__((always_inline)) comparePred(PredecessorNode *p1, PredecessorNode *p2){
    if(p1->key > p2->key)return 1;
    else if(p1->key < p2->key)return -1;
    else return 0;
}


class DelNode : public UpdateNode{
    public:
        std::atomic<int> upper0Boundary;
        
        #warning 64 as maximum lower 1 boundary.
        MinReg64 lower1Boundary; 
        const int64_t delPred;
        int64_t delPred2;
        PredecessorNode * const delPredNode;
        DelNode(int64_t key, int b, UpdateNode *latest, int64_t delP, PredecessorNode *delPredN)
        : UpdateNode(key, latest), upper0Boundary(0), 
            lower1Boundary(b+1), delPred(delP), delPred2(-1), delPredNode(delPredN){

        }
        DelNode(int64_t key, int b, UpdateNode *latest) : DelNode(key, b, latest, -1, nullptr){

        }
        //Should be used to retire a delete node.
        void retire(recordMgr_t &recordMgr){
            //delList.removeKey(this);
            if(delPredNode)delPredNode->retire(recordMgr);
            recordMgr.retire(threadID(), this);
        }
        ~DelNode(){
        }
        TYPE type() {
            return DEL;
        }
        string toString(){
            return std::to_string((uintptr_t)this) + ": DEL Node, retCounter:" + std::to_string(retireCounter) + " State:" + std::to_string(status);
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




//TODO ifdef debug
class Trie{
    private:
    const int b;
    const int64_t universeSize; //Equal to 2^b 
    vector<vector<TrieNode>> trieNodes;
    vector<LatestList> latest;
    recordMgr_t recordMgr;
    record_manager<reclaimer_debra<NotifyNode*>, allocator_new<NotifyNode*>, pool_none<NotifyNode*>, ListNode<NotifyNode*, false>> notifyManager;
    PredecessorNode predMin, predMax; 
    InsNode updateMin, updateMax;
    LinkedList<PredecessorNode*, comparePred> P_ALL;
    LinkedList<UpdateNode *, compareUpdate> U_ALL;
       
    public:
    Trie(int size) : b(size), universeSize(1 << b), latest(universeSize), recordMgr(NUM_THREADS), 
    notifyManager(NUM_THREADS), predMin(-1, &notifyManager), 
    predMax(universeSize + 1, &notifyManager), updateMin(-1), updateMax(universeSize + 1),
        P_ALL(&predMin, &predMax),
        U_ALL(&updateMin, &updateMax)
    {
        threadInit();
        auto guard = recordMgr.getGuard(threadID());
        auto notifyGuard = notifyManager.getGuard(threadID());
        
        //Initialize the binary trie nodes for each level of the trie.
        for(int i = 0; i <= b;++i){
            int64_t rowSize = (1 << i); //Row size = 2^i
            vector<TrieNode> trieNodeRow(rowSize);
            trieNodes.push_back(trieNodeRow);
        }

        vector<TrieNode> &baseRow = trieNodes[b];

        //Initialize the latest lists for each key in the universe.
        //Initialize row b of binary trie nodes.
        //Initially, each latest list's head = a DelNode with
        for(int64_t key = 0; key < universeSize;++key){
            DelNode *initialDelNode = recordMgr.allocate<DelNode>(threadID(), key, b, nullptr);
            //delList.insert(initialDelNode);
            initialDelNode->upper0Boundary = b; // The initial delNodes for the trie have upper0Boundary = b.
            latest[key].head = initialDelNode;
            initialDelNode->status = ACTIVE;
            baseRow[key].dNodePtr = initialDelNode;
            initialDelNode->retireCounter += 2;
        }

        //Initialize rows 1 through b-1, such that every trieNode that is a parent of two trieNodes
        //Has its dNodePtr set to the dNodePtr of its left child.
        for(int row = b-1; row >= 0; row--){
            int64_t rowSize = (1 << row);
            vector<TrieNode> &trieNodeRow = trieNodes[row];
            vector<TrieNode> &childRow = trieNodes[row + 1];
            for(int64_t i = 0; i < rowSize;++i){
                int64_t leftChild = 2 * i;
                DelNode *dNode = childRow[leftChild].dNodePtr;
                trieNodeRow[i].dNodePtr = dNode;
                dNode->retireCounter += 1;
            }
        }
    }
    ~Trie(){
        threadInit();
        recordMgr.startOp(threadID());
        notifyManager.startOp(threadID());
        verifyLists();
        
        //Attempt to retire all of the UpdateNodes stored in the latest lists
        //And all of the dNodePtrs.
        for(vector<TrieNode> &trieNodeRow : trieNodes){
            for(TrieNode &tNode : trieNodeRow){
                DelNode *dNode = tNode.dNodePtr;
                int reclaim = dNode->retireCounter.fetch_add(-1);
                if(reclaim == 1)dNode->retire(recordMgr);
            }
        }
        for(LatestList &list : latest){
            UpdateNode *uNode = list.head;
            UpdateNode *next = uNode->latestNext;
            if(next){
                int reclaim = next->retireCounter.fetch_add(-1);
                if(reclaim == 1){
                    next->retire(recordMgr);
                }
            }
            int reclaim = uNode->retireCounter.fetch_add(-1);
            if(reclaim == 1){
                uNode->retire(recordMgr);
            }
        }
        recordMgr.endOp(threadID());
        notifyManager.endOp(threadID());
        for(int i = 0;i < NUM_THREADS;++i){
            recordMgr.deinitThread(i);
            notifyManager.deinitThread(i);
        }
    }
    UpdateNode *findLatest(int64_t x){
        UpdateNode *l = latest[x].head;
        if(l->status == INACTIVE){
            UpdateNode *m = l->latestNext;
            if(m)return m;
        }
        return l;
    }
    bool firstActivated(UpdateNode *u){
        UpdateNode *l = latest[u->key].head;
        return (u == l) || (l->status == INACTIVE && u == l->latestNext);
    }
    bool search(int64_t x){
        assert(x >= 0 && x <= universeSize); //TODO remove this eventually.
        if (x < 0 || x > universeSize){
            return false; //x outside of range of trie.
        }
        threadInit();
        auto guard = recordMgr.getGuard(threadID());
        auto notifyGuard = notifyManager.getGuard(threadID());

        UpdateNode *l = findLatest(x);
        return l->type() == INS; //Return whether the root of the latest was an insert node.
    }
    

    void insertBinaryTrie(UpdateNode *v){
        //For each binary trie node t on the path from the parent of the leaf with v.key to the root, do 
        int64_t key = v->key;
        for(int depth = b-1;depth >= 0;--depth){
            key = key >> 1;
            TrieNode &t = trieNodes[depth][key];
            UpdateNode *dNodePtr = t.dNodePtr;
            UpdateNode *uNode = findLatest(dNodePtr->key);

            if (uNode->type() == DEL){
                DelNode *delNode = dynamic_cast<DelNode*>(uNode);
                int height = b - depth;
                if (height < delNode->lower1Boundary.minRead() && height <= delNode->upper0Boundary){
                    v->target = delNode;
                    if(firstActivated(v) == false)return;
                    delNode->lower1Boundary.minWrite(height);
                }
            }
        }
    }
    //                0
    //           0       1
    //         0   1    2  3
    //       0  1 2 3  4 5 6 7
    // If the index is odd, it is the right child. Subtract 1 to get the left child.
    // If the index is even, it is the left child. Add 1 to get the right child.
    static int64_t siblingIndex(int64_t index){
        int64_t mod2 = index & 1;
        if(mod2)return index - 1; 
        else return index + 1;    
    }
    void deleteBinaryTrie(DelNode *v){
        TrieNode *t = &trieNodes[b][v->key]; //Get leaf of the trie with v.key
        TrieNode *root = &trieNodes[0][0];
        
        int depth = b;
        int64_t key = v->key;
        while(t != root){
            int height = b - depth;
            TrieNode *sibling = &trieNodes[depth][siblingIndex(key)];
            if(interpretedBit(t, height) == 1 || interpretedBit(sibling, height))return;

            //t = t->parent.
            --depth;
            height = b-depth;
            key = key / 2;
            t = &trieNodes[depth][key];

            DelNode *d = t->dNodePtr;
            if(firstActivated(v) == false)return;
            if(v->stop || v->lower1Boundary.minRead() != b+1)return;
            
            DelNode *expected = d;
            v->retireCounter.fetch_add(1);
            t->dNodePtr.compare_exchange_strong(expected, v);
            if(expected != d){
                d = t->dNodePtr;
                if(firstActivated(v) == false)return;
                if(v->stop || v->lower1Boundary.minRead() != b+1)return;
                expected = d;
                t->dNodePtr.compare_exchange_strong(expected, v);
                if(expected != d){
                    //Retire v if v is no longer in shared memory (TODO maybe this is silly.)
                    int retire = v->retireCounter.fetch_add(-1);
                    if(retire == 1)v->retire(recordMgr);
                    assert(retire > 0);
                    return;
                }
                else{
                    //Retire d if it is no longer in shared memory.
                    int retire = d->retireCounter.fetch_add(-1);
                    if(retire == 1)d->retire(recordMgr);
                    assert(retire > 0);
                }
            }
            else{
                //Retire d if it is no longer in shared memory.
                int retire = d->retireCounter.fetch_add(-1);
                if(retire == 1)d->retire(recordMgr);
                assert(retire > 0);
            }
            TrieNode *left = &trieNodes[depth+1][key * 2], *right = &trieNodes[depth+1][key *2 +1];
            if(interpretedBit(left, height - 1) == 1 || interpretedBit(right, height- 1))return;
            v->upper0Boundary = height;
        }
    }

    //Traverse through the Update Announcement Linked List
    void traverseUALL(int64_t x, vector<InsNode*> &I, std::unordered_set<DelNode*> &D){
        UpdateNode *u, *v;
        
        LinkedList<UpdateNode *, compareUpdate>::Iterator it = U_ALL.begin();
        int64_t uk, vk;                                                                                                  

        //u = head of linked list.
        if(it == U_ALL.end()){
            return;
        }
        u = (*it).key;
        uk = u->key;
        ++it;
        
        while(it != U_ALL.end() && uk < x){
            v = (*it).key;
            vk = v->key;

            if((uk != vk && u->status == ACTIVE) || (uk == vk && v->status == INACTIVE)){
                if(u->type() == INS)I.push_back((InsNode*)u);
                else D.insert((DelNode*)u);
            }
            u = v;
            uk = vk;
            ++it;
        }
    }
    char interpretedBit(TrieNode *t, int height){
        UpdateNode *dNode = t->dNodePtr;
        UpdateNode *uNode = findLatest(dNode->key);
        if(uNode->type() == INS)return 1;
        else{
            DelNode *d = dynamic_cast<DelNode*>(uNode); //uNode must have type = DEL
            if (height >= d->lower1Boundary.minRead()) return 1;
            else if(height <= d->upper0Boundary)return 0;
            else return 1;
        }
    }

    void helpActivate(UpdateNode *uNode){
        if (uNode->status == INACTIVE){

            if(U_ALL.insert(uNode)){
                int result = uNode->retireCounter.fetch_add(1);
                assert(result > 0);
            }

            //TODO fetch+add based on who makes the process active instead of based on insertion?
            //However there may be no such process...
            STATUS expectedStatus = INACTIVE;
            uNode->status.compare_exchange_strong(expectedStatus, ACTIVE);

            UpdateNode *latestNext = uNode->latestNext;
            if(latestNext){
                latestNext->status = STALE;
                
                bool unlinked = uNode->latestNext.compare_exchange_strong(latestNext, nullptr); //Replaces uNode->latestNext = nullptr;
                if(unlinked){ 
                    //If this is the thread that unlinks latestNext, attempt to retire latest next.
                    int retire = latestNext->retireCounter.fetch_add(-1);
                    if(retire == 1)latestNext->retire(recordMgr);
                    assert(retire > 0);
                }
            }
            if (uNode->status == STALE){
                if(U_ALL.removeKey(uNode)){
                    int retire = uNode->retireCounter.fetch_add(-1);
                    if(retire == 1)uNode->retire(recordMgr);
                    assert(retire > 0);
                }
            }
        }
    }
    void insert(int64_t x){
        assert(x >= 0 && x <= universeSize); //TODO remove this eventually.
        if (x < 0 || x > universeSize){
            return; //x outside of range of trie.
        }
        threadInit();
        auto guard = recordMgr.getGuard(threadID());
        auto notifyGuard = notifyManager.getGuard(threadID());
        UpdateNode *dNode = findLatest(x), *expected;
        if (dNode->type() == INS)return; //x already in S, nothing to do!
        //dNode has type DEL and its child, if it has one, is of type INS

        //InsNode *v = new InsNode(x);
        InsNode *v = recordMgr.allocate<InsNode, int64_t>(threadID(), x);
        insList.insert(v);
        v->latestNext = dNode;

        UpdateNode *latestNext = dNode->latestNext;
        if(latestNext){
            latestNext->status = STALE;
            //dNode->latestNext = nullptr
            bool unlinked = dNode->latestNext.compare_exchange_strong(latestNext, nullptr); 
            if(unlinked){
                //If this is the thread that unlinked latestNext from the latest list, 
                //Attempt to retire latestNext.
                int retire = latestNext->retireCounter.fetch_add(-1);
                if(retire == 1) latestNext->retire(recordMgr);
                assert(retire > 0);
            }
        }
        expected = dNode;
        v->retireCounter = 2; //Initially set reclaim counter to 2.
        latest[x].head.compare_exchange_strong(expected, v);
        if (expected != dNode){
            helpActivate(expected);
            //No other thread has seen v, so v can be reclaimed.
            v->retire(recordMgr);
            return;
        }

        U_ALL.insert(v);

        STATUS expectedStatus = INACTIVE;
        v->status.compare_exchange_strong( expectedStatus, ACTIVE);
        dNode->status = STALE;
        //v->latestNext = nullptr
        bool reclaim = v->latestNext.compare_exchange_strong(dNode,nullptr); 
        if(reclaim){
            int retire = dNode->retireCounter.fetch_add(-1);
            if(retire == 1)dNode->retire(recordMgr);
            assert(retire > 0);
        }
        
        vector<InsNode*> I;
        std::unordered_set<DelNode*> D;
        traverseUALL(INT64_MAX, I, D);

        //Notify predecessor operations that this insertion is in progress...
        //Start an iterator from the first pred node with key that is x+1 or greater.
        for(LinkedList<PredecessorNode*, comparePred>::Iterator predIt = P_ALL.begin();predIt != P_ALL.end();++predIt){
            ListNode<PredecessorNode*> &lNode = *predIt;
            PredecessorNode *p = lNode.key;
            if(p->key <= x)continue;
            if(v->status == STALE)break;

            //updateNodeMax = a node with the largest key in I that is less than p.key
            #warning usually updateNodeMax is the actual node in I. However, to make memory reclamation easier, I copy the node
            int64_t maxKey = -1;
            for(InsNode* insNode : I){
               if(insNode->key < p->key){
                   if(maxKey < insNode->key)maxKey = insNode->key;
               }
            }
            InsNode *updateNodeMax;
            if(maxKey != -1){
                updateNodeMax = recordMgr.allocate<InsNode>(threadID(), maxKey); 
                insList.insert(updateNodeMax);
            }
            else updateNodeMax = nullptr;

            v->retireCounter.fetch_add(1);
            NotifyNode *nNode = recordMgr.allocate<NotifyNode>(threadID(), v, updateNodeMax, p->traversedTrie.load());
            notList.insert(nNode);
            p->notifyList.insert(nNode);
        }
        insertBinaryTrie(v);

        //for each node uNode in U-ALL with uNode.key = x before and including v do
            //uNode.status = STALE
        for(LinkedList<UpdateNode*, compareUpdate>::Iterator it = U_ALL.begin();it != U_ALL.end(); ++it){
            ListNode<UpdateNode*> &lNode = *it;
            UpdateNode *uNode = lNode.key;
            if(uNode->key < x)continue;
            if(uNode->key > x)break;
            uNode->status = STALE;
            if(uNode == v) break;
        }

        //Remove v from U_ALL
        if(U_ALL.removeKey(v)){
            int retire = v->retireCounter.fetch_add(-1);
            if(retire == 1)v->retire(recordMgr);
            assert(retire > 0);
        }
    }
    void remove(int64_t x){
        assert(x >= 0 && x <= universeSize); //TODO remove this eventually.
        if (x < 0 || x > universeSize){
            return; //x outside of range of trie.
        }
        threadInit();
        auto guard = recordMgr.getGuard(threadID());
        auto notifyGuard = notifyManager.getGuard(threadID());
        UpdateNode *iNode = findLatest(x), *expected;
        if(iNode->type() == DEL)return; //x is not in S, nothing to do!
        //iNode has type INS. If it has a child, its child has type DEL.

        //PredecessorNode *pNode = new PredecessorNode(x);
        PredecessorNode *pNode = recordMgr.allocate<PredecessorNode>(threadID(),x,&notifyManager);
        P_ALL.insert(pNode);
        int64_t delPred = predHelper(pNode);

        //Initialize update node for this delete operation.
        DelNode *v = recordMgr.allocate<DelNode>(threadID(), x, b, iNode, delPred,pNode);
        //delList.insert(v);
        v->latestNext = iNode;
    
        
        UpdateNode *latestNext = iNode->latestNext;
        if(latestNext){
            latestNext->status = STALE;
            //iNode->latestNext = nullptr; 
            bool unlink = iNode->latestNext.compare_exchange_strong(latestNext, nullptr);
            if(unlink){  //If this operation unlinked the insNode from the latest list...
                int retire = latestNext->retireCounter.fetch_add(-1);
                if(retire == 1)latestNext->retire(recordMgr);
                assert(retire > 0);
            }   
        }
        expected = iNode;
        v->retireCounter = 2; //Initially set reclaim counter to 2.
        latest[x].head.compare_exchange_strong(expected, v);

        if(expected != iNode){
            //There was a different node, expected, instead of iNode at the head of the latest list.
            //We will help activate it.
            helpActivate(expected);

            //Remove pNode from P_ALL.
            P_ALL.removeKey(pNode);
            v->retire(recordMgr); //Retire v and the pNode. 
            return;
        }

        U_ALL.insert(v);
        STATUS expectedStatus = INACTIVE;
        v->status.compare_exchange_strong(expectedStatus, ACTIVE);
        iNode->status = STALE;

        UpdateNode *target = iNode->target;
        if(target)target->stop = true;
        //v->latestNext = nullptr; 
        bool unlink = v->latestNext.compare_exchange_strong(iNode, nullptr);
        if(unlink){  //If this CAS unlinked iNode from the latest list, decrement its retire counter and retire it if necessary.
            int retire = iNode->retireCounter.fetch_add(-1);
            if(retire == 1)iNode->retire(recordMgr);
            assert(retire > 0);
        }
        PredecessorNode *pNode2 = recordMgr.allocate<PredecessorNode>(threadID(), x, &notifyManager);
        P_ALL.insert(pNode2);
        int64_t delPred2 = predHelper(pNode2);
        v->delPred2 = delPred2;

        //Notify predecessor operations with key >= x
        for(LinkedList<PredecessorNode*, comparePred>::Iterator predIt = P_ALL.begin(); predIt != P_ALL.end(); ++predIt){
            ListNode<PredecessorNode*> &lNode = *predIt;
            PredecessorNode *p = lNode.key;
            if(p->key <= x){
                continue;
            }
            v->retireCounter.fetch_add(1);
            NotifyNode *nNode = recordMgr.allocate<NotifyNode>(threadID(), v, nullptr, p->traversedTrie.load());
            //notList.insert(nNode);
            p->notifyList.insert(nNode);
        }
        deleteBinaryTrie(v);

        // Delete pNode and pNode2 from P-ALL.
        P_ALL.removeKey(pNode);
        P_ALL.removeKey(pNode2);

        pNode2->retire(recordMgr); //pNode2 is no longer in shared memory, so it can be retired.
        //pNode is still in shared memory but it will be retired when v is retired.
    
        //Set status for update nodes with key x for ops preceding and including this one to STALE
        InsNode dummyInsNode(x);
        for(LinkedList<UpdateNode*, compareUpdate>::Iterator it = U_ALL.begin(); it != U_ALL.end(); ++it){
            ListNode<UpdateNode*> &lNode = *it;
            UpdateNode *uNode = lNode.key;

            if(uNode->key < x)continue;
            if(uNode->key > x)break;
            uNode->status = STALE;
            if(uNode == v)break;
        }

        if(U_ALL.removeKey(v)){
            int retire = v->retireCounter.fetch_add(-1);
            if(retire == 1)v->retire(recordMgr);
            assert(retire > 0);
        }
    }

    //    0
    //   0 0
    // 0 0 0 0 

    //y = 3 
    //height = 2  
    int64_t predecessorBinaryTrie(int64_t y){
        if(b <= 1)return -1;

        //Get interpreted bit
        //TrieNode *t = &trieNodes[b][y];
        TrieNode *tSibling = &trieNodes[b][siblingIndex(y)], 
                            *tParent = &trieNodes[b-1][y >> 1];
        int depth = b;
        int height = b - depth;
        char i1 = interpretedBit(tParent, height + 1);
        char i2 = interpretedBit(tSibling, height);
        
        //While i1 = 0, i2 == 0, or t is the leftChild of tParent
        while(i1 == 0 || (y % 2 == 0) || i2 == 0){
            y = y >> 1;
            --depth;
            height = b - depth;
            if(depth == 0)return -1; //Interpreted bit of root node was 0 on previous iteration.

            //t = tParent;
            tSibling = &trieNodes[depth][siblingIndex(y)];
            tParent = &trieNodes[depth - 1][y >> 1];
            
            i1 = interpretedBit(tParent,height + 1);
            i2 = interpretedBit(tSibling, height);
        }

        //Go to left child of parent. Subtract 1 from y if it is odd. 
        y = y - (y & 1);
        //t = &trieNodes[depth][y];
    
        while(depth < b){
            //Right child is at 2*y + 1, left child is at 2 * y.
            int64_t rightIndex = y * 2 + 1, leftIndex = y * 2;
            ++depth;
            height = b - depth;
            TrieNode *right = &trieNodes[depth][rightIndex];
            char i = interpretedBit(right, height);
            if(i == 1){
                y = rightIndex;
                continue;
            }
            TrieNode *left = &trieNodes[depth][leftIndex];
            i = interpretedBit(left, height);
            if(i == 1){
                y = leftIndex;
                continue;
            }
            
            //Interpreted bits of left and right nodes are 0. No predecessor found.
            return -2;
        }
        return y;
    }
    int64_t predHelper(PredecessorNode *v){
        int64_t y = v->key;
        vector<InsNode*> I_0, I1_I2_I3;
        unordered_set<DelNode*> D_1, D2_min_D1, D3Prime_min_D1;
        vector<DelNode*> D_3;
        int64_t pred0;

        traverseUALL(y,I1_I2_I3, D_1);
        pred0 = predecessorBinaryTrie(y);

        v->traversedTrie = true;
        traverseUALL(y,I1_I2_I3, D2_min_D1);


        //This vector stores the union of d1 and d3.
        vector<DelNode*> D1_union_D3;
        for(DelNode *dNode : D_1)D1_union_D3.push_back(dNode);

        for(LinkedList<NotifyNode*, compareNotify,false>::Iterator it = v->notifyList.begin();it != v->notifyList.end();++it){
            NotifyNode *nNode = (*it).key;
            if(nNode->updateNode->type() == INS){
                I1_I2_I3.push_back((InsNode*)nNode->updateNode);
                if(nNode->updateNodeMax)I1_I2_I3.push_back(nNode->updateNodeMax);
            }
            else{
                if(nNode->traversedTrie){
                    D_3.push_back((DelNode*)nNode->updateNode); 
                    D1_union_D3.push_back((DelNode*)nNode->updateNode);
                }
                else{
                    D3Prime_min_D1.insert((DelNode*)nNode->updateNode);
                }
            }
        }
        if(pred0 == -2){
            if(1 == 1){
                return -1; //TODO remove this 
            }
            DelNode *d = nullptr; //TODO what if there is no such node?
            int64_t largestDelPred = -1;
            std::unordered_set<int64_t> d1d3Keys;
            std::unordered_set<DelNode*> D_0;
            for(DelNode *dNode : D1_union_D3){
                d1d3Keys.insert(dNode->key);
            }
            for(DelNode *dNode : D1_union_D3){
                if(d1d3Keys.count(dNode->delPred) == 0 && dNode->delPred > largestDelPred){
                    d = dNode;
                    largestDelPred = dNode->delPred;
                }
            }
            LinkedList<NotifyNode*, compareNotify, false> &dPredNotifyList = d->delPredNode->notifyList;
            for(LinkedList<NotifyNode*, compareNotify, false>::Iterator it = dPredNotifyList.begin(); it != dPredNotifyList.end();++it){
                NotifyNode *nNode = (*it).key;
                if(nNode->updateNode->type() == INS)I_0.push_back((InsNode*)nNode->updateNode);
                else D_0.insert((DelNode*)nNode->updateNode);
            }
            int64_t pred0prime = -1;
            int64_t pred0doublePrime = -1;
            //Let pred0prime be the largest key of an INS node in I0 
            //such that there is no non-stale DEL node in D0 with the same key
            for(InsNode *iNode : I_0){
                int64_t iNodeKey = iNode->key;
                if(iNodeKey > pred0prime){
                    //If there is a delNode with the same key that is not stale, do nothing.
                    //Otherwise, set pred0prime = iNodeKey
                    bool foundKey = false;
                    for(DelNode *dNode : D_0){
                        if(dNode->key == iNodeKey && dNode->status != STALE){
                            foundKey = true; 
                            break;
                        }
                    }
                    if(!foundKey){
                        iNodeKey = iNodeKey;
                    }
                }
            }
            unordered_set<int64_t> delPreds;
            unordered_set<int64_t> d0d1d3Keys(d1d3Keys);
            for(DelNode *dNode : D1_union_D3)delPreds.insert(dNode->delPred);
            for(DelNode *dNode : D_0){
                delPreds.insert(dNode->delPred2);
                d0d1d3Keys.insert(dNode->key);
            }

            for(int64_t pred : delPreds){
                if(d0d1d3Keys.count(pred) == 0 && pred0doublePrime < pred)pred0doublePrime = pred;
            }

            //pred0 set to the max of pred0prime and pred0doubleprime.
            if(pred0doublePrime > pred0prime) pred0 = pred0doublePrime;
            else pred0 = pred0prime;
        }
        int64_t pred1 = -1, pred2 = -1, pred3 =-1;

        //pred1 is the largest key in i1 union i2 union i3.
        for(InsNode *iNode : I1_I2_I3){
            if(iNode->key > pred1)pred1 = iNode->key;
        }


        for(DelNode *dNode : D_1){
            D2_min_D1.erase(dNode);
            D3Prime_min_D1.erase(dNode);
        }
        //pred2 is key of del node with largest key in d2 - d1
        for(DelNode *dNode : D2_min_D1){
            if(dNode->key > pred2)pred2 = dNode->key;
        }
        //pred3 is key of del node with largest key in d3prime - d1
        for(DelNode *dNode : D3Prime_min_D1){
            if(dNode->key > pred3)pred3 = dNode->key;
        }

        int64_t max = -1;
        if(pred0 > max) max = pred0;
        if(pred1 > max) max = pred1;
        if(pred2 > max) max = pred2;
        if(pred3 > max) max = pred3;
        return max;
    }
    int64_t predecessor(int64_t y){
        assert(y >= 0 && y <= universeSize); //TODO remove this eventually.
        if (y < 0 || y > universeSize){
            return -1; //x outside of range of trie.
        }
        threadInit();
        auto guard = recordMgr.getGuard(threadID());
        auto notifyGuard = notifyManager.getGuard(threadID());
        PredecessorNode *p = recordMgr.allocate<PredecessorNode>(threadID(), y, &notifyManager);
        P_ALL.insert(p);
        int64_t pred = predHelper(p);
        P_ALL.removeKey(p);
        p->retire(recordMgr); //PredNode p can be retired, since it is no longer in shared memory.
        return pred;
    }
    void printS(){
        std::ostringstream stream;
        int firstElem = 1;
        stream << "\n\t\tS: {";
        for(int64_t i = 0;i < universeSize;++i){
            if(search(i)){
                stream << ((!firstElem) ? ", " : "") << std::to_string(i);
                if(firstElem)firstElem = 0;
            }
        }
        stream << "}\n";
        std::cout << stream.str();
    }
    //TODO ifdef debug
    string interpretedBitsString(){
        std::ostringstream stream;
        TrieNode *curNode;
        //For each level
        for(int depth = 0;depth <= b;++depth){
            stream << "\t\t";
            //Add spaces for fancy formatting.
            int numSpaces = (1 << b) - (1 << depth);
            for(int space = 0;space < numSpaces; ++space){
                stream << ' ';
            }
            int height = b - depth;
            //For every trie node at the given depth, print interpreted bit.
            for(uint64_t n = 0; n < trieNodes[depth].size();++n){
                curNode = &trieNodes[depth][n];
                stream << std::to_string(interpretedBit(curNode, height)) << ' ';
            } 
            stream << "\n"; 
        }
        return stream.str();
    }
    void printInterpretedBits(){
        std::cout << interpretedBitsString();
    }

    //Verify that the interpreted bits of the structure are correct.
    bool verifyInterpretedBits(int index=0, int depth=0){
        if(depth == b)return true;
        else{
            int height = b - depth;
            int leftIndex = index << 1;
            int rightIndex = leftIndex + 1;
            TrieNode *node = &trieNodes[depth][index];
            TrieNode *leftChild = &trieNodes[depth+1][leftIndex];
            TrieNode *rightChild = &trieNodes[depth+1][rightIndex];
            
            if(interpretedBit(node, height) != 
                            (interpretedBit(leftChild, height-1) || interpretedBit(rightChild, height-1)))return false;

            if(!verifyInterpretedBits(leftIndex, depth+1))return false;
            else if(!verifyInterpretedBits(rightIndex, depth+1))return false;
            return true;
        }
    }
    void verifyLists(){
        for(auto it = U_ALL.begin();it != U_ALL.end();++it){
            assert(false);
        }
        for(auto it = P_ALL.begin();it != P_ALL.end();++it){
            assert(false);
        }
    }
    //TODO ifdef debug
    void printOpLog(){

    }

    //Initialize this thread with the record manager if this hasn't happened already.
    void threadInit(){
        assert(threadID() != -1);
        recordMgr.initThread(threadID());	
        notifyManager.initThread(threadID());
    }
};