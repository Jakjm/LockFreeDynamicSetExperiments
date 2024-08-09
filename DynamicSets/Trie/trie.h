#include <climits>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <map>
#include <vector>
#include "LinkedLists/PALL.h"
#include "LinkedLists/RUALL.h"
#include "LinkedLists/UALL.h"
#include "trieNodeTypes.h"
#include "BoundedMinReg/minreg.h"
#include <algorithm>
#include "../DynamicSet.h"

#include <thread>
#include <string>
#include <ostream>
#include <set>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include "../../common.h"


//Jeremy's trie structure source code.
using std::vector;
using std::unordered_set;
using std::set;
using std::string;
using std::map;



//Structure used to store pointers to insert/delete nodes that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated insert/delete node can be used again.
struct NodePool{
    InsNode *insNode;
    DelNode *delNode;
    NotifyNode *notifNode;
    volatile char padding[64 - 3*sizeof(uintptr_t)];
    NodePool() : insNode(nullptr), delNode(nullptr), notifNode(nullptr){

    }
    ~NodePool(){
        delete insNode;
        delete delNode;
        delete notifNode;
    }
};


//Comparator for UpdateNodes, returns true if a->key > b->key, else return false.
struct UpdateNodeGreater{
    bool operator ()(UpdateNode * const a, UpdateNode * const b) const {
        if(a->key > b->key)return true;
        else return false;
    }
};


struct UpdateNodeLesser{
    bool operator ()(UpdateNode * const a, UpdateNode * const b) const {
        if(a->key < b->key)return true;
        else return false;
    }
};

// thread_local int64_t predCounter = 0;
// thread_local int64_t longPredCounter = 0;

class Trie : public DynamicSet{
    private:
    const int trieHeight; //The height of the trie.
    const int64_t universeSize; //Equal to 2^trieHeight
    TrieNode * const trieNodes; //Array of TrieNodes...
    std::atomic<UpdateNode*> * const latest; //Array of LatestLists...
    PALL pall;
    UALL uall;
    RUALL ruall;
    NodePool nodePool[MAX_THREADS];
    public:
    Trie(int height) : trieHeight(height), universeSize((int64_t)1 << trieHeight), trieNodes(new TrieNode[universeSize - 1]), 
    latest(new std::atomic<UpdateNode*>[universeSize]), pall(), uall(), ruall()
    {
        for(int i = 0;i < MAX_THREADS;++i){
            nodePool[i].insNode = new InsNode();
            nodePool[i].delNode = new DelNode(trieHeight);
            nodePool[i].notifNode = new NotifyNode();
        }

        //Initialize the latest lists for each key in the universe.
        //Initialize row b of binary trie nodes.
        //Initially, each latest list's head = a DelNode with
        for(int64_t key = 0; key < universeSize;++key){
            DelNode *initialDelNode = new DelNode(trieHeight);
            initialDelNode->key = key;
            //trieRecordManager.allocate<DelNode>(threadID, key, trieHeight);
            initialDelNode->upper0Boundary = trieHeight; // The initial delNodes for the trie have upper0Boundary = trieHeight.
            latest[key] = initialDelNode;
            initialDelNode->status = ACTIVE;
            initialDelNode->dNodeCount = 1;
            

            //Initialize the dNodePointer of trieNodes...
            //Increment the dNodeCount accordingly.
            int k = key;
            int depth = trieHeight - 1;
            while(depth >= 0 && (k & 1) == 1){
                int depthOffset = (1 << depth) - 1;
                k /= 2;
                trieNodes[depthOffset + k].dNodePtr = initialDelNode;
                initialDelNode->dNodeCount++;
                depth--;
            }
        }

        // for(int h = 0; h < trieHeight;++h){
        //     for(int i = 0;i < (1 << h);++i){
        //         DelNode *dNode = trieNodes[(1 << h) - 1 + i].dNodePtr;
        //         cout << "<" << dNode << "," << ((int)dNode->dNodeCount) << "> ";
        //     }
        //     cout << std::endl;
        // }
        verifyLists();
    }
    ~Trie(){
        //trieRecordManager.startOp(threadID);
        //Attempt to retire all of the UpdateNodes stored in the latest lists
        //And all of the dNodePtrs.
        for(int i = 0;i < universeSize - 1;++i){
            DelNode *dNode = trieNodes[i].dNodePtr;
            int retire = dNode->dNodeCount--;
            if(retire == 1){
                delete dNode;
                //trieRecordManager.deallocate(threadID, dNode);
            }
        }
        

        for(int l = 0; l < universeSize;++l){
            UpdateNode *uNode = latest[l];
            UpdateNode *next = uNode->latestNext;
            if(uNode->type == INS){
                delete (InsNode*)uNode;
                //trieRecordManager.deallocate(threadID, (InsNode*)uNode);
            }
            else{
                DelNode *d = (DelNode*)uNode;
                int retire = d->dNodeCount.fetch_add(-1);
                assert(retire == 1);
                delete d;
                //trieRecordManager.deallocate(threadID, d);
            }
            if(next){
                if(next->type == INS){
                    delete (InsNode*)next;
                    //trieRecordManager.deallocate(threadID, (InsNode*)next);
                }
                else{
                    DelNode *d = (DelNode*)next;
                    int retire = d->dNodeCount.fetch_add(-1);
                    assert(retire == 1);
                    delete d;
                    //trieRecordManager.deallocate(threadID, d);
                }
            }
        }
        delete[] latest; //Delete array allocated for the latest lists.
        delete[] trieNodes;
        verifyLists();
        //Retire update nodes that are still in pools...
        //trieRecordManager.endOp(threadID);

        //Dump all limbo bags of their contents.
        // for(int i = 0;i < MAX_THREADS;++i){
        //     trieRecordManager.deinitThread(i);
        // }
                
    }
    




