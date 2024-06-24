#include <climits>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include "LinkedLists/PALL.h"
#include "LinkedLists/RUALL_notifDesc.h"
#include "LinkedLists/RUALL_swCopy.h"
#include "LinkedLists/UALL.h"
#include "trieNodeTypes.h"
#include "BoundedMinReg/minreg.h"
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




//Structure used to store pointers to insert/delete nodes that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated insert/delete node can be used again.
template <typename NotifDescType>
struct NodePool{
    InsNode<NotifDescType> *insNode;
    DelNode<NotifDescType> *delNode;
    NotifyNode<NotifDescType> *notifNode;
    volatile char padding[64 - 3*sizeof(uintptr_t)];
    NodePool() : insNode(nullptr), delNode(nullptr), notifNode(nullptr){

    }
    ~NodePool(){
        delete insNode;
        delete delNode;
        delete notifNode;
    }
};


//Comparator for UpdateNodes, returns true if a->key > b->key or  a->key == b->key and a > b.
struct UpdateNodeGreater{
    bool operator ()(UpdateNode * const a, UpdateNode * const b) const {
        int64_t diff = a->key - b->key;
        if(diff > 0)return true;
        else if(diff < 0)return false;
        else return a > b;
    }
};
//Comparator for UpdateNodes, returns true if a->key > b->key or  a->key == b->key and a > b.
struct UpdateNodeLess{
    bool operator ()(UpdateNode * const a, UpdateNode * const b) const {
        int64_t diff = a->key - b->key;
        if(diff < 0)return true;
        else if(diff > 0)return false;
        else return a > b;
    }
};



#define reuse 1 //If reuse is defined, update nodes that are not inserted into the trie will be reused.

