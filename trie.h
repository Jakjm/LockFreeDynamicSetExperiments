#include <climits>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include "LinkedLists/P_ALL.h"
#include "LinkedLists/RU_ALL.h"
#include "LinkedLists/UALL.h"
#include "trieNodeTypes.h"
#include "BoundedMinReg/minreg.h"
#include "DynamicSet.h"

#include <deque>
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
using std::deque;




//Structure used to store pointers to insert/delete nodes that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated insert/delete node can be used again.
struct UpdateNodePool{
    InsNode *insNode;
    DelNode *delNode;
    volatile char padding[64 - 2*sizeof(InsNode*)];
    UpdateNodePool() : insNode(nullptr), delNode(nullptr){

    }
};
UpdateNodePool updateNodePool[NUM_THREADS];
#define reuse 1 //If reuse is defined, update nodes that are not inserted into the trie will be reused.

template <int trieHeight>
class Trie : public DynamicSet{
    private:
    //const int trieHeight; //The height of the trie.
    const int64_t universeSize; //Equal to 2^b 
    TrieNode *trieNodes[trieHeight];
    LatestList *latest;
    P_ALL_TYPE P_ALL;
    UALL_Type U_ALL;
    RU_ALL_TYPE RU_ALL;
    public:
    Trie() : universeSize(1 << trieHeight), latest(new LatestList[universeSize]), P_ALL(), U_ALL(), RU_ALL()
    {
        //Initialize the binary trie nodes for each level of the trie.
        for(int i = 0; i < trieHeight;++i){
            int64_t rowSize = (1 << i); //Row size = 2^i
            TrieNode *trieNodeRow = new TrieNode[rowSize];
            trieNodes[i] = trieNodeRow;
        }

        TrieNode *baseRow = trieNodes[trieHeight - 1];

        //Initialize the latest lists for each key in the universe.
        //Initialize row b of binary trie nodes.
        //Initially, each latest list's head = a DelNode with
        for(int64_t key = 0; key < universeSize;++key){
            DelNode *initialDelNode = new DelNode(key, trieHeight);
            //trieRecordManager.allocate<DelNode>(threadID, key, trieHeight);
            initialDelNode->upper0Boundary = trieHeight; // The initial delNodes for the trie have upper0Boundary = trieHeight.
            latest[key].head = initialDelNode;
            initialDelNode->status = ACTIVE;
            if((key & 1) == 0){ //If the key is even, 
                baseRow[key / 2].dNodePtr = initialDelNode;
                initialDelNode->dNodeCount = 2;
            }
            else{
                initialDelNode->dNodeCount = 1;
            }
        }

        //Initialize rows 0 through trieHeight-2, such that every trieNode that is a parent of two trieNodes
        //Has its dNodePtr set to the dNodePtr of its left child.
        for(int row = trieHeight-2; row >= 0; row--){
            int64_t rowSize = (1 << row);
            TrieNode *trieNodeRow = trieNodes[row];
            TrieNode *childRow = trieNodes[row + 1];
            for(int64_t i = 0; i < rowSize;++i){
                int64_t leftChild = 2 * i;
                DelNode *dNode = childRow[leftChild].dNodePtr;
                trieNodeRow[i].dNodePtr = dNode;
                dNode->dNodeCount.fetch_add(1);
            }
        }

        verifyLists();
    }
    ~Trie(){
        //trieRecordManager.startOp(threadID);
        //Attempt to retire all of the UpdateNodes stored in the latest lists
        //And all of the dNodePtrs.
        for(int i = 0; i < trieHeight;++i){
            int numNodes = (1 << i);
            for(int node = 0;node < numNodes;++node){
                DelNode *dNode = trieNodes[i][node].dNodePtr;
                int retire = dNode->dNodeCount.fetch_add(-1);
                if(retire == 1){
                    delete dNode;
                    //trieRecordManager.deallocate(threadID, dNode);
                }
            }
            delete[] trieNodes[i]; //Delete every trie node row...
        }
        for(int l = 0; l < universeSize;++l){
            UpdateNode *uNode = latest[l].head;
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
        delete[] latest; //Delete vector used for the latest lists.
        verifyLists();
        //Retire update nodes that are still in pools...
        for(int i = 0;i < NUM_THREADS;++i){
            InsNode * volatile ins = updateNodePool[i].insNode;
            DelNode * volatile del = updateNodePool[i].delNode; 
            if(ins)delete ins;
            
            //trieRecordManager.deallocate(threadID, ins);
            if(del)delete del;
            //trieRecordManager.deallocate(threadID, del);
        }
        //trieRecordManager.endOp(threadID);

        //Dump all limbo bags of their contents.
        // for(int i = 0;i < NUM_THREADS;++i){
        //     trieRecordManager.deinitThread(i);
        // }
                
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
            DelNode *dNode = trieNodes[depth][index].dNodePtr;
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
            U_ALL.insert(uNode);
            RU_ALL.insert(uNode);

            STATUS expectedStatus = INACTIVE;
            uNode->status.compare_exchange_strong(expectedStatus, ACTIVE);

            UpdateNode *latestNext = uNode->latestNext;
            if(latestNext){
                latestNext->status = STALE;
                
                //Set uNode->latestNext to nullptr
                UpdateNode *result = uNode->latestNext.exchange(nullptr); 
                if(result == latestNext){ //This operation unlinked latestNext from the latest list.
                    if(latestNext->type == INS){
                        trieDebra.retire((InsNode*)latestNext);
                        //trieRecordManager.retire(threadID, (InsNode*)latestNext); //Retire latestNext if it's an insert node.
                    }
                    else{
                        //Otherwise, decrement latestNext's dNodeCount, and retire latestNext if dNodeCount was reduced to 0.
                        int retire = ((DelNode*)latestNext)->dNodeCount.fetch_add(-1); 
                        if(retire == 1){
                            trieDebra.retire((DelNode*)latestNext);
                            //trieRecordManager.retire(threadID, (DelNode*)latestNext);
                        }
                    }
                }
            }
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
            pNode->notifyListHead.compare_exchange_strong(expected,newNotifyNode); 
            if(expected == nNode){
                return true; //Succeeded in putting newNotifyNode at the head of the notifyList.
            }
        }
    }

