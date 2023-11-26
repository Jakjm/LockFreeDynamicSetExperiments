#include <cstdint>
#include <stdint.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iterator>
#include <atomic>
#include <cassert>
#include "../LinkedLists/ListNode.h"
#include "../DynamicSet.h"
#include "../common.h"
#include "../debra.h"
#pragma once
using std::string;

struct SkipNode{
    int64_t key;
    std::atomic<uintptr_t> next;
    std::atomic<SkipNode*> backlink;
    SkipNode * root;
    SkipNode * down;
    SkipNode():  key(-1), next(0), backlink(nullptr), root(nullptr), down(nullptr){

    }
    SkipNode(int64_t k) : key(k), next(0), backlink(nullptr), root(nullptr), down(nullptr){

    }
};

struct SkipListPool{
    SkipNode *node;
    volatile char padding[64-sizeof(SkipListPool*)];
    SkipListPool(): node(nullptr){

    }
};

SkipListPool pool[NUM_THREADS];
Debra<SkipNode, 4> skipDebra;

template <int maxLevels>
class SkipListSet : public DynamicSet{
    public:
    SkipNode head[maxLevels];
    SkipNode tail;
    SkipListSet() : tail(INT64_MAX) {
        for(int i = 0;i < maxLevels;++i){
            head[i].next = (uintptr_t)&tail;
            head[i].key = -1;
            head[i].root = &head[0];
            if(i > 0)head[i].down = &head[i-1];
        }
    }
    ~ SkipListSet(){
        for(int i = 0;i < NUM_THREADS;++i){
            SkipNode * volatile n = pool[i].node;
            if(n)delete n;
        }
        //TODO free each level........
    }


    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
    uintptr_t helpMarked(SkipNode *prev, SkipNode *delNode){
        SkipNode *next = (SkipNode*)(delNode->next & NEXT_MASK);
        uintptr_t expected = (uintptr_t)delNode + DelFlag;
        uintptr_t result = expected;
        prev->next.compare_exchange_strong(result, (uintptr_t)next);
        
        if(result == expected){
            skipDebra.retire(delNode);
            return (uintptr_t)next;
        }
        else return result;
    }
    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
    uintptr_t helpRemove(SkipNode *prev, SkipNode *delNode){
        delNode->backlink = prev;
        uintptr_t succ = delNode->next.load(); //The value of delNode's successor pointer
        uintptr_t state = succ & STATUS_MASK;
        uintptr_t next = succ & NEXT_MASK;

        while(state != Marked){ //While delNode is not marked...
            if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                succ = helpRemove(delNode, (SkipNode*)next);
            }
            else{ //Attempt to mark the node if the status was normal...
                uintptr_t markedSuccessor = (uintptr_t)next + Marked;
                succ = next;
                delNode->next.compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                if(succ == next)break; //The CAS succeeded!
            }
            state = succ & STATUS_MASK;
            next = succ & NEXT_MASK;
        }
        succ = helpMarked(prev, delNode);
        return succ;
    }

    SkipNode* findStart(int &level){
        int curLevel = 0;
        uintptr_t succ = head[curLevel + 1].next;
        SkipNode *next = (SkipNode*)(succ & NEXT_MASK);
        while(next != &tail || curLevel < level){
            ++curLevel;
            succ = head[curLevel + 1].next;
            next = (SkipNode*)(succ & NEXT_MASK);
        }
        level = curLevel;
        return &head[curLevel];
    }


    bool tryFlag(SkipNode *&prev, SkipNode *delNode, bool &inList){
        uint64_t succ;
        while(1){
            succ = prev->next;
            if(succ == ((uint64_t)delNode) + DelFlag){
                inList = true;
                return false;
            }
            succ = (uintptr_t)delNode;
            prev->next.compare_exchange_strong(succ, ((uintptr_t)delNode) + DelFlag);
            if(succ == (uintptr_t)delNode){
                inList = true;
                return true;
            }
            if(succ == ((uintptr_t)delNode) + DelFlag){
                inList = true;
                return false;
            }
            while((succ & STATUS_MASK) == Marked){
                prev = prev->backlink;
                succ = prev->next;
            }
            SkipNode *target;
            target = searchRight(delNode->key,prev);
            if(target != delNode){
                inList = false;
                return false;
            }
        }
    }

    SkipNode *searchRight(int64_t k, SkipNode *&curr){
        uintptr_t succ = curr->next;
        SkipNode *next = (SkipNode*)(succ & NEXT_MASK);
        while(next->key <= k){
            while((next->root->next & STATUS_MASK) == Marked){ //If next is superfluous, try to delete it...
                //Try to flag the node 
                succ = (uintptr_t)next;
                bool status;
                tryFlag(curr, next, status);

                if(status){ //If next is still in the list....
                    succ = helpRemove(curr, next);
                }
                
                next = (SkipNode*)(succ & NEXT_MASK);
                
            }
            if(next->key <= k){
                curr = next;
                succ = curr->next;
                next = (SkipNode*)(succ & NEXT_MASK);
            }
        }
        return next; //Return <curr, next>
    }
    SkipNode* searchToLevel(int64_t k, int level, SkipNode *&next){
        int curLevel = level;
        SkipNode *curr;
        curr = findStart(curLevel);
        while(curLevel > level){
            next = searchRight(k, curr);
            curr = curr->down;
            --curLevel;
        }
        next = searchRight(k, curr);
        return curr; //Return <curr, next>
    }
    SkipNode *insertNode(SkipNode *newNode, SkipNode *&prev, SkipNode *next){
        if(prev->key == newNode->key){
            return nullptr;
        }
        while(1){ //Keep on trying to insert until node is inserted
            uint64_t succ = prev->next;
            if((succ & STATUS_MASK) == DelFlag){
                helpRemove(prev, (SkipNode*)(succ & NEXT_MASK));
            }
            else{
                newNode->next = (uintptr_t)next;
                succ = (uintptr_t)next;
                prev->next.compare_exchange_strong(succ, (uintptr_t)newNode);
                if(succ == (uintptr_t)next){
                    return newNode; //CAS succeeded in inserting new node :)
                }
                else{
                    if((succ & STATUS_MASK) == DelFlag){
                        succ = helpRemove(prev, (SkipNode*)(succ & NEXT_MASK));
                    }
                    while((succ & STATUS_MASK) == Marked){
                        prev = prev->backlink;
                        succ = prev->next;
                    }
                }
            }
            next = searchRight(newNode->key, prev);
            if(prev->key == newNode->key){
                return nullptr;
            }
        }
    }
    void insert(int64_t k){
        skipDebra.startOp();
        SkipNode *curr, *next;
        curr = searchToLevel(k,0, next);
        if(curr->key == k){
            skipDebra.endOp();
            return;
        }
        SkipNode *newRoot;
        SkipNode *newNode = new SkipNode(k);
        newNode->down = nullptr;
        newNode->root = newNode;
        int height = 0;
        newRoot = newNode;
        while(randomNum(1) == 0 && height < maxLevels - 1){
            ++height;
        }
        int level = 0;
        while(1){
            SkipNode *result = insertNode(newNode, curr, next);
            if(result == nullptr && level == 0){
                delete newNode; //TODO pool node instead....
                skipDebra.endOp();
                return;
            }
            if((newRoot->next & STATUS_MASK) == Marked){
                if(result == newNode && newNode != newRoot){
                    removeNode(curr, newNode);
                }
                skipDebra.endOp();
                return;
            }
            ++level;
            if(level >= height){ //Stop building the tower if we've already reached the desired height...
                skipDebra.endOp();
                return;
            }
            SkipNode *lastNode = newNode;
            newNode = new SkipNode(k);
            newNode->down = lastNode;
            newNode->root = newRoot;
            curr = searchToLevel(k, level, next);

        }
        skipDebra.endOp();
    }
    SkipNode *removeNode(SkipNode *prev, SkipNode *delNode){
        bool inList;
        bool result = tryFlag(prev, delNode, inList);
        if(inList){
            helpRemove(prev,delNode);
        }
        if(result)return delNode;
        else return nullptr;
    }
    void remove(int64_t k){
        skipDebra.startOp();
        SkipNode *curr, *delNode;
        curr = searchToLevel(k-1,0, delNode);
        if(delNode->key != k){
            skipDebra.endOp();
            return;
        }
        removeNode(curr, delNode);
        SkipNode *next;
        searchToLevel(k,1, next); //Delete nodes at other levels of the tower
        skipDebra.endOp();
    }
    int64_t predecessor(int64_t k){
        skipDebra.startOp();
        SkipNode *curr, *next;
        curr = searchToLevel(k-1, 0,next);
        int64_t result = curr->key;
        skipDebra.endOp();
        return result;
    }
    bool search(int64_t k){
        skipDebra.startOp();
        SkipNode *curr, *next;
        curr = searchToLevel(k, 0,next);
        bool keyContained = curr->key == k;
        skipDebra.endOp();
        return keyContained;
    }
    
};

