#include <algorithm>
#include <climits>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include "FomitchevRuppert/P_ALL.h"
#include "FomitchevRuppert/RU_ALL.h"
#include "trieNodeTypes.h"
#include "BoundedMinReg/minreg.h"
#include "FomitchevRuppert/list.h"
#include "FomitchevRuppert/list_extension.h"

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
using std::set;
using std::string;
using std::deque;

inline int __attribute__((always_inline)) compareUpdate(ListNode *u1, ListNode *u2){
    UpdateNode *a = (UpdateNode*)u1;
    UpdateNode *b = (UpdateNode*)u2;
    return a->key - b->key;
}


inline int __attribute__((always_inline)) reverseCompareUpdate(RU_ALL_Node *u1, RU_ALL_Node *u2){
    UpdateNode *a = (UpdateNode*)u1;
    UpdateNode *b = (UpdateNode*)u2;
    return b->key - a->key;
}


//TODO ifdef debug
class Trie{
    private:
    const int b;
    const int64_t universeSize; //Equal to 2^b 
    vector<vector<TrieNode>> trieNodes;
    vector<LatestList> latest;
    NodeRecordManager recordMgr;
    P_ALL_TYPE P_ALL;
    LinkedList_FRE<compareUpdate> U_ALL;
    RU_ALL_TYPE<reverseCompareUpdate> RU_ALL;
    public:
    Trie(int size) : b(size), universeSize(1 << b), latest(universeSize), recordMgr(NUM_THREADS), P_ALL(), U_ALL(), RU_ALL()
    {
        threadInit();
        auto guard = recordMgr.getGuard(threadID);
        
        //Initialize the binary trie nodes for each level of the trie.
        for(int i = 0; i < b;++i){
            int64_t rowSize = (1 << i); //Row size = 2^i
            vector<TrieNode> trieNodeRow(rowSize);
            trieNodes.push_back(trieNodeRow);
        }

        vector<TrieNode> &baseRow = trieNodes[b-1];

        //Initialize the latest lists for each key in the universe.
        //Initialize row b of binary trie nodes.
        //Initially, each latest list's head = a DelNode with
        for(int64_t key = 0; key < universeSize;++key){
            DelNode *initialDelNode = recordMgr.allocate<DelNode>(threadID, key, b, nullptr);
            initialDelNode->upper0Boundary = b; // The initial delNodes for the trie have upper0Boundary = b.
            latest[key].head = initialDelNode;
            initialDelNode->status = ACTIVE;
            if((key & 1) == 0){ //If the key is even, 
                baseRow[key / 2].dNodePtr = initialDelNode;
                initialDelNode->dNodeCount += 2;
            }
            else{
                initialDelNode->dNodeCount += 1;
            }
        }

        //Initialize rows 0 through b-2, such that every trieNode that is a parent of two trieNodes
        //Has its dNodePtr set to the dNodePtr of its left child.
        for(int row = b-2; row >= 0; row--){
            int64_t rowSize = (1 << row);
            vector<TrieNode> &trieNodeRow = trieNodes[row];
            vector<TrieNode> &childRow = trieNodes[row + 1];
            for(int64_t i = 0; i < rowSize;++i){
                int64_t leftChild = 2 * i;
                DelNode *dNode = childRow[leftChild].dNodePtr;
                trieNodeRow[i].dNodePtr = dNode;
                dNode->dNodeCount += 1;
            }
        }

        verifyLists();
    }
    ~Trie(){
        threadInit();
        recordMgr.startOp(threadID);
        
        //Attempt to retire all of the UpdateNodes stored in the latest lists
        //And all of the dNodePtrs.
        for(vector<TrieNode> &trieNodeRow : trieNodes){
            for(TrieNode &tNode : trieNodeRow){
                DelNode *dNode = tNode.dNodePtr;
                int reclaim = dNode->dNodeCount.fetch_add(-1);
                if(reclaim == 1)dNode->retire(recordMgr);
            }
        }
        for(LatestList &list : latest){
            UpdateNode *uNode = list.head;
            UpdateNode *next = uNode->latestNext;
            if(uNode->type == INS){
                uNode->retire(recordMgr);
            }
            else{
                DelNode *d = (DelNode*)d;
                int retire = d->dNodeCount.fetch_add(-1);
                assert(retire == 1);
                d->retire(recordMgr);
            }
            if(next){
                if(next->type == INS){
                    next->retire(recordMgr);
                }
                else{
                    DelNode *d = (DelNode*)next;
                    int retire = d->dNodeCount.fetch_add(-1);
                    assert(retire == 1);
                    next->retire(recordMgr);
                }
            }
        }
        verifyLists();

        recordMgr.endOp(threadID);

        //Dump limbo bags
        for(int i = 0;i < NUM_THREADS;++i){
            recordMgr.deinitThread(i);
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
        assert(x >= 0 && x <= universeSize);
        threadInit();
        auto guard = recordMgr.getGuard(threadID);

        UpdateNode *l = findLatest(x);
        return l->type == INS; //Return whether the root of the latest was an insert node.
    }

    char interpretedBit(TrieNode *t, int height){
        UpdateNode *dNode = t->dNodePtr;
        UpdateNode *uNode = findLatest(dNode->key);
        if(uNode->type == INS)return 1;
        else{
            DelNode *d = (DelNode*)uNode; //uNode must have type = DEL
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
                
                bool unlinked = uNode->latestNext.compare_exchange_strong(latestNext, nullptr); //Replaces uNode->latestNext = nullptr;
                if(unlinked){ 
                    //TODO might help with knowing if a node can be reclaimed...
                }
            }
        }
    }
    