        //Traverse through the Update Announcement Linked List
    void traverseUALL(int64_t x, vector<InsNode*> &I, vector<DelNode*> &D){
        UpdateNode *uNode = (UpdateNode*)U_ALL.first();   
        while(uNode && uNode->key <= x){
            if(uNode->status != INACTIVE && firstActivated(uNode)){
                if(uNode->type == INS)I.push_back((InsNode*)uNode);
                else D.push_back((DelNode*)uNode);
            }
            uNode = (UpdateNode*)U_ALL.next(uNode);
        }                                                                                             
    }

    //Send notifications to predecessor operations.
    void notifyPredOps(UpdateNode *uNode){
        vector<InsNode*> I; 
        vector <DelNode*> D;
        traverseUALL(INT64_MAX, I, D);

        PredecessorNode *pNode = (PredecessorNode*)P_ALL.first();
        while(pNode){
            int64_t tau;
            UpdateNode *notifyThres = pNode->notifyThreshold;
            tau = notifyThres->key;

            if(!firstActivated(uNode)){
                return;
            }
            int64_t maxKey = -1;
            InsNode *updateNodeMax = nullptr;
            for(InsNode* insNode : I){
                if(insNode->key < pNode->key && insNode->key > maxKey){
                    maxKey = insNode->key;
                    updateNodeMax = insNode;
                }
            }

            NotifyNode *newNotif = new NotifyNode(uNode, updateNodeMax, tau);
            if(!sendNotification(newNotif,pNode)){
                delete newNotif; //Deallocate newNotif as it was not used.
                break;
            }
            pNode = (PredecessorNode*)P_ALL.next(pNode);
        }
    }