    UpdateNode *findLatest(int64_t x){
        UpdateNode *l = latest[x];
        if(l->status == INACTIVE){
            UpdateNode *m = l->latestNext;
            if(m)return m;
        }
        return l;
    }
    bool firstActivated(UpdateNode *u){
        UpdateNode *l = latest[u->key];
        return (u == l) || (l->status == INACTIVE && u == l->latestNext);
    }
    bool firstActivated(UpdateNode *u, int64_t key){
        UpdateNode *l = latest[key];
        return (u == l) || (l->status == INACTIVE && u == l->latestNext);
    }
    bool search(int64_t x){
        //trieRecordManager.startOp(threadID);
        bool containsX;
        debra.startOp();
        UpdateNode *l = findLatest(x);
        containsX = (l->type == INS);
        //trieRecordManager.endOp(threadID);
        debra.endOp();
        return containsX; //Return whether the root of the latest was an insert node.
    }
    char interpretedBit(int index, int depth){
        UpdateNode *uNode;
        if(depth == trieHeight){
            uNode = findLatest(index); //Check the latest list for key = index.
        }
        else{
            [[likely]];
            int depthOffset = (1 << depth) - 1;
            DelNode *dNode = trieNodes[depthOffset + index].dNodePtr;
            uNode = findLatest(dNode->key);
        }
        if(uNode->type == INS)return 1;
        else{
            DelNode *d = (DelNode*)uNode;
            int height = trieHeight - depth;
            if (height >= d->lower1Boundary.minRead()) return 1;
            else if(height <= d->upper0Boundary)return 0;
            else return 1;
        }
    }