    //Notify predecessor operation by placing a new notify node in the notify list.
    void sendNotification(NotifyNode *newNotifyNode, PredecessorNode *pNode){
        while(1){
            NotifyNode *nNode = pNode->notifyListHead;
            newNotifyNode->next = nNode;
            if(!firstActivated(newNotifyNode->updateNode))return; //If the updateNode is no longer the first activated one, no need to continue....

            //Otherwise, attempt to put newNotifyNode at the head of the notifyList for pNode.
            NotifyNode *expected = nNode;
            pNode->notifyListHead.compare_exchange_strong(expected,newNotifyNode); 
            if(expected == nNode){
                return; //Succeeded in putting newNotifyNode at the head of the notifyList.
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

            if(firstActivated(uNode)){
                int64_t maxKey = -1;
                InsNode *updateNodeMax = nullptr;
                for(InsNode* insNode : I){
                    if(insNode->key < pNode->key && insNode->key > maxKey){
                            maxKey = insNode->key;
                            updateNodeMax = insNode;
                    }
                }

                NotifyNode *newNotif = recordMgr.allocate<NotifyNode>(threadID, uNode, updateNodeMax, tau);
                sendNotification(newNotif,pNode);
            }
            pNode = (PredecessorNode*)P_ALL.next(pNode);
        }
    }

    void insertBinaryTrie(UpdateNode *iNode){
        //For each binary trie node t on the path from the parent of the leaf with iNode.key to the root, do 
        int64_t key = iNode->key;
        for(int depth = b-1;depth >= 0;--depth){
            key = key >> 1;
            TrieNode &t = trieNodes[depth][key]; //Start from parent of 
            UpdateNode *dNodePtr = t.dNodePtr;
            UpdateNode *uNode = findLatest(dNodePtr->key);

            if (uNode->type == DEL){
                DelNode *delNode = (DelNode*)uNode;
                int height = b - depth;
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
        TrieNode *t = &trieNodes[b][dNode->key]; //Get leaf of the trie with dNode.key
        TrieNode *root = &trieNodes[0][0];
        
        int depth = b;
        int64_t key = dNode->key;
        while(t != root){
            int height = b - depth;
            TrieNode *sibling = &trieNodes[depth][siblingIndex(key)];
            if(interpretedBit(t, height) == 1 || interpretedBit(sibling, height) == 1)return;

            //t = t->parent.
            --depth;
            height = b-depth;
            key = key / 2;
            t = &trieNodes[depth][key];

            DelNode *d = t->dNodePtr;
            if(firstActivated(dNode) == false)return;
            if(dNode->stop || dNode->lower1Boundary.minRead() != b+1)return;
            
            DelNode *expected = d;
            t->dNodePtr.compare_exchange_strong(expected, dNode);
            if(expected != d){
                d = t->dNodePtr;
                if(firstActivated(dNode) == false)return;
                if(dNode->stop || dNode->lower1Boundary.minRead() != b+1)return;
                expected = d;
                t->dNodePtr.compare_exchange_strong(expected, dNode);
                if(expected != d){
                    //Retire dNode if dNode is no longer in shared memory? (TODO maybe this is silly.)
                    return;
                }
                else{
                    //Retire d if it is no longer in shared memory.
                }
            }
            else{
                //Retire d if it is no longer in shared memory?
            }
            TrieNode *left = &trieNodes[depth+1][key * 2], *right = &trieNodes[depth+1][key *2 +1];
            if(interpretedBit(left, height - 1) == 1 || interpretedBit(right, height- 1))return;
            dNode->upper0Boundary = height;
        }
    }

    /**
    Insert operation on the binary trie.
    */
    void insert(int64_t x){
        assert(x >= 0 && x <= universeSize);
        threadInit();
        auto guard = recordMgr.getGuard(threadID);
        UpdateNode *dNode = findLatest(x), *expected;
        if (dNode->type == INS)return; //x already in S, nothing to do!
        //dNode has type DEL and its child, if it has one, is of type INS

        InsNode *iNode = recordMgr.allocate<InsNode, int64_t>(threadID, x);
        iNode->latestNext = dNode;

        UpdateNode *latestNext = dNode->latestNext;
        if(latestNext){
            latestNext->status = STALE;
            //dNode->latestNext = nullptr
            UpdateNode *expected = latestNext;
            dNode->latestNext.compare_exchange_strong(expected, nullptr); 
            if(expected == latestNext){
                assert(latestNext->type == INS);
                latestNext->retire(recordMgr); //Retire the InsertNode.
            }
        }
        expected = dNode;
        latest[x].head.compare_exchange_strong(expected, iNode);
        if (expected != dNode){
            helpActivate(expected);
            //No other thread has seen iNode, so iNode can be retired.
            iNode->retire(recordMgr);
            return;
        }

        U_ALL.insert(iNode);
        RU_ALL.insert(iNode);

        STATUS expectedStatus = INACTIVE;
        iNode->status.compare_exchange_strong( expectedStatus, ACTIVE);
        dNode->status = STALE;
        
        //iNode->latestNext = nullptr
        expected = dNode;
        iNode->latestNext.compare_exchange_strong(expected,nullptr); 
        if(expected == dNode){
            //TODO retire dNode if possible now that 
        }
        
        insertBinaryTrie(iNode);
        notifyPredOps(iNode); //Notify predecessor operations that this insertion is in progress...

        //for each node uNode in U-ALL with uNode.key = x before and including v do
            //uNode.status = STALE

        U_ALL.remove(iNode);
        RU_ALL.remove(iNode);
        //TODO stuff for retiring iNode if possible....
    }
    

    void remove(int64_t x){
        assert(x >= 0 && x <= universeSize);
        threadInit();
        auto guard = recordMgr.getGuard(threadID);

        UpdateNode *iNode = findLatest(x), *expected;
        if(iNode->type == DEL)return; //x is not in S, nothing to do!
        //iNode has type INS. If it has a child, its child has type DEL.

        //PredecessorNode *pNode = new PredecessorNode(x);
        PredecessorNode *pNode = recordMgr.allocate<PredecessorNode>(threadID,x);
        int64_t delPred = predHelper(pNode);

        //Initialize update node for this delete operation.
        DelNode *dNode = recordMgr.allocate<DelNode>(threadID, x, b, iNode, delPred,pNode);
        //delList.insert(v);
        dNode->latestNext = iNode;
    
        
        UpdateNode *latestNext = iNode->latestNext;
        if(latestNext){
            latestNext->status = STALE;
            //iNode->latestNext = nullptr; 
            bool unlink = iNode->latestNext.compare_exchange_strong(latestNext, nullptr);
            if(unlink){  //If this operation unlinked the insNode from the latest list...
                //int retire = latestNext->retireCounter.fetch_add(-1);
                //if(retire == 1)latestNext->retire(recordMgr);
                //assert(retire > 0);
            }   
        }
        notifyPredOps(iNode);
        expected = iNode;
        latest[x].head.compare_exchange_strong(expected, dNode);

        if(expected != iNode){
            //There was a different node, expected, instead of iNode at the head of the latest list.
            //We will help activate it.
            helpActivate(expected);

            //Remove pNode from P_ALL.
            P_ALL.remove(pNode);
            dNode->retire(recordMgr); //Retire dNode and the pNode. 
            return;
        }

        U_ALL.insert(dNode);
        RU_ALL.insert(dNode);

        STATUS expectedStatus = INACTIVE;
        dNode->status.compare_exchange_strong(expectedStatus, ACTIVE);
        iNode->status = STALE;

        UpdateNode *target = iNode->target;
        if(target)target->stop = true;
        //v->latestNext = nullptr; 
        bool unlink = dNode->latestNext.compare_exchange_strong(iNode, nullptr);
        if(unlink){  
            //If this CAS unlinked iNode from the latest list, decrement its retire counter and retire it if necessary.
        }
        PredecessorNode *pNode2 = recordMgr.allocate<PredecessorNode>(threadID, x);
        int64_t delPred2 = predHelper(pNode2);
        dNode->delPred2 = delPred2;

        deleteBinaryTrie(dNode);
        notifyPredOps(dNode);

        // Delete pNode and pNode2 from P-ALL.
        P_ALL.remove(pNode);
        P_ALL.remove(pNode2);

        pNode2->retire(recordMgr); //pNode2 is no longer in shared memory, so it can be retired.
        //pNode is still in shared memory but it will be retired when v is retired.


        U_ALL.remove(dNode);
        RU_ALL.remove(dNode);

        //TODO Try to retire dNode if possible....
    }

    //    0
    //   0 0
    // 0 0 0 0 

    //y = 3 
    //height = 2  
    int64_t traverseBinaryTrie(int64_t y, int64_t &depth){
        if(b <= 1)return -1;

        //Get interpreted bit
        //TrieNode *t = &trieNodes[b][y];
        TrieNode *tSibling = &trieNodes[b][siblingIndex(y)], 
                            *tParent = &trieNodes[b-1][y / 2];
        depth = b;
        int height = b - depth;
        char i1 = interpretedBit(tParent, height + 1);
        char i2 = interpretedBit(tSibling, height);
        
        //While i1 = 0, i2 == 0, or t is the leftChild of tParent
        while(i1 == 0 || (y % 2 == 0) || i2 == 0){
            y = y >> 1;
            --depth;
            height = b - depth;
            if(depth == 0){
                return -1; //Interpreted bit of root node was 0 on previous iteration. Return <-1, null>
            }

            //t = tParent;
            tSibling = &trieNodes[depth][siblingIndex(y)];
            tParent = &trieNodes[depth - 1][y / 2];
            
            i1 = interpretedBit(tParent,height + 1);
            i2 = interpretedBit(tSibling, height);
        }

        //Go to left child of parent. Subtract 1 from y if it is odd. 
        y = y - (y & 1);
        //t = &trieNodes[depth][y];
    
        while(depth < b){
            //Right child is at 2*y + 1, left child is at 2 * y.
            int64_t rightIndex = y * 2 + 1, leftIndex = y * 2;
            int childDepth = depth + 1;
            int childHeight = height - 1;
            TrieNode *right = &trieNodes[childDepth][rightIndex];
            if(interpretedBit(right, childHeight) == 1){
                y = rightIndex;
                depth = childDepth;
                height = childHeight;
                continue;
            }
            TrieNode *left = &trieNodes[childDepth][leftIndex];
            if(interpretedBit(left, childHeight) == 1){
                y = leftIndex;
                depth = childDepth;
                height = childHeight;
                continue;
            }
            
            //Interpreted bits of left and right nodes are 0. No predecessor found.
            return -2;
        }
        return y;
    }
    


    void traverseAndInsertPALL(PredecessorNode *newNode, deque<PredecessorNode*> &q){
        set<PredecessorNode*> qSet;
        PredecessorNode *first = (PredecessorNode*)(P_ALL.head.successor.load() & NEXT_MASK);
        PredecessorNode *pNode = first;
        
        //Traverse P_ALL from start to end
        while(pNode){
            q.push_back(pNode);
            qSet.insert(pNode);
            pNode = (PredecessorNode*)P_ALL.next(pNode);
        }

        //Insert newNode into P_ALL
        while(1){
            //Set newNode's next to first.
            newNode->successor = (uintptr_t)first;
            uintptr_t expected = (uintptr_t)first;

            P_ALL.head.successor.compare_exchange_strong(expected, (uintptr_t)newNode);
            if(expected == (uintptr_t)first){
                q.push_front(newNode); //Put newNode at the front of q.
                return; //newNode was successfully inserted
            }
            int64_t state = (int64_t)(expected & STATUS_MASK);
            ListNode *next = (ListNode*)(expected & NEXT_MASK);
            if(state == DelFlag){
                first = (PredecessorNode*)(P_ALL.helpRemove(&P_ALL.head, next) & NEXT_MASK);
            }

            vector<PredecessorNode*> qPrime;
            first = (PredecessorNode*)(P_ALL.head.successor.load() & NEXT_MASK);
            pNode = first;
            //Traverse P_ALL from start to end, add any nodes not in Q to qPrime
            while(pNode && qSet.count(pNode) == 0){
                qPrime.push_back(pNode);
                pNode = (PredecessorNode*)P_ALL.next(pNode);
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
    void traverseRUALL(PredecessorNode *pNode, unordered_set<InsNode *> &I, unordered_set<DelNode*> &D){
        UpdateNode *uNode = (UpdateNode*)RU_ALL.first(pNode); //Atomically set pNode.notifyThreshold....
        while(uNode){
            assert(uNode != &RU_ALL.tail);
            if(uNode->key < pNode->key){
                if(uNode->status != INACTIVE && firstActivated(uNode)){
                    if(uNode->type == INS) I.insert((InsNode*)uNode);
                    else D.insert((DelNode*)uNode);
                }
            }
            uNode = (UpdateNode*)RU_ALL.next(pNode,uNode); //Atomically set pNode.notifyThreshold....
        }
        pNode->notifyThreshold.store(&ZERO_THRES);
    }

    int64_t predHelper(PredecessorNode *pNode){
        deque<PredecessorNode*> Q;
        vector<InsNode*> I_1, I_2;
        vector<DelNode*> D_1, D_2;
        unordered_set<InsNode*> I_0;
        unordered_set<DelNode*> D_0;
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
        
        //pred1 = key of ins node in I_1 with greatest key
        //pred2 = key of ins node in I_2 with greatest key
        //pred3 = key of del node in d1_minus_d0 with largest key
        //pred4 = key of del node in d2_minus_d0 with largest key
        int64_t pred1 = -1, pred2 = -1, pred3, pred4;
        for(InsNode *i : I_1){
            if(i->key > pred1)pred1 = i->key;

        }
        for(InsNode *i : I_2){
            if(i->key > pred2)pred2 = i->key;
        }

        //Create D1 - D0 and D2 - D0
        for(DelNode *d : D_1){
            if(D_0.count(d) == 0){
                if(d->key > pred3)pred3 = d->key;
            }
        }
        for(DelNode *d : D_2){
            if(D_0.count(d) == 0){
                if(d->key > pred4)pred4 = d->key;
            }
        }

        int k = -1;
        if(pred1 > k) k = pred1;
        if(pred2 > k) k = pred2;
        if(pred3 > k) k = pred3;
        if(pred4 > k) k = pred4;
        
        //Binary trie traversal stopped at an internal node t at depth depthT
        if(pred0 == -2){
            //The minimum key among the leaves of the subtree rooted by t
            //pred0 * 2^(b - depthT)
            int minU_t = pred0 * (1 << (b - depthT)); 
            if(k < minU_t){
                //D_0 must contain a DEL node with key that is in the 
                //range of keys of leaves in the subtree rooted by t
                unordered_set<PredecessorNode*> predNodes;
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

                unordered_set<UpdateNode*> L;
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
                unordered_set<int64_t> R;
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
                std::unordered_set<int64_t> goodKeys;
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
        assert(y >= 0 && y <= universeSize);

        threadInit();
        auto guard = recordMgr.getGuard(threadID);
        
        PredecessorNode *p = recordMgr.allocate<PredecessorNode>(threadID, y);
        int64_t pred = predHelper(p);
        P_ALL.remove(p);
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

    //Verify that all of the lists are empty
    void verifyLists(){
        assert(RU_ALL.head.successor == (uintptr_t)&RU_ALL.tail);
        assert(U_ALL.head.successor == (uintptr_t)&U_ALL.tail);
        assert(P_ALL.head.successor == (uintptr_t)&P_ALL.tail);
    }

    //TODO ifdef debug
    void printOpLog(){

    }

    //Initialize this thread with the record manager if this hasn't happened already.
    void threadInit(){
        assert(threadID != -1);
        recordMgr.initThread(threadID);
    }
};
