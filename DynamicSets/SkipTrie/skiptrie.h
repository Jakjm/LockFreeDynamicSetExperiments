#include <iostream>
#include <string>
#include <atomic>
#include <cstdint>
#include <stack>
#include "../DynamicSet.h"
#include "../../common.h"
#include "../debra.h"


using std::stack;
//Nodes of the skip trie.....
struct alignas(64) STNode{
    int64_t key;
    std::atomic<uintptr_t> nextState; //Contains <next, mark>
    std::atomic<STNode*> back;
    std::atomic<STNode*> prev; 
    STNode* down;
    STNode* root;
    std::atomic<bool> ready;
    //volatile char padding[64 - 4 * sizeof(STNode*) + sizeof(bool)];
    STNode(int64_t k=0) : key(k), nextState(0), back(nullptr), down(nullptr), root(nullptr), ready(false){

    }
};

//Type which is used to allow threads to reuse nodes they fail to insert...
struct STNodePool{
    STNode *node;
    volatile char padding[64 - 8];
    STNodePool(){

    }
    ~STNodePool(){
        delete node;
    }
};

STNodePool node_pool[MAX_THREADS];


Debra<STNode,5> stDebra;

//numLevels - max number of levels in the skip list....
template <int numLevels>
struct SkipTrie : public DynamicSet {
    STNode head[numLevels]; //levels are numbered 0, ..., levels - 1
    STNode tail;
    SkipTrie(): tail(INT64_MAX){
        //Initialize the skip trie...
        head[0] = STNode(INT64_MIN);
        for(int l = 1; l < numLevels; ++l){
            head[l] = STNode(INT64_MIN);
            head[l].nextState = (uintptr_t)&tail;
            head[l].down = head[l - 1];
        }
    }
    STNode *lowestAncestor(int64_t key){
        return nullptr;
    }
    STNode *xFastTriePred(int64_t key){
        STNode *curr = lowestAncestor(key);
        while(curr->key > key){
            uintptr_t state = curr->nextState & STATUS_MASK;
            if(state == Marked)curr = curr->back;
            else curr = curr->prev;
        }
        return curr;
    }
    void fixPrev(STNode *pred, STNode *node){
        STNode *left, *right;
        left = pred;
        right = searchRight(node->key,left);
        uintptr_t nextState = node->nextState;
        uint64_t state = nextState & STATUS_MASK;
        while(state != Marked){
            STNode *prev = node->prev;
            atomic_noexcept{ //Use a transaction to perform a DCSS.
                if(left->nextState == (uintptr_t)node){ //left.succ = <node, 0>
                    if(node->prev == prev){
                        node->prev = left;
                        break;
                    }
                }
            }
            //search
            left = pred;
            right = searchRight(node->key, left);

            nextState = node->nextState;
            state = nextState & STATUS_MASK;
        }
        node->ready = true;
    }
    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
    uintptr_t helpMarked(STNode *prev, STNode *delNode){
        STNode *next = (STNode*)(delNode->nextState & NEXT_MASK);
        uintptr_t expected = (uintptr_t)delNode + DelFlag;
        uintptr_t result = expected;
        prev->nextState.compare_exchange_strong(result, (uintptr_t)next);
        //assert(prev->key < next->key);
        //assert((STNode*)(next->nextState & NEXT_MASK) != prev);
        
        if(result == expected){
            stDebra.reclaimLater(delNode);
            return (uintptr_t)next;
        }
        else return result;
    }
    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
    uintptr_t helpRemove(STNode *prev, STNode *delNode){
        delNode->back = prev;
        uintptr_t succ = delNode->nextState.load(); //The value of delNode's successor pointer
        uintptr_t state = succ & STATUS_MASK;
        uintptr_t next = succ & NEXT_MASK;

        while(state != Marked){ //While delNode is not marked...
            if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                succ = helpRemove(delNode, (STNode*)next);
            }
            else{ //Attempt to mark the node if the status was normal...
                uintptr_t markedSuccessor = (uintptr_t)next + Marked;
                succ = next;
                delNode->nextState.compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                //assert((STNode*)(delNode->nextState & NEXT_MASK) != prev);
                if(succ == next)break; //The CAS succeeded!
            }
            state = succ & STATUS_MASK;
            next = succ & NEXT_MASK;
        }
        succ = helpMarked(prev, delNode);
        return succ;
    }



    //Precondition: targetNode->root is marked, and prev->right was equal to targetNode.
    bool tryFlag(STNode *&prev, STNode *targetNode, bool &inList){
        while(1){
            uintptr_t succ = prev->nextState;
            if(succ == ((uintptr_t)targetNode) + DelFlag){
                inList = true;
                return false;
            }
            succ = (uintptr_t)targetNode;
            prev->nextState.compare_exchange_strong(succ, ((uintptr_t)targetNode) + DelFlag);
            if(succ == (uintptr_t)targetNode){
                inList = true;
                return true;
            }
            if(succ == (((uintptr_t)targetNode) + DelFlag)){
                inList = true;
                return false;
            }
            while((succ & STATUS_MASK) == Marked){
                prev = prev->back;
                succ = prev->nextState;
            }

            STNode *delNode = searchRight(targetNode->key - 1,prev);
            if(targetNode != delNode){
                inList = false;
                return false;
            }
        }
    }

    //Returns next and curr such that curr.key <= k < next.key
    STNode *searchRight(int64_t k, STNode *&curr){
        uintptr_t succ = curr->nextState;
        STNode *next = (STNode*)(succ & NEXT_MASK);      
        int64_t status = (succ & STATUS_MASK);  
        //assert((curr->key <= k) && (curr->key < next->key));
        while(true){
            if(status == DelFlag){ //If curr is flagged,, try to remove the node following it.
                succ = helpRemove(curr, next);
            }
            else if(status == Marked){ //If curr is marked, try to help remove it, and resume search from node preceding it.
                STNode *prev = curr->back;
                succ = prev->nextState;
                if(succ == (uintptr_t)next + DelFlag){
                    succ = helpMarked(prev, curr);
                }
                curr = prev;
            }
            else if((next->root->nextState & STATUS_MASK) == Marked){ //If next is superfluous, try to flag the node preceding it.
                succ = (uintptr_t)next;
                curr->nextState.compare_exchange_strong(succ, (uintptr_t)next + DelFlag);
            }
            else{
                if(next->key <= k){
                    curr = next;
                    succ = curr->nextState;
                }
                else{
                    break;
                }
            }
            next = (STNode*)(succ & NEXT_MASK);
            status = (succ & STATUS_MASK);  
        }
        //assert(curr->key <= k);
        //assert(next->key > k);
        return next; //Return <curr, next> such that curr->key <= k < next->key
    }


    // STNode* findStart(int &level){
    //     int curLevel = level;
    //     uintptr_t succ = head[curLevel + 1].succ;
    //     STNode *next = (STNode*)(succ & NEXT_MASK);
    //     while(next != &tail && curLevel < numLevels - 1){
    //         ++curLevel;

    //         succ = head[curLevel + 1].succ;
    //         next = (STNode*)(succ & NEXT_MASK);
    //     }
    //     level = curLevel;
    //     return &head[curLevel];
    // }

    //Storing nodes with key <= k for each level on the stack
    STNode* searchToLevel(int64_t k, int level, STNode *&next, std::array<STNode*,numLevels> &levelStart){
        int curLevel = numLevels - 1;
        STNode *curr = &head[curLevel];
        while(curLevel > level){
            next = searchRight(k, curr);
            levelStart[curLevel] = curr;;
            curr = curr->down;
            --curLevel;
        }
        next = searchRight(k, curr);
        levelStart[curLevel] = curr;
        return curr; //Return <curr, next>
    }
    STNode* searchToLevel(int64_t k, int level, STNode *&next){
        int curLevel = numLevels - 1;
        STNode *curr = &head[curLevel];
        while(curLevel > level){
            next = searchRight(k, curr);
            curr = curr->down;
            --curLevel;
        }
        next = searchRight(k, curr);
        return curr; //Return <curr, next>
    }
    STNode *insertNode(STNode *newNode, STNode *&prev, STNode *next){
        //assert(newNode->down);
        if(prev->key == newNode->key){
            return nullptr;
        }
        while(1){ //Keep on trying to insert until node is inserted
            uint64_t succ = prev->nextState;
            if((succ & STATUS_MASK) == DelFlag){
                helpRemove(prev, (STNode*)(succ & NEXT_MASK));
            }
            else{
                newNode->nextState = (uintptr_t)next;
                succ = (uintptr_t)next;
                //assert(prev->key < newNode->key);
                //assert(newNode->key < next->key);
                prev->nextState.compare_exchange_strong(succ, (uintptr_t)newNode);
                //assert((STNode*)(newNode->nextState & NEXT_MASK) != prev);
                if(succ == (uintptr_t)next){
                    return newNode; //CAS succeeded in inserting new node :)
                }
                else{
                    if((succ & STATUS_MASK) == DelFlag){
                        succ = helpRemove(prev, (STNode*)(succ & NEXT_MASK));
                    }
                    while((succ & STATUS_MASK) == Marked){
                        prev = prev->back;
                        succ = prev->nextState;
                    }
                }
            }
            next = searchRight(newNode->key, prev);
            //assert(prev->key <= newNode->key);
            //assert(newNode->key < next->key);
            if(prev->key == newNode->key){
                return nullptr;
            }
        }
    }
    bool insert(int64_t k){
        stDebra.startOp();
        STNode *curr, *next;
        std::array<STNode*,numLevels> startingPlaces;
        curr = searchToLevel(k, 0, next, startingPlaces);
        if(curr->key == k){
            stDebra.endOp();
            return false;
        }
        STNode *newRoot = node_pool[threadID].node;
        STNode *newNode = newRoot;
        STNode *lastNode;
        int height = 1;
        //Continue increasing height up to max level while flipping a fair coin
        while(rng(2) == 0 && height < numLevels - 1){
            ++height;
        }
        int level = 0;
        while(true){
            lastNode = newNode;
            newNode = node_pool[threadID].node;
            newNode->key = k;
            newNode->down = lastNode;
            newNode->root = newRoot;

            //If insertion of node has failed, and this is the first level, keep node for subsequent insertion...
            STNode *result = insertNode(newNode, curr, next);
            if(result == nullptr && level == 0){
                //delete newNode; <-- instead of this, nodes are pooled.
                stDebra.endOp();
                return false;
            }
            node_pool[threadID].node = new STNode(k);
            if((newRoot->nextState & STATUS_MASK) == Marked){
                //If newNode was inserted and this is not the bottom level, help remove newnode...
                if(result == newNode && newNode != newRoot){
                    removeNode(curr, newNode);
                }
                stDebra.endOp();
                return true;
            }
            ++level;
            if(level >= height){ //Stop building the tower if we've already reached the desired height...
                stDebra.endOp();
                return true;
            }

            curr = startingPlaces[level];
            next = searchRight(k,curr);
        }
        stDebra.endOp();
        return level > 0;
    }
    STNode *removeNode(STNode *prev, STNode *delNode){
        bool inList;
        bool result = tryFlag(prev, delNode, inList);
        if(inList){
            helpRemove(prev,delNode);
        }
        if(result)return delNode;
        else return nullptr;
    }
    bool remove(int64_t k){
        stDebra.startOp();
        STNode *curr, *delNode;
        std::array<STNode*,numLevels> startingPlaces;
        curr = searchToLevel(k-1,0, delNode,startingPlaces);
        if(delNode->key != k){ //delNode does not have key k so we will not remove it.
            stDebra.endOp();
            return false;
        }
        removeNode(curr, delNode);
        //Search for other levels....
        for(int level = numLevels - 1; level >= 1;--level){
            curr = startingPlaces[level];
            searchRight(k+1,curr);
        }
        stDebra.endOp();
        return true;
    }
    int64_t predecessor(int64_t k){
        stDebra.startOp();
        STNode *curr, *next;
        curr = searchToLevel(k-1, 0,next);
        int64_t result = curr->key;
        stDebra.endOp();
        return result;
    }
    bool search(int64_t k){
        stDebra.startOp();
        STNode *curr, *next;
        curr = searchToLevel(k, 0,next);
        bool keyContained = curr->key == k;
        stDebra.endOp();
        return keyContained;
    }
    std::string name(){
        return "Shavit SkipTrie";
    }
};