    //Help to activate the update node created by an insert or delete operation....
    void helpActivate(UpdateNode *uNode){
        if (uNode->status == INACTIVE){
            uall.insert(uNode);
            ruall.insert(uNode);

            //STATUS expectedStatus = INACTIVE;
            //uNode->status.compare_exchange_strong(expectedStatus, ACTIVE);
            uNode->status = ACTIVE;

            //UpdateNode *latestNext = uNode->latestNext;
            //if(latestNext){
                
                //Set uNode->latestNext to nullptr
            UpdateNode *latestNext = uNode->latestNext.exchange(nullptr); 
            if(latestNext != nullptr){ //This operation unlinked latestNext from the latest list.
                if(latestNext->type == INS){
                    debra.reclaimLater((InsNode*)latestNext); //Put latestNext into a limbo bagif it's an insert node.
                }
                else{
                    //Otherwise, decrement latestNext's dNodeCount, and put latestNext into a limbo bag if dNodeCount was reduced to 0.
                    int retire = ((DelNode*)latestNext)->dNodeCount.fetch_add(-1); 
                    if(retire == 1){
                        debra.reclaimLater((DelNode*)latestNext);
                    }
                }
            }
            //The following code is not needed due to how we implemented the UALL/RUALL. 
            //The UALL/RUALL do not allow reinsertions, which means this helper doesn't need to remove uNode if uNode is not the first activComparatorated node...
            //if uNode is not the first activated node, then can removed uNode from UALL/RUALL
        }
    }

    
    //Traverses through the Update Announcement Linked List
    //Returns: - An integer, maxI, which is the maximum key over all active InsNodes with key < x encountered while traversing, or -1 if no such InsNode exists
    //         - A vector, D, of pointers to active DelNodes of key < x encountered while traversing.
    void traverseUALL(int64_t x, int64_t &maxI, int64_t &maxD, std::set<DelNode*> &D_ruall){
        UpdateNode *uNode = (UpdateNode*)uall.first();   
        maxD = maxI = -1;
        while(uNode && uNode->key < x){
            if(uNode->status != INACTIVE && firstActivated(uNode)){
                int64_t uKey = uNode->key;
                if(uNode->type == INS){
                    if(uKey > maxI)maxI = uKey;
                }
                else{//uNode is a DelNode (uNode->type is DEL)...
                    if(uKey > maxD && (D_ruall.find((DelNode*)uNode) == D_ruall.end())){
                        maxD = uKey;
                    }
                }
            }
            uNode = (UpdateNode*)uall.next(uNode);
        }                                                                                             
    }
    //Traverse through the Update Announcement Linked List
    //Returns a list of InsNodes that were the first active 
    void traverseUALL(vector<InsNode*> &I){
        UpdateNode *uNode = (UpdateNode*)uall.first();   
        while(uNode){
            if(uNode->type == INS && uNode->status != INACTIVE){
                if(I.empty() || I.back()->key != uNode->key){
                    if(firstActivated(uNode))I.push_back((InsNode*)uNode);
                }
            }
            uNode = (UpdateNode*)uall.next(uNode);
        }                                                                                             
    }

    //Notify predecessor operation by placing a new notify node in the notify list.
    bool sendNotification(NotifyNode *newNotifyNode, PredecessorNode *pNode){
        while(1){
            NotifyNode *nNode = pNode->notifyListHead;
            newNotifyNode->next = nNode;
            if(!firstActivated(newNotifyNode->updateNode)){
                return false; //If the updateNode is no longer the first activated one, no need to continue....
            }

            //Otherwise, attempt to put newNotifyNode at the head of the notifyList for pNode.
            NotifyNode *expected = nNode;

            //If pNode->notifyListHead is still nNode...
            if(pNode->notifyListHead == nNode){
                pNode->notifyListHead.compare_exchange_strong(expected,newNotifyNode); 
                if(expected == nNode){
                    return true; //Succeeded in putting newNotifyNode at the head of the notifyList.
                }
            }            
        }
    }

    //Send notifications to predecessor operations.
    void notifyPredOps(UpdateNode * const uNode){
        if(!firstActivated(uNode))return;
        vector<InsNode*> I; 
        traverseUALL(I);

        PredecessorNode *pNode = (PredecessorNode*)pall.first();
        InsNode dummyNode;
        while(pNode){
            //if(!firstActivated(uNode)) return;
            NotifyNode *nNode = nodePool[threadID].notifNode;

            nNode->key = uNode->key;
            nNode->updateNode = uNode;
            UpdateNode *notifyThres = pNode->ruallPosition.read();
            int64_t tau = notifyThres->key;
            nNode->notifyThreshold = tau;
            dummyNode.key = pNode->key;
            //auto iter = std::adjacent_find(I.begin(), I.end(), UpdateNodeLess());
            auto iter = std::upper_bound(I.rbegin(),I.rend(),&dummyNode, UpdateNodeGreater());
            if(iter == I.rend())nNode->updateNodeMax = nullptr;            
            else nNode->updateNodeMax = *iter;

            if(!sendNotification(nNode, pNode)){
                return;
            }
            
            nodePool[threadID].notifNode = new NotifyNode();
            pNode = (PredecessorNode*)pall.next(pNode);
        }
    }

    void insertBinaryTrie(InsNode * const iNode){
        //For each binary trie node t on the path from the parent of the leaf with iNode.key to the root, do 
        int64_t key = iNode->key;
        for(int depth = trieHeight-1;depth >= 0;--depth){
            key = key >> 1;
            int depthOffset = (1 << depth) - 1;
            TrieNode &t = trieNodes[depthOffset + key]; //Start from parent of 
            UpdateNode *dNodePtr = t.dNodePtr;
            UpdateNode *uNode = findLatest(dNodePtr->key);

            if (uNode->type == DEL){
                DelNode *delNode = (DelNode*)uNode;
                int height = trieHeight - depth;
                if (height < delNode->lower1Boundary.minRead() && height <= delNode->upper0Boundary){
                    iNode->target_key = delNode->key;
                    iNode->target = delNode;
                    if(firstActivated(iNode) == false)return;
                    delNode->lower1Boundary.minWrite(height);
                }
            }
        }
    }
    