template <typename NotifDescType = AtomicCopyNotifyThreshold>
class Trie : public DynamicSet{
    private:
    const int trieHeight; //The height of the trie.
    const int64_t universeSize; //Equal to 2^trieHeight
    TrieNode<NotifDescType> * const trieNodes;
    std::atomic<UpdateNode*> * const latest;
    PALL<NotifDescType> pall;
    UALL uall;
    RUALL<NotifDescType> ruall;
    NodePool<NotifDescType> nodePool[MAX_THREADS];
    public:
    Trie(int height) : trieHeight(height), universeSize((int64_t)1 << trieHeight), trieNodes(new TrieNode<NotifDescType>[universeSize - 1]), 
    latest(new std::atomic<UpdateNode*>[universeSize]), pall(), uall(), ruall()
    {
        for(int i = 0;i < MAX_THREADS;++i){
            nodePool[i].insNode = new InsNode<NotifDescType>();
            nodePool[i].delNode = new DelNode<NotifDescType>(trieHeight);
            nodePool[i].notifNode = new NotifyNode<NotifDescType>();
        }


        //Initialize the binary trie nodes for each level of the trie.
        // for(int i = 0; i < trieHeight;++i){
        //     int64_t rowSize = (1 << i); //Row size = 2^i
        //     TrieNode<NotifDescType> *trieNodeRow = new TrieNode<NotifDescType>[rowSize];
        //     trieNodes[i] = trieNodeRow;
        // }


        //Initialize the latest lists for each key in the universe.
        //Initialize row b of binary trie nodes.
        //Initially, each latest list's head = a DelNode with
        for(int64_t key = 0; key < universeSize;++key){
            DelNode<NotifDescType> *initialDelNode = new DelNode<NotifDescType>(trieHeight);
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
        //         DelNode<NotifDescType> *dNode = trieNodes[(1 << h) - 1 + i].dNodePtr;
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
            DelNode<NotifDescType> *dNode = trieNodes[i].dNodePtr;
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
                delete (InsNode<NotifDescType>*)uNode;
                //trieRecordManager.deallocate(threadID, (InsNode*)uNode);
            }
            else{
                DelNode<NotifDescType> *d = (DelNode<NotifDescType>*)uNode;
                int retire = d->dNodeCount.fetch_add(-1);
                assert(retire == 1);
                delete d;
                //trieRecordManager.deallocate(threadID, d);
            }
            if(next){
                if(next->type == INS){
                    delete (InsNode<NotifDescType>*)next;
                    //trieRecordManager.deallocate(threadID, (InsNode*)next);
                }
                else{
                    DelNode<NotifDescType> *d = (DelNode<NotifDescType>*)next;
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
        trieDebra.startOp();
        UpdateNode *l = findLatest(x);
        containsX = (l->type == INS);
        //trieRecordManager.endOp(threadID);
        trieDebra.endOp();
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
            DelNode<NotifDescType> *dNode = trieNodes[depthOffset + index].dNodePtr;
            uNode = findLatest(dNode->key);
        }
        if(uNode->type == INS)return 1;
        else{
            DelNode<NotifDescType> *d = (DelNode<NotifDescType>*)uNode;
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
                    trieDebra.reclaimLater((InsNode<NotifDescType>*)latestNext); //Put latestNext into a limbo bagif it's an insert node.
                }
                else{
                    //Otherwise, decrement latestNext's dNodeCount, and put latestNext into a limbo bag if dNodeCount was reduced to 0.
                    int retire = ((DelNode<NotifDescType>*)latestNext)->dNodeCount.fetch_add(-1); 
                    if(retire == 1){
                        trieDebra.reclaimLater((DelNode<NotifDescType>*)latestNext);
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
    void traverseUALL(int64_t x, int64_t &maxI, int64_t &maxD, std::set<DelNode<NotifDescType>*> &D_ruall){
        UpdateNode *uNode = (UpdateNode*)uall.first();   
        maxD = maxI = -1;
        while(uNode && uNode->key < x){
            if(uNode->status != INACTIVE && firstActivated(uNode)){
                int64_t uKey = uNode->key;
                if(uNode->type == INS){
                    if(uKey > maxI)maxI = uKey;
                }
                else{
                    if(uKey > maxD && (D_ruall.find((DelNode<NotifDescType>*)uNode) == D_ruall.end())){
                        maxD = uKey;
                    }
                }
            }
            uNode = (UpdateNode*)uall.next(uNode);
        }                                                                                             
    }
    //Traverse through the Update Announcement Linked List
    //Returns a set, I, of active InsNodes encountered while traversing the UALL.
    void traverseUALL(set<InsNode<NotifDescType>*,UpdateNodeGreater> &I){
        UpdateNode *uNode = (UpdateNode*)uall.first();   
        while(uNode){
            if(uNode->type == INS && uNode->status != INACTIVE && firstActivated(uNode)){
                I.insert((InsNode<NotifDescType>*)uNode);
            }
            uNode = (UpdateNode*)uall.next(uNode);
        }                                                                                             
    }

    //Notify predecessor operation by placing a new notify node in the notify list.
    bool sendNotification(NotifyNode<NotifDescType> *newNotifyNode, PredecessorNode<NotifDescType> *pNode){
        while(1){
            NotifyNode<NotifDescType> *nNode = pNode->notifyListHead;
            newNotifyNode->next = nNode;
            if(!firstActivated(newNotifyNode->updateNode)){
                return false; //If the updateNode is no longer the first activated one, no need to continue....
            }

            //Otherwise, attempt to put newNotifyNode at the head of the notifyList for pNode.
            NotifyNode<NotifDescType> *expected = nNode;
            pNode->notifyListHead.compare_exchange_strong(expected,newNotifyNode); 
            if(expected == nNode){
                return true; //Succeeded in putting newNotifyNode at the head of the notifyList.
            }
        }
    }

    //Send notifications to predecessor operations.
    void notifyPredOps(UpdateNode * const uNode){
        if(!firstActivated(uNode))return;
        set<InsNode<NotifDescType>*, UpdateNodeGreater> I; 
        traverseUALL(I);

        PredecessorNode<NotifDescType> *pNode = (PredecessorNode<NotifDescType>*)pall.first();
        InsNode<NotifDescType> dummyNode;
        while(pNode){
            //if(!firstActivated(uNode)) return;
            NotifyNode<NotifDescType> *nNode = nodePool[threadID].notifNode;

            nNode->key = uNode->key;
            nNode->updateNode = uNode;
            UpdateNode *notifyThres = pNode->notifyThreshold.read();
            int64_t tau = notifyThres->key;
            nNode->notifyThreshold = tau;
            dummyNode.key = pNode->key;
            auto iter = I.upper_bound(&dummyNode);
            if(iter == I.end())nNode->updateNodeMax = nullptr;            
            else nNode->updateNodeMax = *iter;

            if(!sendNotification(nNode, pNode)){
                return;
            }
            nodePool[threadID].notifNode = new NotifyNode<NotifDescType>();
            pNode = (PredecessorNode<NotifDescType>*)pall.next(pNode);
        }
    }

    void insertBinaryTrie(InsNode<NotifDescType> * const iNode){
        //For each binary trie node t on the path from the parent of the leaf with iNode.key to the root, do 
        int64_t key = iNode->key;
        for(int depth = trieHeight-1;depth >= 0;--depth){
            key = key >> 1;
            int depthOffset = (1 << depth) - 1;
            TrieNode<NotifDescType> &t = trieNodes[depthOffset + key]; //Start from parent of 
            UpdateNode *dNodePtr = t.dNodePtr;
            UpdateNode *uNode = findLatest(dNodePtr->key);

            if (uNode->type == DEL){
                DelNode<NotifDescType> *delNode = (DelNode<NotifDescType>*)uNode;
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

    void deleteBinaryTrie(DelNode<NotifDescType> *dNode){
        int depth = trieHeight;
        int64_t key = dNode->key;
        while(depth > 0){
            if(interpretedBit(key, depth) == 1 || interpretedBit(siblingIndex(key), depth) == 1)return;

            //t = t->parent.
            --depth;
            int depthOffset = (1 << depth) - 1;
            key = key / 2;
            TrieNode<NotifDescType> *t = &trieNodes[depthOffset + key];

            DelNode<NotifDescType> *d = t->dNodePtr; //d is the oldValue of t->dNodePtr
            
            if(firstActivated(dNode) == false)return;
            if(dNode->stop || dNode->lower1Boundary.minRead() != trieHeight+1)return;
            
            
            DelNode<NotifDescType> *expected = d;
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
                trieDebra.reclaimLater(d);
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
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);
        UpdateNode *dNode = findLatest(x);
        if (dNode->type == INS){
            trieDebra.endOp();
            //trieRecordManager.endOp(threadID); //x already in S, nothing to do!
            return false;
        } 
        //dNode has type DEL and its child, if it has one, is of type INS

        #ifdef reuse 
        InsNode<NotifDescType> *iNode = nodePool[threadID].insNode;
        #else
        InsNode *iNode = trieRecordManager.allocate<InsNode>(threadID); //Allocate a new node for each insert
        #endif
        iNode->key = x;
        iNode->latestNext = dNode;
        //dNode->latestNext = nullptr
        UpdateNode *latestNext = dNode->latestNext.exchange(nullptr); 
        //If latestNext was removed by this exchange operation....
        if(latestNext != nullptr){
            trieDebra.reclaimLater((InsNode<NotifDescType>*)latestNext);
            //trieRecordManager.reclaimLater(threadID, (InsNode*)latestNext); //Retire the InsertNode following dNode in latest list.
        }
        UpdateNode *expected = dNode;
        latest[x].compare_exchange_strong(expected, iNode);
        if (expected != dNode){
            helpActivate(expected);
            #ifdef reuse //This insert node can be reused by subsequent insert operations...
            #else
            trieRecordManager.deallocate(threadID, iNode);
            #endif
            //trieRecordManager.endOp(threadID);
            trieDebra.endOp();
            return false;
        }
        #ifdef reuse 
        nodePool[threadID].insNode = new InsNode<NotifDescType>(); //Ensure that iNode has been removed from the pool...
        #endif 
        uall.insert(iNode);
        ruall.insert(iNode);
        iNode->status = ACTIVE;
        //iNode->status.compare_exchange_strong( expectedStatus, ACTIVE);
        
        //Instead of iNode->latestNext = nullptr, use swap to determine if this swap removes 
        UpdateNode *result = iNode->latestNext.exchange(nullptr); 
        if(result == dNode){ //If this exchange removed dNode from the latest list....
            //Retire the delete node if it is no longer in the latest list or stored as a dNodePtr
            int retire = ((DelNode<NotifDescType>*)dNode)->dNodeCount.fetch_add(-1);
            if(retire == 1)trieDebra.reclaimLater((DelNode<NotifDescType>*)dNode);
            //trieRecordManager.reclaimLater(threadID, (DelNode*)dNode);
        }
        
        insertBinaryTrie(iNode);
        notifyPredOps(iNode); //Notify predecessor operations that this insertion is in progress...

        //for each node uNode in U-ALL with uNode.key = x before and including v do

        uall.remove(iNode);
        ruall.remove(iNode);
        trieDebra.endOp();
        return true;
        //trieRecordManager.endOp(threadID);
    }
    

    bool remove(int64_t x){
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);
        UpdateNode *iNode = findLatest(x);
        if(iNode->type == DEL){ //Already a delNode with key x at head of latestList...
            trieDebra.endOp();
            //trieRecordManager.endOp(threadID);
            return false; //x is not in S, nothing to do!
        }
        //iNode has type INS. If it has a child, its child has type DEL.

        PredecessorNode<NotifDescType> *pNode = new PredecessorNode<NotifDescType>(x, &ruall.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred = predHelper(pNode);

        //Initialize update node for this delete operation.
        #ifdef reuse 
        DelNode<NotifDescType> *dNode = nodePool[threadID].delNode;
        #else 
        DelNode *dNode = trieRecordManager.allocate<DelNode>(threadID, trieHeight); //Allocate a delNode for each delete...
        #endif
        dNode->key = x;
        dNode->delPredNode = pNode;
        dNode->delPred = delPred;
        dNode->latestNext = iNode;
    
        
        UpdateNode *latestNext = iNode->latestNext.exchange(nullptr); 
            //Swap iNode's latestNext with nullptr.
        if(latestNext != nullptr){ //If this operation removed latestNext, decrement its dNodeCount.
            int retire = ((DelNode<NotifDescType>*)latestNext)->dNodeCount.fetch_add(-1);
            if(retire == 1)trieDebra.reclaimLater((DelNode<NotifDescType>*)latestNext);
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

            #ifdef reuse
            #else 
            trieRecordManager.deallocate(threadID, dNode);
            #endif
            //Retire pNode as it is no longer in shared memory.
            trieDebra.reclaimLater(pNode);
            //trieRecordManager.reclaimLater(threadID, pNode);
            trieDebra.endOp();
            //trieRecordManager.endOp(threadID);
            return false;
        }
        #ifdef reuse
        nodePool[threadID].delNode = new DelNode<NotifDescType>(trieHeight); //Remove dNode from the pool; it should not be reused for the next deletion
        #endif
        uall.insert(dNode);
        ruall.insert(dNode);

        dNode->status = ACTIVE;

        DelNode<NotifDescType> *target = ((InsNode<NotifDescType>*)iNode)->target;
        if(target && firstActivated(target, ((InsNode<NotifDescType>*)iNode)->target_key))target->stop = true;

        //Swap dNode's latestNext with nullptr.
        UpdateNode *result = dNode->latestNext.exchange(nullptr);
        if(result == iNode){  
            //If this CAS unlinked iNode from the latest list, retire iNode.
            trieDebra.reclaimLater((InsNode<NotifDescType>*)iNode);
            //trieRecordManager.reclaimLater(threadID, (InsNode*)iNode);
        }
        PredecessorNode<NotifDescType> *pNode2 = new PredecessorNode<NotifDescType>(x, &ruall.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred2 = predHelper(pNode2);
        dNode->delPred2 = delPred2;

        deleteBinaryTrie(dNode);
        notifyPredOps(dNode);

        // Delete pNode and pNode2 from P-ALL.
        pall.remove(pNode);
        pall.remove(pNode2);

        //Retire both pNode and pNode2.
        trieDebra.reclaimLater(pNode);
        trieDebra.reclaimLater(pNode2);
        //trieRecordManager.reclaimLater(threadID, pNode);
        //trieRecordManager.reclaimLater(threadID, pNode2);

        uall.remove(dNode);
        ruall.remove(dNode);

        int retire = dNode->dNodeCount.fetch_add(-1);
        if(retire == 1)trieDebra.reclaimLater(dNode);
        //trieRecordManager.reclaimLater(threadID, dNode);
        trieDebra.endOp();
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
    


    // void traverseAndInsertPALL(PredecessorNode<NotifDescType> *newNode, deque<PredecessorNode<NotifDescType>*> &q){
    //     set<PredecessorNode<NotifDescType>*> qSet;

    //     PredecessorNode<NotifDescType> *first = (PredecessorNode<NotifDescType>*)(pall.head.succ.load() & NEXT_MASK);
    //     PredecessorNode<NotifDescType> *pNode = (PredecessorNode<NotifDescType>*)first;
    //     //Traverse pall from start to end
    //     while(pNode){
    //         q.push_back(pNode);
    //         qSet.insert(pNode);
    //         pNode = (PredecessorNode<NotifDescType>*)pall.next(pNode);
    //     }

    //     //Insert newNode into pall
    //     while(1){
    //         //Set newNode's next to first.
    //         newNode->succ = (uintptr_t)first;
    //         uintptr_t expected = (uintptr_t)first;

    //         pall.head.succ.compare_exchange_strong(expected, (uintptr_t)newNode);
    //         if(expected == (uintptr_t)first){
    //             q.push_front(newNode); //Put newNode at the front of q.
    //             return; //newNode was successfully inserted
    //         }
    //         int64_t state = (int64_t)(expected & STATUS_MASK);
    //         PredecessorNode<NotifDescType> *next = (PredecessorNode<NotifDescType>*)(expected & NEXT_MASK);
    //         if(state == DelFlag){
    //             first = (PredecessorNode<NotifDescType>*)(pall.helpRemove(&pall.head, next) & NEXT_MASK);
    //         }

    //         vector<PredecessorNode<NotifDescType>*> qPrime;
    //         first = (PredecessorNode<NotifDescType>*)(pall.head.succ.load() & NEXT_MASK);
    //         pNode = (PredecessorNode<NotifDescType>*)first;
    //         //Traverse pall from start to end, add any nodes not in Q to qPrime
    //         while(pNode && qSet.count(pNode) == 0){
    //             qPrime.push_back(pNode);
    //             //Note the cast to a ListNode is important!
    //             pNode = (PredecessorNode<NotifDescType>*)pall.next(pNode);
    //         }

    //         //Go through qPrime in reverse order, and put each element at the front of q.
    //         //This way q is sorted from the most recently inserted PNode to the least recently inserted PNode.
    //         for(auto iter = qPrime.rbegin(); iter != qPrime.rend(); ++iter){
    //             PredecessorNode<NotifDescType> *p = *iter;
                
    //             q.push_front(p);
    //             qSet.insert(p);
    //         }
    //     }
    // }

    //Traverse the reverse update announcement linked list.
    void traverseRUALL(PredecessorNode<NotifDescType> *pNode, set<InsNode<NotifDescType> *> &I, set<DelNode<NotifDescType>*> &D){
        UpdateNode *uNode = (UpdateNode*)ruall.first(pNode); //Atomically set pNode.notifyThreshold....
        while(uNode){
            if(uNode->key < pNode->key){
                if(uNode->status != INACTIVE && firstActivated(uNode)){
                    if(uNode->type == INS) I.insert((InsNode<NotifDescType>*)uNode);
                    else D.insert((DelNode<NotifDescType>*)uNode);
                }
            }
            uNode = (UpdateNode*)ruall.next(pNode,uNode); //Atomically set pNode.notifyThreshold....
        }
    }

    int64_t predHelper(PredecessorNode<NotifDescType> *pNode){
        int64_t y = pNode->key;
        int64_t max_I_Notify = -1;
        int64_t max_D_notify = -1;
        int64_t max_I_uall; //Maximum key of an active INS node encountered while traversing an 
        //Let D_uall be the set of all active DelNodes with key < yencountered while traversing UALL
        //max_D_uall is the maximum key of such a DelNode that is not in D_ruall.
        int64_t max_D_uall; 
        //vector<DelNode<NotifDescType>*> D_notify;
        set<InsNode<NotifDescType>*> I_ruall;
        set<DelNode<NotifDescType>*> D_ruall;


        //Insert pNode into the PALL
        pall.insert(pNode);
        
        //Traverse the pall from the PredecessorNode following pNode to the end, inserting every node encountered into Q 
        vector<PredecessorNode<NotifDescType>*> Q;
        PredecessorNode<NotifDescType> *p = pall.next(pNode);
        while(p){
            Q.push_back(p);
            p = pall.next(p);
        }

        traverseRUALL(pNode,I_ruall, D_ruall);
        int64_t r0 = relaxedPredecessor(y);
        traverseUALL(y, max_I_uall, max_D_uall, D_ruall);

        //Traverse pNode's notify list...
        NotifyNode<NotifDescType> *nNode = pNode->notifyListHead;
        while(nNode){
            if(nNode->key < y){ //For every NotifyNode in pNode's notifyList with key < y....
                if(nNode->updateNode->type == INS){
                    if(nNode->notifyThreshold <= nNode->key && nNode->key > max_I_Notify){
                        max_I_Notify = nNode->key;
                        //I_notify.push_back((InsNode<NotifDescType>*)nNode->updateNode);
                    }
                }
                else if(nNode->key > nNode->notifyThreshold){
                    if(nNode->key > max_D_notify && (D_ruall.find((DelNode<NotifDescType>*)nNode->updateNode) != D_ruall.end())){
                        max_D_notify = nNode->key;
                    }
                    //D_notify.push_back((DelNode<NotifDescType>*)nNode->updateNode);
                }
                if((nNode->notifyThreshold == -1) && 
                    ((nNode->updateNode->type == INS && (I_ruall.find((InsNode<NotifDescType>*)nNode->updateNode)) == I_ruall.end())) && 
                    ((nNode->updateNode->type == DEL && (D_ruall.find((DelNode<NotifDescType>*)nNode->updateNode)) == D_ruall.end()))){
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
        // auto max_uall = I_uall.begin(); 
        // if(max_uall != I_uall.end()){
        //     InsNode<NotifDescType> *i = *max_uall;
        //     int64_t maxUALLKey = i->key;
        //     if(maxUALLKey > r1)r1 = maxUALLKey;
        // }

        //Let d be the DelNode of maximum key which is in D_uall and not in D_ruall, if one exists.
        //if d->key > r1, r1 = d->key
        // for(auto it = D_uall.begin(); it != D_uall.end();++it){
        //     DelNode<NotifDescType> *d = *it;
        //     if(d->key > r1 && D_ruall.find(d) == D_ruall.end()){
        //         r1 = d->key;
        //     }
        // }

        //Let d be the DelNode of maximum key which is in D_notify and not in D_ruall, if one exists.
        //if d->key > r1, r1 = d->key
        // for(auto it = D_notify.begin(); it != D_notify.end();++it){
        //     DelNode<NotifDescType> *d = *it;
        //     if(d->key > r1 && D_ruall.find(d) == D_ruall.end()){
        //         r1 = d->key;
        //     }
        // }

        //Traversal of the binary trie stopped while traversing back down.....
        if(r0 == -2 && !D_ruall.empty()){
            set<PredecessorNode<NotifDescType>*> predNodes; //Set containing the delPredNodes of DeleteNodes encountered in the RUALL.
            set<int64_t> R; //Set containing the delPreds of DeleteNodes encountered in the RUALL.
            for(DelNode<NotifDescType> *d : D_ruall){
                predNodes.insert(d->delPredNode);
                R.insert(d->delPred);
            }

            //pNodePrime is the predecessor node in predNodes that is the latest in Q.
            PredecessorNode<NotifDescType> *pNodePrime = nullptr;
            for (auto rit = Q.rbegin(); rit != Q.rend(); ++rit) { //Iterate through Q backwards.
                PredecessorNode<NotifDescType> *p = *rit;
                if(predNodes.find(p) != predNodes.end()){ //Stop as soon as such a predecessor node is found.
                    pNodePrime = p;
                    break;
                }
            }
            
            vector<UpdateNode*> L1;
            if(pNodePrime){
                //Insert all updateNodes of the notify nodes of pNodePrime to LPrime.
                NotifyNode<NotifDescType> *nNode = pNodePrime->notifyListHead;
                while(nNode){
                    if(nNode->key < y)L1.push_back(nNode->updateNode);
                    nNode = nNode->next;
                }
            }

            set<UpdateNode*> LPrime;
            vector<UpdateNode*> L2; //L2 is a stack. The top of the stack is at the end of the vector.
            //For every notifyNode in pNode's notifyList with key < y
            nNode = pNode->notifyListHead;
            while(nNode){
                if(nNode->key < y){
                    UpdateNode *uNode = nNode->updateNode;
                    LPrime.insert(uNode);
                    if(nNode->key <= nNode->notifyThreshold){
                        L2.push_back(uNode);
                    }
                }
                nNode = nNode->next;
            }

            //Go through L1 then L2 and do stuff to R
            //For elements of L1, skip elements that are in L Prime.
            for(auto rit = L1.rbegin(); rit != L1.rend(); ++rit){
                UpdateNode *uNode = *rit;
                if(LPrime.find(uNode) != LPrime.end())continue;
                if(uNode->type == INS)R.insert(uNode->key);
                else{  //If uNode->key in R, delete uNode->key and uNode->delPred2 from R
                    auto it = R.find(uNode->key);
                    if(it != R.end()){
                        R.erase(it);
                        R.erase(((DelNode<NotifDescType>*)uNode)->delPred2);
                    }
                }
            }
            for(auto rit = L2.rbegin(); rit != L2.rend(); ++rit){
                UpdateNode *uNode = *rit;
                if(uNode->type == INS)R.insert(uNode->key);
                else{  //If uNode->key in R, delete uNode->key and uNode->delPred2 from R
                    auto it = R.find(uNode->key);
                    if(it != R.end()){
                        R.erase(it);
                        R.erase(((DelNode<NotifDescType>*)uNode)->delPred2);
                    }
                }
            }
            //Set of keys in R, such that for every key x in goodKeys, 
            //there is an UpdateNode with key x in L'' and the last one in L'' has type INS.
            //set<int64_t> goodKeys;

            //For any key x, if x is in R and there is an UpdateNode with key x in D_ruall, remove x from R.
            for(UpdateNode *d : D_ruall){
                auto it = R.find(d->key);
                if(it != R.end())R.erase(it);
            }

            //Remove every key x from R such that the last UpdateNode with key x in L'' is an UpdateNode of type DEL.
            //Go through LDouble prime in reverse...
            // for(auto it = L.begin(); it != L.end(); ++it){
            //     UpdateNode *uNode = *it;
            //     int64_t key = uNode->key;
            //     //If uNode->key is not in goodKeys and uNode->key is in R, then remove uNode->key from R.
            //     if(goodKeys.count(key) == 0 && R.count(key) > 0){
            //         if(uNode->type == DEL){
            //             //If uNode is a delete node, then remove key from R as the furthest node in L'' with key uNode->key has type DEL
            //             R.erase(key); 
            //         }
            //         else{
            //             //Otherwise add uNode->key to goodKeys, since uNode is the furthest node with key uNode->key in L'' and it has type INS
            //             goodKeys.insert(key);
            //         }
            //     }
            // }

            //r0 = maximum value -1 and among keys in R
            r0 = -1;
            auto maxIter = R.rbegin();
            if(maxIter != R.rend())r0 = *maxIter;
            // for(UpdateNode* i : I_ruall){
            //     if(i->key > pred0)pred0 = i->key;
            // }

        }

        //Return the max among r0 and r1.
        if(r0 > r1)return r0;
        else return r1;
    }
    int64_t predecessor(int64_t y){
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);
        
        PredecessorNode<NotifDescType> *p = new PredecessorNode<NotifDescType>(y, &ruall.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, y);
        int64_t pred = predHelper(p);
        pall.remove(p);
        trieDebra.reclaimLater(p);
        //trieRecordManager.reclaimLater(threadID, p); //PredNode p can be retired, since it is no longer in shared memory.

        trieDebra.endOp();
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
            uint64_t numNodes = (1 << depth);
            for(uint64_t n = 0; n < numNodes; ++n){
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

    //Verify that all of the lists are empty
    void verifyLists(){
        assert(uall.head.succ == (uintptr_t)&uall.tail);
        assert(ruall.head.rSucc == (uintptr_t)&ruall.tail);
        assert(pall.head.succ == (uintptr_t)&pall.tail);
    }
};