    void insertBinaryTrie(InsNode *iNode){
        //For each binary trie node t on the path from the parent of the leaf with iNode.key to the root, do 
        int64_t key = iNode->key;
        for(int depth = trieHeight-1;depth >= 0;--depth){
            key = key >> 1;
            TrieNode &t = trieNodes[depth][key]; //Start from parent of 
            UpdateNode *dNodePtr = t.dNodePtr;
            UpdateNode *uNode = findLatest(dNodePtr->key);

            if (uNode->type == DEL){
                DelNode *delNode = (DelNode*)uNode;
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
    #define siblingIndex(index) (index + 1 - ((index & 1) * 2))

    void deleteBinaryTrie(DelNode *dNode){
        int depth = trieHeight;
        int64_t key = dNode->key;
        while(depth > 0){
            if(interpretedBit(key, depth) == 1 || interpretedBit(siblingIndex(key), depth) == 1)return;

            //t = t->parent.
            --depth;
            key = key / 2;
            TrieNode *t = &trieNodes[depth][key];

            DelNode *d = t->dNodePtr; //d is the oldValue of t->dNodePtr
            
            if(dNode->stop || dNode->lower1Boundary.minRead() != trieHeight+1)return;
            if(firstActivated(dNode) == false)return;
            
            DelNode *expected = d;
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
        InsNode *iNode;
        if(updateNodePool[threadID].insNode){ 
            iNode = updateNodePool[threadID].insNode; //Use the node that is currently in the pool
        }
        else{
            iNode = new InsNode();
            //trieRecordManager.allocate<InsNode>(threadID); //If there is no available node in the pool, allocate one.
        }
        #else
        InsNode *iNode = trieRecordManager.allocate<InsNode>(threadID); //Allocate a new node for each insert
        #endif
        iNode->key = x;

        iNode->latestNext = dNode;

        UpdateNode *latestNext = dNode->latestNext;
        if(latestNext){
            latestNext->status = STALE;
            //dNode->latestNext = nullptr
            UpdateNode *result = dNode->latestNext.exchange(nullptr); 
            if(result == latestNext){
                //assert(latestNext->type == INS);
                trieDebra.retire((InsNode*)latestNext);
                //trieRecordManager.retire(threadID, (InsNode*)latestNext); //Retire the InsertNode following dNode in latest list.
            }
        }
        expected = dNode;
        latest[x].head.compare_exchange_strong(expected, iNode);
        if (expected != dNode){
            helpActivate(expected);
            #ifdef reuse
            updateNodePool[threadID].insNode = iNode; //This insert node can be reused by subsequent insert operations...
            #else
            trieRecordManager.deallocate(threadID, iNode);
            #endif
            //trieRecordManager.endOp(threadID);
            trieDebra.endOp();
            return;
        }
        #ifdef reuse 
        updateNodePool[threadID].insNode = nullptr; //Ensure that iNode has been removed from the pool...
        #endif 
        U_ALL.insert(iNode);
        RU_ALL.insert(iNode);

        STATUS expectedStatus = INACTIVE;
        iNode->status.compare_exchange_strong( expectedStatus, ACTIVE);
        dNode->status = STALE;
        
        //iNode->latestNext = nullptr
        expected = dNode;
        UpdateNode *result = iNode->latestNext.exchange(nullptr); 
        if(result == dNode){
            //Retire the delete node if it is no longer in the latest list or stored as a dNodePtr
            //assert(dNode->type == DEL);
            int retire = ((DelNode*)dNode)->dNodeCount.fetch_add(-1);
            if(retire == 1)trieDebra.retire((DelNode*)dNode);
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

        PredecessorNode *pNode = new PredecessorNode(x);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred = predHelper(pNode);

        //Initialize update node for this delete operation.
        #ifdef reuse 
        DelNode *dNode;
        if(updateNodePool[threadID].delNode){
            dNode = updateNodePool[threadID].delNode; //Reuse a delete node from a previous operation that failed to insert delnode into latest list.
        }
        else{ //If there is no available node in the pool, allocate one.
            dNode = new DelNode(trieHeight);
            //trieRecordManager.allocate<DelNode>(threadID, trieHeight);
        }
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
            latestNext->status = STALE;

            UpdateNode *result = iNode->latestNext.exchange(nullptr); 
            //Swap iNode's latestNext with nullptr.
            if(result == latestNext){ //If this operation removed latestNext, decrement its dNodeCount.
                //assert(latestNext->type == DEL);
                int retire = ((DelNode*)latestNext)->dNodeCount.fetch_add(-1);
                if(retire == 1)trieDebra.retire((DelNode*)latestNext);
                //trieRecordManager.retire(threadID, (DelNode*)latestNext); //Retire if dNodeCount was lowered to 0.
            }   
        }
        notifyPredOps(iNode);
        expected = iNode;
        latest[x].head.compare_exchange_strong(expected, dNode);

        if(expected != iNode){ //Failed to CAS dNode to head of latest list....
            //There was a different node, expected, instead of iNode at the head of the latest list prior to our CAS.
            //We will help activate it.
            helpActivate(expected);

            //Remove pNode from P_ALL.
            P_ALL.remove((ListNode*)pNode);

            #ifdef reuse
            updateNodePool[threadID].delNode = dNode; //Place dNode into the pool to be reused for the next delete operation.
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
        updateNodePool[threadID].delNode = nullptr; //Remove dNode from the pool; it should not be reused for the next deletion
        #endif
        U_ALL.insert(dNode);
        RU_ALL.insert(dNode);

        STATUS expectedStatus = INACTIVE;
        dNode->status.compare_exchange_strong(expectedStatus, ACTIVE);
        iNode->status = STALE;

        DelNode *target = ((InsNode*)iNode)->target;
        if(target && firstActivated(target))target->stop = true;

        //Swap dNode's latestNext with nullptr.
        UpdateNode *result = dNode->latestNext.exchange(nullptr);
        if(result == iNode){  
            //If this CAS unlinked iNode from the latest list, retire iNode.
            //assert(iNode->type == INS);
            trieDebra.retire((InsNode*)iNode);
            //trieRecordManager.retire(threadID, (InsNode*)iNode);
        }
        PredecessorNode *pNode2 = new PredecessorNode(x);
        //trieRecordManager.allocate<PredecessorNode>(threadID, x);
        int64_t delPred2 = predHelper(pNode2);
        dNode->delPred2 = delPred2;

        //assert(dNode->dNodeCount > 0);
        deleteBinaryTrie(dNode);
        notifyPredOps(dNode);

        // Delete pNode and pNode2 from P-ALL.
        P_ALL.remove((ListNode*)pNode);
        P_ALL.remove((ListNode*)pNode2);

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

    //    0
    //   0 0
    // 0 0 0 0 

    //y = 3 
    //height = 2  
    int64_t traverseBinaryTrie(int64_t y, int64_t &depth){
        if(trieHeight <= 1)return -1;

        //Get interpreted bit
        //TrieNode *t = &trieNodes[trieHeight][y];

        int64_t parentIndex = y / 2;
        depth = trieHeight;
        char i1 = interpretedBit(parentIndex, depth - 1);
        char i2 = interpretedBit(siblingIndex(y), depth);
        
        //While i1 = 0, i2 == 0, or t is the leftChild of tParent
        while(i1 == 0 || (y % 2 == 0) || i2 == 0){
            y = y >> 1;
            --depth;
            if(depth == 0){
                depth = -1;
                return -1; //Interpreted bit of root node was 0 on previous iteration. Return <-1, depth>
            }

            //t = tParent;
            parentIndex = y / 2;
            
            i1 = interpretedBit(parentIndex,depth - 1);
            i2 = interpretedBit(siblingIndex(y), depth);
        }

        //Go to left child of parent. Subtract 1 from y if it is odd. 
        y = y - (y & 1);
    
        while(depth < trieHeight){
            //Right child is at 2*y + 1, left child is at 2 * y.
            int64_t rightIndex = y * 2 + 1;
            
            if(interpretedBit(rightIndex, depth+1) == 1){
                ++depth;
                y = rightIndex;
                continue;
            }
            int64_t leftIndex = y * 2;
            if(interpretedBit(leftIndex, depth+1) == 1){
                ++depth;
                y = leftIndex;
                continue;
            }
            
            //Interpreted bits of left and right nodes are 0. No predecessor found.
            return y; //depth < trieHeight. return <y, depth>
        }
        return y; //depth = trieHeight. return <y, trieHeight>
    }
    


    void traverseAndInsertPALL(PredecessorNode *newNode, deque<PredecessorNode*> &q){
        set<PredecessorNode*> qSet;

        ListNode *first = (ListNode*)(P_ALL.head.successor.load() & NEXT_MASK);
        PredecessorNode *pNode = (PredecessorNode*)first;
        //Traverse P_ALL from start to end
        while(pNode){
            q.push_back(pNode);
            qSet.insert(pNode);
            //Note the cast to a ListNode is important!
            pNode = (PredecessorNode*)P_ALL.next((ListNode*)pNode);
        }

        //Insert newNode into P_ALL
        while(1){
            //Set newNode's next to first.
            newNode->successor = (uintptr_t)first;
            uintptr_t expected = (uintptr_t)first;

            P_ALL.head.successor.compare_exchange_strong(expected, (uintptr_t)(ListNode*)newNode);
            if(expected == (uintptr_t)first){
                q.push_front(newNode); //Put newNode at the front of q.
                return; //newNode was successfully inserted
            }
            int64_t state = (int64_t)(expected & STATUS_MASK);
            ListNode *next = (ListNode*)(expected & NEXT_MASK);
            if(state == DelFlag){
                first = (ListNode*)(P_ALL.helpRemove(&P_ALL.head, next) & NEXT_MASK);
            }

            vector<PredecessorNode*> qPrime;
            first = (ListNode*)(P_ALL.head.successor.load() & NEXT_MASK);
            pNode = (PredecessorNode*)first;
            //Traverse P_ALL from start to end, add any nodes not in Q to qPrime
            while(pNode && qSet.count(pNode) == 0){
                qPrime.push_back(pNode);
                //Note the cast to a ListNode is important!
                pNode = (PredecessorNode*)P_ALL.next((ListNode*)pNode);
            }

            //Go through qPrime in reverse order, and put each element at the front of q.
            //This way q is sorted from the most recently inserted PNode to the least recently inserted PNode.
            for(vector<PredecessorNode*>::reverse_iterator iter = qPrime.rbegin(); iter != qPrime.rend(); ++iter){
                PredecessorNode *p = *iter;
                
                q.push_front(p);
                qSet.insert(p);
            }
        }
    }

    //Traverse the reverse update announcement linked list.
    void traverseRUALL(PredecessorNode *pNode, set<InsNode *> &I, set<DelNode*> &D){
        UpdateNode *uNode = (UpdateNode*)RU_ALL.first(pNode); //Atomically set pNode.notifyThreshold....
        while(uNode){
            if(uNode->key < pNode->key){
                if(uNode->status != INACTIVE && firstActivated(uNode)){
                    if(uNode->type == INS) I.insert((InsNode*)uNode);
                    else D.insert((DelNode*)uNode);
                }
            }
            uNode = (UpdateNode*)RU_ALL.next(pNode,uNode); //Atomically set pNode.notifyThreshold....
        }
        //assert(pNode->notifyThreshold == &ZERO_THRES);
    }

    int64_t predHelper(PredecessorNode *pNode){
        deque<PredecessorNode*> Q;
        vector<InsNode*> I_1, I_2;
        vector<DelNode*> D_1, D_2;
        set<InsNode*> I_0;
        set<DelNode*> D_0;
        int64_t y = pNode->key;
        int64_t depthT;
        

        traverseAndInsertPALL(pNode, Q);
        traverseRUALL(pNode,I_0, D_0);
        int64_t pred0 = traverseBinaryTrie(y, depthT);
        traverseUALL(y, I_1, D_1);

        //Traverse pNode's notify list...
        NotifyNode *nNode = pNode->notifyListHead;
        while(nNode){
            if(nNode->key < y){
                if(nNode->key > nNode->notifyThreshold){
                    if(nNode->updateNode->type == INS)I_2.push_back((InsNode*)nNode->updateNode);
                    else D_2.push_back((DelNode*)nNode->updateNode);

                    if(nNode->updateNodeMax)I_2.push_back(nNode->updateNodeMax);
                }
                else{
                    if(nNode->updateNode->type == INS)I_0.insert((InsNode*)nNode->updateNode);
                    else D_0.insert((DelNode*)nNode->updateNode);
                }
            }
            nNode = nNode->next;
        }
        
        int64_t k = -1;
        //k = key of updateNode with largest key among I_1, I_2, D_1 - D_0 and D_2 - D_0.
        for(InsNode *i : I_1){
            if(i->key > k)k = i->key;

        }
        for(InsNode *i : I_2){
            if(i->key > k)k = i->key;
        }

        //Create D1 - D0 and D2 - D0
        for(DelNode *d : D_1){
            if(D_0.count(d) == 0){
                if(d->key > k)k = d->key;
            }
        }
        for(DelNode *d : D_2){
            if(D_0.count(d) == 0){
                if(d->key > k)k = d->key;
            }
        }

        //Traversal of the binary trie stopped while traversing back down.....
        if(depthT != -1 && depthT < trieHeight){
            //The minimum key among the leaves of the subtree rooted by t
            int minU_t = pred0 * (1 << (trieHeight - depthT)); 
            if(k < minU_t){
                //D_0 must contain a DEL node with key that is in the 
                //range of keys of leaves in the subtree rooted by t
                set<PredecessorNode*> predNodes;
                predNodes.insert(pNode);
                for(DelNode *d : D_0){
                    predNodes.insert(d->delPredNode);
                }

                //pNodePrime is the predecessor node in predNodes that is the latest in Q.
                PredecessorNode *pNodePrime = nullptr;
                for (auto rit = Q.rbegin(); rit != Q.rend(); ++rit) { //Iterate through Q backwards.
                    PredecessorNode *p = *rit;
                    if(predNodes.count(p) > 0){ //Stop as soon as such a predecessor node is found.
                        pNodePrime = p;
                        break;
                    }
                }

                vector<UpdateNode*> LPrime;
                //Insert all updateNodes of the notify nodes of pNodePrime to LPrime.
                NotifyNode *nNode = pNodePrime->notifyListHead;
                while(nNode){
                    LPrime.push_back(nNode->updateNode);
                    nNode = nNode->next;
                }

                set<UpdateNode*> L;
                //Insert all updateNodes not in I_0 union D_0 of notify nodes of pNode to L
                nNode = pNode->notifyListHead;
                while(nNode){
                    UpdateNode *uNode = nNode->updateNode;
                    if(uNode->type == INS){
                        if(I_0.count((InsNode*)uNode) == 0){
                            L.insert(uNode);
                        }
                    }
                    else if(uNode->type == DEL){
                        if(D_0.count((DelNode*)uNode) == 0){
                            L.insert(uNode);
                        }
                    }
                    nNode = nNode->next;
                }
                //Insert the nodes that are in LPrime but not L into LDoublePrime, 
                //in the reverse order of which they are stored in LPrime.
                vector<UpdateNode*> LDoublePrime; 
                for (auto rit = LPrime.rbegin(); rit != LPrime.rend(); ++rit) {
                    UpdateNode *uNode = *rit;
                    if(L.count(uNode) == 0)LDoublePrime.push_back(uNode);
                }
                set<int64_t> R;
                for(DelNode *dNode : D_0){
                    R.insert(dNode->delPred); //Insert the embedded predecessor value of the delNodes in D_0
                }
                for(UpdateNode *uNode : LDoublePrime){
                    if(uNode->type == INS){
                        R.insert(uNode->key);
                    }
                    else{
                        R.insert(uNode->key);
                        int64_t delPred2 = ((DelNode*)uNode)->delPred2;
                        R.insert(delPred2);
                    }
                }
                //Set of keys X, such that for every key x in X, 
                //x is in R, and an UpdateNode with key x is in L'', 
                //and the furthest update node within L'' has type INS
                std::set<int64_t> goodKeys;
                //Go through LDouble prime in reverse
                for(vector<UpdateNode*>::reverse_iterator iter = LDoublePrime.rbegin(); iter != LDoublePrime.rend(); ++iter){
                    UpdateNode *uNode = *iter;
                    int64_t key = uNode->key;
                    //If uNode->key is not in goodKeys and uNode->key is in R...
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

                //Pred0 = maximum value among keys in R
                pred0 = -1;
                for(int64_t v : R){
                    if(v > pred0)pred0 = v;
                }
            }
        }

        //Return the max among pred0 and k.
        if(pred0 > k)return pred0;
        else return k;
    }
    int64_t predecessor(int64_t y){
        //assert(y >= 0 && y <= universeSize);
        trieDebra.startOp();
        //trieRecordManager.startOp(threadID);
        
        PredecessorNode *p = new PredecessorNode(y);
        //trieRecordManager.allocate<PredecessorNode>(threadID, y);
        int64_t pred = predHelper(p);
        P_ALL.remove((ListNode*)p);
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
        assert(RU_ALL.head.successor == (uintptr_t)&RU_ALL.tail);
        assert(U_ALL.head.successor == (uintptr_t)&U_ALL.tail);
        assert(P_ALL.head.successor == (uintptr_t)&P_ALL.tail);
    }

    //TODO ifdef debug
    void printOpLog(){

    }
};