    //               0
    //           0       1
    //         0   1    2  3
    //       0  1 2 3  4 5 6 7
    // If the index is odd, it is the right child. Subtract 1 to get the left child.
    // If the index is even, it is the left child. Add 1 to get the right child.
    #define siblingIndex(index) (index ^ 1)

    void deleteBinaryTrie(DelNode *dNode){
        int depth = trieHeight;
        int64_t key = dNode->key;
        while(depth > 0){
            if(interpretedBit(key, depth) == 1 || interpretedBit(siblingIndex(key), depth) == 1)return;

            //t = t->parent.
            --depth;
            int depthOffset = (1 << depth) - 1;
            key = key / 2;
            TrieNode *t = &trieNodes[depthOffset + key];

            DelNode *d = t->dNodePtr; //d is the oldValue of t->dNodePtr
            
            if(firstActivated(dNode) == false)return;
            if(dNode->stop || dNode->lower1Boundary.minRead() != trieHeight+1)return;
            
            
            DelNode *expected = d;
            dNode->dNodeCount.fetch_add(1); //Increment dNodeCount of dNode
            
            t->dNodePtr.compare_exchange_strong(expected, dNode);
            if(expected != d){
                d = expected;

                if(dNode->stop || (dNode->lower1Boundary.minRead() != trieHeight+1 || firstActivated(dNode) == false)){
                    dNode->dNodeCount.fetch_add(-1);
                    return;
                }
                expected = d;
                t->dNodePtr.compare_exchange_strong(expected, dNode);
                if(expected != d){
                    dNode->dNodeCount.fetch_add(-1);
                    return;
                }
            }

            //dNode has successfully taken d's place as t->dNodePtr
            //Put d in a limbo bag if it is no longer in shared memory.
            int count = d->dNodeCount.fetch_add(-1);
            if(count == 1){
                debra.reclaimLater(d);
            }
                //trieRecordManager.reclaimLater(threadID, d);

            if(interpretedBit(key * 2, depth + 1) || interpretedBit(key * 2 + 1, depth + 1))return;
            dNode->upper0Boundary = (trieHeight - depth);
        }
    }

    /**
    Insert operation on the binary trie.
    */
    bool insert(int64_t x){
        debra.startOp();
        //trieRecordManager.startOp(threadID);
        UpdateNode *dNode = findLatest(x);
        if (dNode->type == INS){
            debra.endOp();
            //trieRecordManager.endOp(threadID); //x already in S, nothing to do!
            return false;
        } 
        //dNode has type DEL and its child, if it has one, is of type INS
        InsNode *iNode = nodePool[threadID].insNode;
        iNode->key = x;
        iNode->latestNext = dNode;
        //dNode->latestNext = nullptr
        UpdateNode *latestNext = dNode->latestNext.exchange(nullptr); 
        //If latestNext was removed by this exchange operation....
        if(latestNext != nullptr){
            debra.reclaimLater((InsNode*)latestNext);
            //trieRecordManager.reclaimLater(threadID, (InsNode*)latestNext); //Retire the InsertNode following dNode in latest list.
        }
        UpdateNode *expected = dNode;
        latest[x].compare_exchange_strong(expected, iNode);
        if (expected != dNode){
            helpActivate(expected);
            //trieRecordManager.endOp(threadID);
            debra.endOp();
            return false;
        }
        
        nodePool[threadID].insNode = new InsNode(); //Ensure that iNode has been removed from the pool...
        uall.insert(iNode);
        ruall.insert(iNode);
        iNode->status = ACTIVE;
        //iNode->status.compare_exchange_strong( expectedStatus, ACTIVE);
        
        //Instead of iNode->latestNext = nullptr, use swap to determine if this swap removes 
        UpdateNode *result = iNode->latestNext.exchange(nullptr); 
        if(result == dNode){ //If this exchange removed dNode from the latest list....
            //Retire the delete node if it is no longer in the latest list or stored as a dNodePtr
            int retire = ((DelNode*)dNode)->dNodeCount.fetch_add(-1);
            if(retire == 1)debra.reclaimLater((DelNode*)dNode);
            //trieRecordManager.reclaimLater(threadID, (DelNode*)dNode);
        }
        
        insertBinaryTrie(iNode);
        notifyPredOps(iNode); //Notify predecessor operations that this insertion is in progress...

        //for each node uNode in U-ALL with uNode.key = x before and including v do

        uall.remove(iNode);
        ruall.remove(iNode);
        debra.endOp();
        return true;
        //trieRecordManager.endOp(threadID);
    }
    

