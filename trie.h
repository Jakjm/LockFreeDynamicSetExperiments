#include <climits>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include "LinkedLists/P_ALL.h"
#include "LinkedLists/RU_ALL_notifDesc.h"
#include "LinkedLists/RU_ALL_swCopy.h"
#include "LinkedLists/UALL.h"
#include "trieNodeTypes.h"
#include "BoundedMinReg/minreg.h"
#include "DynamicSet.h"

#include <thread>
#include <string>
#include <ostream>
#include <set>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include "common.h"


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
    volatile char padding[64 - 3*sizeof(InsNode<NotifDescType>*)];
    NodePool() : insNode(nullptr), delNode(nullptr), notifNode(nullptr){

    }
    ~NodePool(){
        delete insNode;
        delete delNode;
        delete notifNode;
    }
};

#define reuse 1 //If reuse is defined, update nodes that are not inserted into the trie will be reused.
#define seqSet unordered_set

template <typename NotifDescType = NotifDescNotifyThreshold>
class Trie : public DynamicSet{
    private:
    const int trieHeight; //The height of the trie.
    const int64_t universeSize; //Equal to 2^trieHeight
    TrieNode<NotifDescType> * const trieNodes;
    std::atomic<UpdateNode*> * const latest;
    P_ALL_TYPE<NotifDescType> P_ALL;
    UALL_Type U_ALL;
    RU_ALL_TYPE<NotifDescType> RU_ALL;
    NodePool<NotifDescType> nodePool[MAX_THREADS];
    public:
    Trie(int height) : trieHeight(height), universeSize(1 << trieHeight), trieNodes(new TrieNode<NotifDescType>[universeSize - 1]), 
    latest(new std::atomic<UpdateNode*>[universeSize]), P_ALL(), U_ALL(), RU_ALL()
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
            DelNode<NotifDescType> *initialDelNode = new DelNode<NotifDescType>(key, trieHeight);
            //trieRecordManager.allocate<DelNode>(threadID, key, trieHeight);
            initialDelNode->upper0Boundary = trieHeight; // The initial delNodes for the trie have upper0Boundary = trieHeight.
            latest[key] = initialDelNode;
            initialDelNode->status = ACTIVE;
            initialDelNode->dNodeCount = 1;
            