    bool remove(int64_t x){
        debra.startOp();
        //trieRecordManager.startOp(threadID);
        UpdateNode *iNode = findLatest(x);
        if(iNode->type == DEL){ //Already a delNode with key x at head of latestList...
            debra.endOp();
            //trieRecordManager.endOp(threadID);
            return false; //x is not in S, nothing to do!
        }
        //iNode has type INS. If it has a child, its child has type DEL.
        
        PredecessorNode *pNode = new PredecessorNode(x, &ruall.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred = predHelper(pNode);

        //Initialize update node for this delete operation.
        DelNode *dNode = nodePool[threadID].delNode;
        dNode->key = x;
        dNode->delPredNode = pNode;
        dNode->delPred = delPred;
        dNode->latestNext = iNode;
    
        
        UpdateNode *latestNext = iNode->latestNext.exchange(nullptr); 
            //Swap iNode's latestNext with nullptr.
        if(latestNext != nullptr){ //If this operation removed latestNext, decrement its dNodeCount.
            int retire = ((DelNode*)latestNext)->dNodeCount.fetch_add(-1);
            if(retire == 1)debra.reclaimLater((DelNode*)latestNext);
            //trieRecordManager.reclaimLater(threadID, (DelNode*)latestNext); //Retire if dNodeCount was lowered to 0.
        }   
        
        //If operations might not have been notified about iNode, 
        //Notify predecessor ops about iNode.
        uint64_t iNodeState = (iNode->succ & STATUS_MASK);
        if(iNodeState != Marked)notifyPredOps(iNode); 

        UpdateNode *expected = iNode;
        latest[x].compare_exchange_strong(expected, dNode);

        if(expected != iNode){ //Failed to CAS dNode to head of latest list....
            //There was a different node, expected, instead of iNode at the head of the latest list prior to our CAS.
            //We will help activate it.
            helpActivate(expected);

            //Remove pNode from pall.
            pall.remove(pNode);

            //Retire pNode as it is no longer in shared memory.
            debra.reclaimLater(pNode);
            //trieRecordManager.reclaimLater(threadID, pNode);
            debra.endOp();
            //trieRecordManager.endOp(threadID);
            return false;
        }
        
        nodePool[threadID].delNode = new DelNode(trieHeight); //Remove dNode from the pool; it should not be reused for the next deletion
        uall.insert(dNode);
        ruall.insert(dNode);

        dNode->status = ACTIVE;

        DelNode *target = ((InsNode*)iNode)->target;
        if(target && firstActivated(target, ((InsNode*)iNode)->target_key))target->stop = true;

        //Swap dNode's latestNext with nullptr.
        UpdateNode *result = dNode->latestNext.exchange(nullptr);
        if(result == iNode){  
            //If this CAS unlinked iNode from the latest list, retire iNode.
            debra.reclaimLater((InsNode*)iNode);
            //trieRecordManager.reclaimLater(threadID, (InsNode*)iNode);
        }
        
        PredecessorNode *pNode2 = new PredecessorNode(x, &ruall.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred2 = predHelper(pNode2);
        dNode->delPred2 = delPred2;

        deleteBinaryTrie(dNode);
        notifyPredOps(dNode);

        // Delete pNode and pNode2 from P-ALL.
        pall.remove(pNode);
        pall.remove(pNode2);

        //Retire both pNode and pNode2.
        debra.reclaimLater(pNode);
        debra.reclaimLater(pNode2);
        //trieRecordManager.reclaimLater(threadID, pNode);
        //trieRecordManager.reclaimLater(threadID, pNode2);

        uall.remove(dNode);
        ruall.remove(dNode);

        int retire = dNode->dNodeCount.fetch_add(-1);
        if(retire == 1)debra.reclaimLater(dNode);
        //trieRecordManager.reclaimLater(threadID, dNode);
        debra.endOp();
        //trieRecordManager.endOp(threadID);
        return true;
    }

    //Computes the relaxed predecessor of key y.
    //This is done by traversing up and then down the binary trie.
    int64_t relaxedPredecessor(int64_t y){
        //Let t be the latestList for key y.
        int depth = trieHeight; //depth is the depth of t.
        
        //While t is the left sibling or the sibling of t (which is the left child) has interpreted bit 0
        while((y & 1) == 0 || interpretedBit(y - 1, depth) == 0){
            //t = t.parent
            y = y >> 1; 
            --depth; 
            if(depth == 0)return -1;
            
        }

        //t = t.parent.left 
        //Subtract 1 from y if it is odd. 
        y = y - (y & 1);
        while(depth < trieHeight){
            ++depth; 
            //Right child is at 2*y + 1, left child is at 2 * y.
            int64_t rightIndex = y * 2 + 1;
            if(interpretedBit(rightIndex, depth) == 1){ //If interpreted bit of t.right is 1, t = t.right
                y = rightIndex;
            }
            else{
                int64_t leftIndex = y * 2;
                if(interpretedBit(leftIndex, depth) == 1){ //If interpreted bit of t.left is 1, t = t.left
                    y = leftIndex;
                }
                else{
                    //Interpreted bits of left child and right child are 0. No predecessor found.
                    return -2;
                }
            }
        }
        return y; //Return t.key
    }
    

    //Traverse the reverse update announcement linked list.
    void traverseRUALL(PredecessorNode *pNode, set<InsNode *> &I, set<DelNode*> &D){
        UpdateNode *uNode = (UpdateNode*)ruall.first(pNode); //Atomically set pNode.notifyThreshold....
        while(uNode){
            if(uNode->key < pNode->key){
                if(uNode->status != INACTIVE && firstActivated(uNode)){
                    if(uNode->type == INS) I.insert((InsNode*)uNode);
                    else D.insert((DelNode*)uNode);
                }
            }
            uNode = (UpdateNode*)ruall.next(pNode,uNode); //Atomically set pNode.notifyThreshold....
        }
    }



    int64_t predHelper(PredecessorNode *pNode){
        int64_t y = pNode->key;
        int64_t max_I_Notify = -1;
        int64_t max_D_notify = -1;
        int64_t max_I_uall; //Maximum key of an active INS node encountered while traversing an 
        //Let D_uall be the set of all active DelNodes with key < yencountered while traversing UALL
        //max_D_uall is the maximum key of such a DelNode that is not in D_ruall.
        int64_t max_D_uall; 
        //vector<DelNode*> D_notify;
        set<InsNode*> I_ruall;
        set<DelNode*> D_ruall;

        //Insert pNode into the PALL
        pall.insert(pNode);
        
        //Traverse the pall from the PredecessorNode following pNode to the end, inserting every node encountered into Q 
        vector<PredecessorNode*> Q;
        PredecessorNode *p = pall.next(pNode);
        while(p){
            Q.push_back(p);
            p = pall.next(p);
        }

        traverseRUALL(pNode,I_ruall, D_ruall);
        int64_t r0 = relaxedPredecessor(y);
        traverseUALL(y, max_I_uall, max_D_uall, D_ruall);

        //Traverse pNode's notify list...
        NotifyNode *nNode = pNode->notifyListHead;
        while(nNode){
            if(nNode->key < y){ //For every NotifyNode in pNode's notifyList with key < y....
                if(nNode->updateNode->type == INS){
                    if(nNode->notifyThreshold <= nNode->key && nNode->key > max_I_Notify){
                        max_I_Notify = nNode->key;
                        //I_notify.push_back((InsNode*)nNode->updateNode);
                    }
                }
                else if(nNode->key > nNode->notifyThreshold){
                    if(nNode->key > max_D_notify && (D_ruall.find((DelNode*)nNode->updateNode) != D_ruall.end())){
                        max_D_notify = nNode->key;
                    }
                    //D_notify.push_back((DelNode*)nNode->updateNode);
                }
                if((nNode->notifyThreshold == -1) && 
                    ((nNode->updateNode->type == INS && (I_ruall.find((InsNode*)nNode->updateNode)) == I_ruall.end())) && 
                    ((nNode->updateNode->type == DEL && (D_ruall.find((DelNode*)nNode->updateNode)) == D_ruall.end()))){
                    if(nNode->updateNodeMax && nNode->updateNodeMax->key > max_I_Notify){
                        max_I_Notify = nNode->updateNodeMax->key;
                        //I_notify.push_back(nNode->updateNodeMax);
                    }
                }
            }
            nNode = nNode->next;
        }
        //r1 = key of UpdateNode with largest key among those in the union of I_uall, I_notify, (D_uall - D_ruall) and (D_notify - D_ruall).
        int64_t r1 = max_I_Notify;

        //Let i be the InsNode of maximum key in I_all, if one exists.
        //if i->key > r1, r1 = i->key
        if(max_I_uall > r1)r1 = max_I_uall;
        if(max_D_uall > r1)r1 = max_D_uall;
        if(max_D_notify > r1)r1 = max_D_notify;

        //Traversal of the binary trie stopped while traversing back down.....
        if(r0 == -2 && !D_ruall.empty()){
            set<PredecessorNode*> predNodes; //Set containing the delPredNodes of DeleteNodes encountered in the RUALL.
            //set<int64_t> R; //Set containing the delPreds of DeleteNodes encountered in the RUALL.
            for(DelNode *d : D_ruall){
                predNodes.insert(d->delPredNode);
                //R.insert(d->delPred);
            }

            //pNodePrime is the predecessor node in predNodes that is the latest in Q.
            PredecessorNode *pNodePrime = nullptr;
            for (auto rit = Q.rbegin(); rit != Q.rend(); ++rit) { //Iterate through Q backwards.
                PredecessorNode *p = *rit;
                if(predNodes.find(p) != predNodes.end()){ //Stop as soon as such a predecessor node is found.
                    pNodePrime = p;
                    break;
                }
            }
            
            vector<UpdateNode*> L1;
            if(pNodePrime){
                set<UpdateNode*> alreadyIn; //Set of UpdateNodes already in L1....
                //Insert all updateNodes of the notify nodes of pNodePrime to LPrime.
                NotifyNode *nNode = pNodePrime->notifyListHead;
                while(nNode){
                    if(nNode->key < y){
                        UpdateNode *uNode = nNode->updateNode;
                        //Put uNode in L1 if it was not already in L1.
                        if(alreadyIn.find(uNode) == alreadyIn.end()){
                            L1.push_back(uNode);
                            alreadyIn.insert(uNode);
                        }
                    }
                    nNode = nNode->next;
                }
            }

            set<UpdateNode*> LPrime; //Set of UpdateNodes that should be removed from L1
            set<UpdateNode*> alreadyIn; //Set of UpdateNodes already in L2....
            vector<UpdateNode*> L2; //L2 is a stack. The top of the stack is at the end of the vector.
            //For every notifyNode in pNode's notifyList with key < y
            nNode = pNode->notifyListHead;
            while(nNode){
                if(nNode->key < y){
                    UpdateNode *uNode = nNode->updateNode;
                    LPrime.insert(uNode);
                    if(nNode->key <= nNode->notifyThreshold){
                        if(alreadyIn.find(uNode) == alreadyIn.end()){
                            L2.push_back(uNode);
                            alreadyIn.insert(uNode);
                        }
                    }
                }
                nNode = nNode->next;
            }

            //sequence of UpdateNodes in L1 then in L2; 
            vector<UpdateNode*> L3;

            //lastDelKey maps keys of UpdateNodes in L3 to the last UpdateNode of that key in L3.
            map<int64_t, UpdateNode*> lastDelKey;
            //Add update nodes from L1 to L3
            for(auto rit = L1.rbegin(); rit != L1.rend(); ++rit){
                UpdateNode *uNode = *rit;
                //Skip this UpdateNode
                if(LPrime.find(uNode) != LPrime.end())continue;
                L3.push_back(uNode);
                lastDelKey.insert_or_assign(uNode->key, uNode);
            }
            //Add update nodes from L2 to L3
            for(auto rit = L2.rbegin(); rit != L2.rend(); ++rit){
                UpdateNode *uNode = *rit;
                L3.push_back(uNode);
                lastDelKey.insert_or_assign(uNode->key, uNode);
            }

            //L = L3 - {dNode | dNode is a DelNode in L3 which is not the last update node of that key}
            vector<UpdateNode*> L;
            for(auto it = L3.begin();it != L3.end();++it){
                UpdateNode *uNode = *it;
                if(uNode->type == INS || lastDelKey[uNode->key] == uNode){
                    L.push_back(uNode);
                }
            }
            //DelNodes in L all have unique keys...

            //V = {uNode.key | uNode in L} union {dNode.delPred2 | dNode in L} union {dNode.delPred | dNode in Druall}
            //Let E be the set of directed edges <u, v> where dNode.key = u and dNode.delPred2 = v, where dNode in L
            //Let T_L = <V, E>
            //For every edge <u,v> in edges, u > v since v is the return value of an embedded predecessor operation with key u.
            vector<int64_t> X;
            //set<int64_t> vertices;
            map<int64_t, int64_t> edges;
            for(UpdateNode *uNode : L){
                int64_t uKey = uNode->key;
                //vertices.insert(uKey);
                if(uNode->type == DEL){
                    DelNode *dNode = (DelNode*)uNode;
                    int64_t delPred2 = dNode->delPred2;
                    //vertices.insert(delPred2);
                    edges.insert_or_assign(uKey, delPred2);
                }
                else{
                    X.push_back(uKey);
                }
            }
            for(DelNode *dNode : D_ruall){
                //int64_t dKey = dNode->key;
                int64_t delPred = dNode->delPred;
                X.push_back(delPred);
                //vertices.insert(dNode->key);
            }

            //Let R be the set of sinks reachable from keys in X
            set<int64_t> R;
            //We could optimize this by keeping track of nodes we've already seen,
            //but this case occurs pretty rarely and that optimization would have some cost.
            //For every vertex n in X, traverse from v until you get to a sink.
            for(int64_t n : X){
                auto edgeIt = edges.find(n);
                while(edgeIt != edges.end()){
                    auto pair = *edgeIt;
                    n = pair.second;
                    edgeIt = edges.find(n);
                }
                //n is now a sink.
                R.insert(n);
            }

            //R = R - {x | x in R and exists dNode in D_ruall such that dNode.key = x}
            for(DelNode *dNode : D_ruall){
                auto it = R.find(dNode->key);
                if(it != R.end()){
                    R.erase(it);
                }
            }

            //r0 = maximum among keys in R
            auto maxIter = R.rbegin();
            r0 = *maxIter;
        }

        //Return the max among r0 and r1.
        if(r0 > r1)return r0;
        else return r1;
    }
    int64_t predecessor(int64_t y){
        debra.startOp();
        //trieRecordManager.startOp(threadID);
        
        PredecessorNode *p = new PredecessorNode(y, &ruall.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, y);
        int64_t pred = predHelper(p);
        pall.remove(p);
        debra.reclaimLater(p);
        //trieRecordManager.reclaimLater(threadID, p); //PredNode p can be retired, since it is no longer in shared memory.

        debra.endOp();
        //trieRecordManager.endOp(threadID);
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
        //For each level
        for(int depth = 0;depth <= trieHeight;++depth){
            stream << "\t\t";
            //Add spaces for fancy formatting.
            int numSpaces = (1 << trieHeight) - (1 << depth);
            for(int space = 0;space < numSpaces; ++space){
                stream << ' ';
            }
            //For every trie node at the given depth, print interpreted bit.
            int64_t numNodes = (1 << depth);
            for(int64_t n = 0; n < numNodes; ++n){
                stream << std::to_string(interpretedBit(n, depth)) << ' ';
            } 
            stream << "\n"; 
        }
        return stream.str();
    }
    void printInterpretedBits(){
        std::cout << interpretedBitsString();
    }

    string name(){
        return "Jeremy's Trie";
    }

    //Verify that the interpreted bits of the structure are correct.
    bool verifyInterpretedBits(int index=0, int depth=0){
        if(depth == trieHeight)return true;
        else{
            int leftIndex = index << 1;
            int rightIndex = leftIndex + 1;

            int iBit = interpretedBit(index, depth);
            int leftBit = interpretedBit(leftIndex, depth+1);
            int rightBit = interpretedBit(rightIndex, depth+1);
            
            //Validate that interpreted bit of D_depth[index] = its left child's bit | it's right child's bit.
            if(iBit != (leftBit | rightBit))return false; 

            if(!verifyInterpretedBits(leftIndex, depth+1))return false;
            else if(!verifyInterpretedBits(rightIndex, depth+1))return false;
            return true;
        }
    }

    //Verify that all of the lists are empty at the end of the execution.
    void verifyLists(){
        assert(uall.head.succ == (uintptr_t)&uall.tail);
        assert(ruall.head.rSucc == (uintptr_t)&ruall.tail);
        assert(pall.head.succ == (uintptr_t)&pall.tail);
    }
};