            //Initialize the dNodePointer of trieNodes...
            //Increment the dNodeCount accordingly.
            int k = key;
            int depth = trieHeight - 1;
            while(depth >= 0 && (k & 1) == 0){
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
    bool search(int64_t x){
        //trieRecordManager.startOp(threadID);
        trieDebra.startOp();
        UpdateNode *l = findLatest(x);

        //trieRecordManager.endOp(threadID);
        trieDebra.endOp();
        return l->type == INS; //Return whether the root of the latest was an insert node.
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
            U_ALL.insert(uNode);
            RU_ALL.insert(uNode);

            //STATUS expectedStatus = INACTIVE;
            //uNode->status.compare_exchange_strong(expectedStatus, ACTIVE);
            uNode->status = ACTIVE;

            UpdateNode *latestNext = uNode->latestNext;
            if(latestNext){
                latestNext->status = STALE;
                
                //Set uNode->latestNext to nullptr
                UpdateNode *result = uNode->latestNext.exchange(nullptr); 
                if(result == latestNext){ //This operation unlinked latestNext from the latest list.
                    if(latestNext->type == INS){
                        trieDebra.retire((InsNode<NotifDescType>*)latestNext);
                        //trieRecordManager.retire(threadID, (InsNode*)latestNext); //Retire latestNext if it's an insert node.
                    }
                    else{
                        //Otherwise, decrement latestNext's dNodeCount, and retire latestNext if dNodeCount was reduced to 0.
                        int retire = ((DelNode<NotifDescType>*)latestNext)->dNodeCount.fetch_add(-1); 
                        if(retire == 1){
                            trieDebra.retire((DelNode<NotifDescType>*)latestNext);
                            //trieRecordManager.retire(threadID, (DelNode*)latestNext);
                        }
                    }
                }
            }
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

        //Traverse through the Update Announcement Linked List
    void traverseUALL(int64_t x, vector<InsNode<NotifDescType>*> &I, vector<DelNode<NotifDescType>*> &D){
        UpdateNode *uNode = (UpdateNode*)U_ALL.first();   
        while(uNode && uNode->key < x){
            if(uNode->status != INACTIVE && firstActivated(uNode)){
                if(uNode->type == INS)I.push_back((InsNode<NotifDescType>*)uNode);
                else D.push_back((DelNode<NotifDescType>*)uNode);
            }
            uNode = (UpdateNode*)U_ALL.next(uNode);
        }                                                                                             
    }

    //Send notifications to predecessor operations.
    void notifyPredOps(UpdateNode * const uNode){
        vector<InsNode<NotifDescType>*> I; 
        vector<DelNode<NotifDescType>*> D;
        traverseUALL(INT64_MAX, I, D);

        PredecessorNode<NotifDescType> *pNode = (PredecessorNode<NotifDescType>*)P_ALL.first();
        while(pNode){
            UpdateNode *notifyThres = pNode->notifyThreshold.read();
            int64_t tau = notifyThres->key;

            if(!firstActivated(uNode)){
                return;
            }

            NotifyNode<NotifDescType> *nNode = nodePool[threadID].notifNode;

            nNode->key = uNode->key;
            nNode->updateNode = uNode;
            nNode->notifyThreshold = tau;
            int64_t maxKey = -1;
            InsNode<NotifDescType> *updateNodeMax = nullptr;
            for(InsNode<NotifDescType>* insNode : I){
                if(insNode->key < pNode->key && insNode->key > maxKey){
                    maxKey = insNode->key;
                    updateNodeMax = insNode;
                }
            }
            nNode->updateNodeMax = updateNodeMax;
            if(!sendNotification(nNode, pNode)){
                break;
            }
            nodePool[threadID].notifNode = new NotifyNode<NotifDescType>();

            pNode = (PredecessorNode<NotifDescType>*)P_ALL.next(pNode);
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
            
            if(dNode->stop || dNode->lower1Boundary.minRead() != trieHeight+1)return;
            if(firstActivated(dNode) == false)return;
            
            DelNode<NotifDescType> *expected = d;
            dNode->dNodeCount.fetch_add(1); 
            //assert(count > 0);
            t->dNodePtr.compare_exchange_strong(expected, dNode);
            if(expected != d){
                d = t->dNodePtr;

                if(dNode->stop || (dNode->lower1Boundary.minRead() != trieHeight+1 || firstActivated(dNode) == false)){
                    dNode->dNodeCount.fetch_add(-1);
                    //assert(count > 1);
                    return;
                }
                expected = d;
                t->dNodePtr.compare_exchange_strong(expected, dNode);
                if(expected != d){
                    dNode->dNodeCount.fetch_add(-1);
                    //assert(count > 1);
                    return;
                }
            }

            //dNode has successfully taken d's place as t->dNodePtr
            //Retire d if it is no longer in shared memory.
            int count = d->dNodeCount.fetch_add(-1);
            if(count == 1){
                trieDebra.retire(d);
            }
                //trieRecordManager.retire(threadID, d);

            //Increment dNodeCount of dNode
            if(interpretedBit(key * 2, depth + 1) || interpretedBit(key * 2 + 1, depth + 1))return;
            dNode->upper0Boundary = (trieHeight - depth);
        }
    }

    /**
    Insert operation on the binary trie.
    */
    void insert(int64_t x){
        //assert(x >= 0 && x <= universeSize);
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);
        UpdateNode *dNode = findLatest(x), *expected;
        if (dNode->type == INS){
            trieDebra.endOp();
            //trieRecordManager.endOp(threadID); //x already in S, nothing to do!
            return;
        } 
        //dNode has type DEL and its child, if it has one, is of type INS

        #ifdef reuse 
        InsNode<NotifDescType> *iNode = nodePool[threadID].insNode;
        #else
        InsNode *iNode = trieRecordManager.allocate<InsNode>(threadID); //Allocate a new node for each insert
        #endif
        iNode->key = x;
        iNode->latestNext = dNode;
        UpdateNode *latestNext = dNode->latestNext;
        if(latestNext){
            //latestNext->status = STALE;
            //dNode->latestNext = nullptr
            UpdateNode *result = dNode->latestNext.exchange(nullptr); 
            //If latestNext was removed by this exchange operation....
            if(result == latestNext){
                //assert(latestNext->type == INS);
                trieDebra.retire((InsNode<NotifDescType>*)latestNext);
                //trieRecordManager.retire(threadID, (InsNode*)latestNext); //Retire the InsertNode following dNode in latest list.
            }
        }
        expected = dNode;
        latest[x].compare_exchange_strong(expected, iNode);
        if (expected != dNode){
            helpActivate(expected);
            #ifdef reuse //This insert node can be reused by subsequent insert operations...
            #else
            trieRecordManager.deallocate(threadID, iNode);
            #endif
            //trieRecordManager.endOp(threadID);
            trieDebra.endOp();
            return;
        }
        #ifdef reuse 
        nodePool[threadID].insNode = new InsNode<NotifDescType>(); //Ensure that iNode has been removed from the pool...
        #endif 
        U_ALL.insert(iNode);
        RU_ALL.insert(iNode);
        iNode->status = ACTIVE;
        //iNode->status.compare_exchange_strong( expectedStatus, ACTIVE);
        //dNode->status = STALE;
        
        //Instead of iNode->latestNext = nullptr, use swap to determine if this swap removes 
        UpdateNode *result = iNode->latestNext.exchange(nullptr); 
        if(result == dNode){ //If this exchange removed dNode from the latest list....
            //Retire the delete node if it is no longer in the latest list or stored as a dNodePtr
            //assert(dNode->type == DEL);
            int retire = ((DelNode<NotifDescType>*)dNode)->dNodeCount.fetch_add(-1);
            if(retire == 1)trieDebra.retire((DelNode<NotifDescType>*)dNode);
            //trieRecordManager.retire(threadID, (DelNode*)dNode);
        }
        
        insertBinaryTrie(iNode);
        notifyPredOps(iNode); //Notify predecessor operations that this insertion is in progress...

        //for each node uNode in U-ALL with uNode.key = x before and including v do
            //uNode.status = STALE

        U_ALL.remove(iNode);
        RU_ALL.remove(iNode);
        trieDebra.endOp();
        //trieRecordManager.endOp(threadID);
    }
    

    void remove(int64_t x){
        //assert(x >= 0 && x <= universeSize);
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);

        UpdateNode *iNode = findLatest(x), *expected;
        if(iNode->type == DEL){
            trieDebra.endOp();
            //trieRecordManager.endOp(threadID);
            return; //x is not in S, nothing to do!
        }
        //iNode has type INS. If it has a child, its child has type DEL.

        PredecessorNode<NotifDescType> *pNode = new PredecessorNode<NotifDescType>(x, &RU_ALL.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred = predHelper(pNode);

        //Initialize update node for this delete operation.
        #ifdef reuse 
        DelNode<NotifDescType> *dNode;
        dNode = nodePool[threadID].delNode;
        #else 
        DelNode *dNode = trieRecordManager.allocate<DelNode>(threadID, trieHeight); //Allocate a delNode for each delete...
        #endif
        dNode->key = x;

        dNode->delPredNode = pNode;
        dNode->delPred = delPred;
        dNode->latestNext = iNode;
        //assert(dNode->dNodeCount == 2);
    
        
        UpdateNode *latestNext = iNode->latestNext;
        if(latestNext){
            //latestNext->status = STALE;

            UpdateNode *result = iNode->latestNext.exchange(nullptr); 
            //Swap iNode's latestNext with nullptr.
            if(result == latestNext){ //If this operation removed latestNext, decrement its dNodeCount.
                //assert(latestNext->type == DEL);
                int retire = ((DelNode<NotifDescType>*)latestNext)->dNodeCount.fetch_add(-1);
                if(retire == 1)trieDebra.retire((DelNode<NotifDescType>*)latestNext);
                //trieRecordManager.retire(threadID, (DelNode*)latestNext); //Retire if dNodeCount was lowered to 0.
            }   
        }
        notifyPredOps(iNode);
        expected = iNode;
        latest[x].compare_exchange_strong(expected, dNode);

        if(expected != iNode){ //Failed to CAS dNode to head of latest list....
            //There was a different node, expected, instead of iNode at the head of the latest list prior to our CAS.
            //We will help activate it.
            helpActivate(expected);

            //Remove pNode from P_ALL.
            P_ALL.remove(pNode);

            #ifdef reuse
            #else 
            trieRecordManager.deallocate(threadID, dNode);
            #endif
            //Retire pNode as it is no longer in shared memory.
            trieDebra.retire(pNode);
            //trieRecordManager.retire(threadID, pNode);
            trieDebra.endOp();
            //trieRecordManager.endOp(threadID);
            return;
        }
        #ifdef reuse
        nodePool[threadID].delNode = new DelNode<NotifDescType>(trieHeight); //Remove dNode from the pool; it should not be reused for the next deletion
        #endif
        U_ALL.insert(dNode);
        RU_ALL.insert(dNode);

        dNode->status = ACTIVE;

        DelNode<NotifDescType> *target = ((InsNode<NotifDescType>*)iNode)->target;
        if(target && firstActivated(target))target->stop = true;

        //Swap dNode's latestNext with nullptr.
        UpdateNode *result = dNode->latestNext.exchange(nullptr);
        if(result == iNode){  
            //If this CAS unlinked iNode from the latest list, retire iNode.
            //assert(iNode->type == INS);
            trieDebra.retire((InsNode<NotifDescType>*)iNode);
            //trieRecordManager.retire(threadID, (InsNode*)iNode);
        }
        PredecessorNode<NotifDescType> *pNode2 = new PredecessorNode<NotifDescType>(x, &RU_ALL.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred2 = predHelper(pNode2);
        dNode->delPred2 = delPred2;

        //assert(dNode->dNodeCount > 0);
        deleteBinaryTrie(dNode);
        notifyPredOps(dNode);

        // Delete pNode and pNode2 from P-ALL.
        P_ALL.remove(pNode);
        P_ALL.remove(pNode2);

        //Retire both pNode and pNode2.
        trieDebra.retire(pNode);
        trieDebra.retire(pNode2);
        //trieRecordManager.retire(threadID, pNode);
        //trieRecordManager.retire(threadID, pNode2);

        U_ALL.remove(dNode);
        RU_ALL.remove(dNode);

        int retire = dNode->dNodeCount.fetch_add(-1);
        if(retire == 1)trieDebra.retire(dNode);
        //trieRecordManager.retire(threadID, dNode);
        trieDebra.endOp();
        //trieRecordManager.endOp(threadID);
    }

    //Computes the relaxed predecessor of key y.
    //This is done by traversing up and then down the binary trie.
    int64_t relaxedPredecessor(int64_t y){
        //Let t be the latestList for key y.
        int depth = trieHeight; //depth is the depth of t.
        
        //While t is the leftChild of t.parent or t.parent.left's interpreted bit equals 0
        while((y & 1) == 0 || interpretedBit(y - 1, depth) == 0){
            //t = t.parent
            y = y >> 1; 
            --depth; 
            if(depth == 0)return -1; //If t is the root node, no predecessors of y were found
            
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

    //     PredecessorNode<NotifDescType> *first = (PredecessorNode<NotifDescType>*)(P_ALL.head.succ.load() & NEXT_MASK);
    //     PredecessorNode<NotifDescType> *pNode = (PredecessorNode<NotifDescType>*)first;
    //     //Traverse P_ALL from start to end
    //     while(pNode){
    //         q.push_back(pNode);
    //         qSet.insert(pNode);
    //         pNode = (PredecessorNode<NotifDescType>*)P_ALL.next(pNode);
    //     }

    //     //Insert newNode into P_ALL
    //     while(1){
    //         //Set newNode's next to first.
    //         newNode->succ = (uintptr_t)first;
    //         uintptr_t expected = (uintptr_t)first;

    //         P_ALL.head.succ.compare_exchange_strong(expected, (uintptr_t)newNode);
    //         if(expected == (uintptr_t)first){
    //             q.push_front(newNode); //Put newNode at the front of q.
    //             return; //newNode was successfully inserted
    //         }
    //         int64_t state = (int64_t)(expected & STATUS_MASK);
    //         PredecessorNode<NotifDescType> *next = (PredecessorNode<NotifDescType>*)(expected & NEXT_MASK);
    //         if(state == DelFlag){
    //             first = (PredecessorNode<NotifDescType>*)(P_ALL.helpRemove(&P_ALL.head, next) & NEXT_MASK);
    //         }

    //         vector<PredecessorNode<NotifDescType>*> qPrime;
    //         first = (PredecessorNode<NotifDescType>*)(P_ALL.head.succ.load() & NEXT_MASK);
    //         pNode = (PredecessorNode<NotifDescType>*)first;
    //         //Traverse P_ALL from start to end, add any nodes not in Q to qPrime
    //         while(pNode && qSet.count(pNode) == 0){
    //             qPrime.push_back(pNode);
    //             //Note the cast to a ListNode is important!
    //             pNode = (PredecessorNode<NotifDescType>*)P_ALL.next(pNode);
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
    void traverseRUALL(PredecessorNode<NotifDescType> *pNode, seqSet<InsNode<NotifDescType> *> &I, seqSet<DelNode<NotifDescType>*> &D){
        UpdateNode *uNode = (UpdateNode*)RU_ALL.first(pNode); //Atomically set pNode.notifyThreshold....
        while(uNode){
            if(uNode->key < pNode->key){
                if(uNode->status != INACTIVE && firstActivated(uNode)){
                    if(uNode->type == INS) I.insert((InsNode<NotifDescType>*)uNode);
                    else D.insert((DelNode<NotifDescType>*)uNode);
                }
            }
            uNode = (UpdateNode*)RU_ALL.next(pNode,uNode); //Atomically set pNode.notifyThreshold....
        }
        //assert(pNode->notifyThreshold == &ZERO_THRES);
    }

    int64_t predHelper(PredecessorNode<NotifDescType> *pNode){
        vector<PredecessorNode<NotifDescType>*> Q;
        vector<InsNode<NotifDescType>*> I_uall, I_notify;
        vector<DelNode<NotifDescType>*> D_uall, D_notify;
        seqSet<InsNode<NotifDescType>*> I_ruall;
        seqSet<DelNode<NotifDescType>*> D_ruall;
        int64_t y = pNode->key;

        //traverseAndInsertPALL(pNode, Q);
        //Insert pNode into the PALL
        P_ALL.insert(pNode);

        //Traverse the P_ALL from pNode to the end, 
        PredecessorNode<NotifDescType> *p = pNode;
        do{
            Q.push_back(p);
            p = P_ALL.next(p);
        }while(p);

        traverseRUALL(pNode,I_ruall, D_ruall);
        int64_t pred0 = relaxedPredecessor(y);
        traverseUALL(y, I_uall, D_uall);

        //Traverse pNode's notify list...
        NotifyNode<NotifDescType> *nNode = pNode->notifyListHead;
        while(nNode){
            if(nNode->key < y){ //For every NotifyNode in pNode's notifyList with key < y....
                if(nNode->updateNode->type == INS){
                    if(nNode->key >= nNode->notifyThreshold)I_notify.push_back((InsNode<NotifDescType>*)nNode->updateNode);
                }
                else{
                    if(nNode->key > nNode->notifyThreshold)D_notify.push_back((DelNode<NotifDescType>*)nNode->updateNode);
                }
                if((nNode->notifyThreshold == -1) && 
                    ((nNode->updateNode->type == INS && I_ruall.count((InsNode<NotifDescType>*)nNode->updateNode)) == 0) && 
                    ((nNode->updateNode->type == DEL && D_ruall.count((DelNode<NotifDescType>*)nNode->updateNode)) == 0)){
                    if(nNode->updateNodeMax)I_notify.push_back(nNode->updateNodeMax);
                }
            }
            nNode = nNode->next;
        }
        //p1 = key of UpdateNode with largest key among those in the union of I_uall, I_notify, (D_uall - D_ruall) and (D_notify - D_ruall).
        int64_t p1 = -1;
        for(InsNode<NotifDescType> *i : I_uall){
            if(i->key > p1)p1 = i->key;
        }
        for(InsNode<NotifDescType> *i : I_notify){
            if(i->key > p1)p1 = i->key;
        }
        for(DelNode<NotifDescType> *d : D_uall){
            if(D_ruall.count(d) == 0 && d->key > p1)p1 = d->key;
        }
        for(DelNode<NotifDescType> *d : D_notify){
            if(D_ruall.count(d) == 0 && d->key > p1)p1 = d->key;
        }

        //Traversal of the binary trie stopped while traversing back down.....
        if(pred0 == -2 && D_ruall.size() > 0){
            seqSet<PredecessorNode<NotifDescType>*> predNodes;
            predNodes.insert(pNode);
            for(DelNode<NotifDescType> *d : D_ruall){
                predNodes.insert(d->delPredNode);
            }

            //pNodePrime is the predecessor node in predNodes that is the latest in Q.
            PredecessorNode<NotifDescType> *pNodePrime = nullptr;
            for (auto rit = Q.rbegin(); rit != Q.rend(); ++rit) { //Iterate through Q backwards.
                PredecessorNode<NotifDescType> *p = *rit;
                if(predNodes.count(p) > 0){ //Stop as soon as such a predecessor node is found.
                    pNodePrime = p;
                    break;
                }
            }

            vector<UpdateNode*> LPrime;
            //Insert all updateNodes of the notify nodes of pNodePrime to LPrime.
            NotifyNode<NotifDescType> *nNode = pNodePrime->notifyListHead;
            while(nNode){
                LPrime.push_back(nNode->updateNode);
                nNode = nNode->next;
            }

            seqSet<UpdateNode*> L;
            vector<UpdateNode*> LPrime2; //LPrime2 is a stack. The top of the stack is at the end of the vector.
            //For every notifyNode in pNode's notifyList with key < y
            nNode = pNode->notifyListHead;
            while(nNode){
                if(nNode->key < y){
                    UpdateNode *uNode = nNode->updateNode;
                    if(nNode->key <= nNode->notifyThreshold){
                        LPrime2.push_back(uNode);
                    }
                    else{
                        L.insert(uNode);
                    }
                }
                nNode = nNode->next;
            }
            //Insert UpdateNodes in LPrime into LPrime2.
            //LPrime2 now corresponds to L' following the line L' = L' + L'2 in the pseudocode
            for (auto it = LPrime.rbegin(); it != LPrime.rend(); ++it) {
                UpdateNode *uNode = *it;
                LPrime2.push_back(uNode);
            }
            //Traverse LPrime2 in reverse order.....
            //Insert the nodes that are in LPrime2 but not L into LDoublePrime.
            vector<UpdateNode*> LDoublePrime; 
            for (auto rit = LPrime2.rbegin(); rit != LPrime2.rend(); ++rit) {
                UpdateNode *uNode = *rit;
                if(L.count(uNode) == 0)LDoublePrime.push_back(uNode);
            }
            seqSet<int64_t> R; //Set containing the delNodePreds of DeleteNodes encountered in the RUALL.
            for(DelNode<NotifDescType> *dNode : D_ruall){
                R.insert(dNode->delPred); //Insert the embedded predecessor value of the delNodes in D_0
            }
            for(UpdateNode *uNode : LDoublePrime){
                if(uNode->type == INS){
                    R.insert(uNode->key);
                }
                else{
                    int64_t delPred2 = ((DelNode<NotifDescType>*)uNode)->delPred2;
                    R.insert(delPred2);
                }
            }
            //Set of keys in R, such that for every key x in goodKeys, 
            //there is an UpdateNode with key x in L'' and the last one in L'' has type INS.
            seqSet<int64_t> goodKeys;

            //Remove every key x from R such that the last UpdateNode with key x in L'' is an UpdateNode of type DEL.
            //Go through LDouble prime in reverse...
            for(auto it = LDoublePrime.begin(); it != LDoublePrime.end(); ++it){
                UpdateNode *uNode = *it;
                int64_t key = uNode->key;
                //If uNode->key is not in goodKeys and uNode->key is in R, then remove uNode->key from R.
                if(goodKeys.count(key) == 0 && R.count(key) > 0){
                    if(uNode->type == DEL){
                        //If uNode is a delete node, then remove key from R as the furthest node in L'' with key uNode->key has type DEL
                        R.erase(key); 
                    }
                    else{
                        //Otherwise add uNode->key to goodKeys, since uNode is the furthest node with key uNode->key in L'' and it has type INS
                        goodKeys.insert(key);
                    }
                }
            }

            //For any key x, if x is in R and there is an UpdateNode with key x in D_ruall, remove x from R.
            for(UpdateNode *d : D_ruall){
                if(R.count(d->key) > 0)R.erase(d->key);
            }

            //Pred0 = maximum value among keys in R
            pred0 = -1;
            for(UpdateNode* i : I_ruall){
                if(i->key > pred0)pred0 = i->key;
            }
            for(int64_t v : R){
                if(v > pred0)pred0 = v;
            }
        }

        //Return the max among pred0 and p1.
        if(pred0 > p1)return pred0;
        else return p1;
    }
    int64_t predecessor(int64_t y){
        //assert(y >= 0 && y <= universeSize);
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);
        
        PredecessorNode<NotifDescType> *p = new PredecessorNode<NotifDescType>(y, &RU_ALL.head);
        //trieRecordManager.allocate<PredecessorNode>(threadID, y);
        int64_t pred = predHelper(p);
        P_ALL.remove(p);
        trieDebra.retire(p);
        //trieRecordManager.retire(threadID, p); //PredNode p can be retired, since it is no longer in shared memory.

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
        assert(RU_ALL.head.rSucc == (uintptr_t)&RU_ALL.tail);
        assert(U_ALL.head.succ == (uintptr_t)&U_ALL.tail);
        assert(P_ALL.head.succ == (uintptr_t)&P_ALL.tail);
    }

    //TODO ifdef debug
    void printOpLog(){

    }
};
