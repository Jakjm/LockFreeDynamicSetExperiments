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
#include "../DynamicSet.h"
#include "../../common.h"
#include "../debra.h"
#pragma once
using std::string;

struct SkipNode{
    int64_t key;
    std::atomic<uintptr_t> succ;
    std::atomic<SkipNode*> backlink;
    SkipNode * root;
    SkipNode * down;

    SkipNode(int64_t k = 0) : key(k), succ(0), backlink(nullptr), root(nullptr), down(nullptr){

    }
};

struct SkipListPool{
    SkipNode *node;
    volatile char padding[64-sizeof(SkipListPool*)];
    SkipListPool() : node(new SkipNode()){

    }
    ~SkipListPool(){
        delete node;
    }
};


SkipListPool pool[MAX_THREADS];
Debra<SkipNode, 7> skipDebra;

template <int maxLevels>
class SkipListSet : public DynamicSet{
    public:
    SkipNode head[maxLevels];
    SkipNode tail;
    SkipListSet() : tail(INT64_MAX) {
        for(int i = 0;i < maxLevels;++i){
            head[i].succ = (uintptr_t)&tail;
            head[i].key = -1;
            head[i].root = &head[0];
            if(i > 0){
                head[i].down = &head[i-1];
            }
            else{
                head[i].down = &head[i];
            }
        }
        tail.root = &tail;
    }
    ~ SkipListSet(){
        for(int lv = maxLevels-1; lv >= 0;--lv){
            SkipNode *cur = (SkipNode*)(head[lv].succ & NEXT_MASK); 
            SkipNode *next;
            while(cur != &tail){
                next = (SkipNode*)(cur->succ & NEXT_MASK);
                delete cur;
                cur = next;
            }
        }
    }


    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
    uintptr_t helpMarked(SkipNode *prev, SkipNode *delNode){
        SkipNode *next = (SkipNode*)(delNode->succ & NEXT_MASK);
        uintptr_t expected = (uintptr_t)delNode + DelFlag;
        uintptr_t result = expected;
        prev->succ.compare_exchange_strong(result, (uintptr_t)next);
        //assert(prev->key < next->key);
        //assert((SkipNode*)(next->succ & NEXT_MASK) != prev);
        
        if(result == expected){
            skipDebra.reclaimLater(delNode);
            return (uintptr_t)next;
        }
        else return result;
    }
    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
    uintptr_t helpRemove(SkipNode *prev, SkipNode *delNode){
        delNode->backlink = prev;
        uintptr_t succ = delNode->succ.load(); //The value of delNode's successor pointer
        uintptr_t state = succ & STATUS_MASK;
        uintptr_t next = succ & NEXT_MASK;

        while(state != Marked){ //While delNode is not marked...
            if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                succ = helpRemove(delNode, (SkipNode*)next);
            }
            else{ //Attempt to mark the node if the status was normal...
                uintptr_t markedSuccessor = (uintptr_t)next + Marked;
                succ = next;
                delNode->succ.compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                //assert((SkipNode*)(delNode->succ & NEXT_MASK) != prev);
                if(succ == next)break; //The CAS succeeded!
            }
            state = succ & STATUS_MASK;
            next = succ & NEXT_MASK;
        }
        succ = helpMarked(prev, delNode);
        return succ;
    }



    //Precondition: targetNode->root is marked, and prev->right was equal to targetNode.
    bool tryFlag(SkipNode *&prev, SkipNode *targetNode, bool &inList){
        while(1){
            uintptr_t succ = prev->succ;
            if(succ == ((uintptr_t)targetNode) + DelFlag){
                inList = true;
                return false;
            }
            succ = (uintptr_t)targetNode;
            prev->succ.compare_exchange_strong(succ, ((uintptr_t)targetNode) + DelFlag);
            assert((SkipNode*)(targetNode->succ & NEXT_MASK) != prev);
            if(succ == (uintptr_t)targetNode){
                inList = true;
                return true;
            }
            if(succ == (((uintptr_t)targetNode) + DelFlag)){
                inList = true;
                return false;
            }
            while((succ & STATUS_MASK) == Marked){
                assert(prev->key > ((SkipNode*)prev->backlink)->key);
                prev = prev->backlink;
                succ = prev->succ;
            }

            SkipNode *delNode = searchRight(targetNode->key - 1,prev);
            assert(prev->key <= (targetNode->key - 1) && (targetNode->key - 1) < delNode->key);
            if(targetNode != delNode){
                inList = false;
                return false;
            }
        }
    }

    SkipNode *searchRight(int64_t k, SkipNode *&curr){
        uintptr_t succ = curr->succ;
        SkipNode *next = (SkipNode*)(succ & NEXT_MASK);      
        int64_t status = (succ & STATUS_MASK);  
        //assert((curr->key <= k) && (curr->key < next->key));
        while(next->key <= k){
            if(status == DelFlag){ //If curr is flagged,, try to remove the node following it.
                succ = helpRemove(curr, next);
            }
            else if(status == Marked){ //If curr is marked, try to help remove it, and resume search from node preceding it.
                SkipNode *prev = curr->backlink;
                succ = prev->succ;
                if(succ == (uintptr_t)next + DelFlag){
                    succ = helpMarked(prev, curr);
                }
                curr = prev;
            }
            else if((next->root->succ & STATUS_MASK) == Marked){ //If next is superfluous, try to flag the node preceding it.
                succ = (uintptr_t)next;
                curr->succ.compare_exchange_strong(succ, (uintptr_t)next + DelFlag);
                if(succ == (uintptr_t)next){ //CAS succeeded, try to remove node.
                    helpRemove(curr, next);
                }
            }
            else{
                curr = next;
                succ = curr->succ;
            }
            next = (SkipNode*)(succ & NEXT_MASK);
            status = (succ & STATUS_MASK);  
        }
        //assert(curr->key <= k);
        //assert(next->key > k);
        return next; //Return <curr, next> such that curr->key <= k < next->key
    }


    SkipNode* findStart(int &level){
        int curLevel = level;
        uintptr_t succ = head[curLevel + 1].succ;
        SkipNode *next = (SkipNode*)(succ & NEXT_MASK);
        while(next != &tail && curLevel < maxLevels - 1){
            ++curLevel;

            succ = head[curLevel + 1].succ;
            next = (SkipNode*)(succ & NEXT_MASK);
        }
        level = curLevel;
        return &head[curLevel];
    }

    //Storing nodes with key <= k for each level on the stack
    SkipNode* searchToLevel(int64_t k, int level, SkipNode *&next, stack<SkipNode*> levelStart){
        int curLevel = level;
        SkipNode *curr;
        curr = findStart(curLevel);
        while(curLevel > level){
            next = searchRight(k, curr);
            levelStart.push(curr);
            curr = curr->down;
            --curLevel;
        }
        next = searchRight(k, curr);
        return curr; //Return <curr, next>
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
        //assert(newNode->down);
        if(prev->key == newNode->key){
            return nullptr;
        }
        while(1){ //Keep on trying to insert until node is inserted
            uint64_t succ = prev->succ;
            if((succ & STATUS_MASK) == DelFlag){
                helpRemove(prev, (SkipNode*)(succ & NEXT_MASK));
            }
            else{
                newNode->succ = (uintptr_t)next;
                succ = (uintptr_t)next;
                //assert(prev->key < newNode->key);
                //assert(newNode->key < next->key);
                prev->succ.compare_exchange_strong(succ, (uintptr_t)newNode);
                //assert((SkipNode*)(newNode->succ & NEXT_MASK) != prev);
                if(succ == (uintptr_t)next){
                    return newNode; //CAS succeeded in inserting new node :)
                }
                else{
                    if((succ & STATUS_MASK) == DelFlag){
                        succ = helpRemove(prev, (SkipNode*)(succ & NEXT_MASK));
                    }
                    while((succ & STATUS_MASK) == Marked){
                        prev = prev->backlink;
                        succ = prev->succ;
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
    void insert(int64_t k){
        skipDebra.startOp();
        SkipNode *curr, *next;
        curr = searchToLevel(k, 0, next);
        if(curr->key == k){
            skipDebra.endOp();
            return;
        }
        SkipNode *newRoot = pool[threadID].node;
        SkipNode *newNode = newRoot;
        SkipNode *lastNode;
        int height = 1;
        while(rng(2) == 0 && height < maxLevels - 1){
            ++height;
        }
        int level = 0;
        while(1){
            lastNode = newNode;
            newNode = pool[threadID].node;
            newNode->key = k;
            newNode->down = lastNode;
            newNode->root = newRoot;
            //assert(newNode->down != nullptr);
            //assert(newNode->key == k);

            //If insertion of node has failed, and this is the first level, keep node for subsequent insertion...
            SkipNode *result = insertNode(newNode, curr, next);
            if(result == nullptr && level == 0){
                //delete newNode; <-- instead of this, nodes are pooled.
                skipDebra.endOp();
                return;
            }
            pool[threadID].node = new SkipNode(k);
            if((newRoot->succ & STATUS_MASK) == Marked){
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
        if(delNode->key != k){ //delNode does not have key k so we will not remove it.
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
    string name(){
        return "Fomitchev/Ruppert SkipListSet";
    }
    char stat_to_char(uint64_t status){
        if(status == Normal){
            return ' ';
        }
        else if(status == DelFlag){
            return 'F';
        }
        else if(status == Marked){
            return 'M';
        }
        return ' ';
    }

    //Thread safe way of printing list
    void printList(){
        for(int i = maxLevels - 1; i >= 0; --i){
            std::ostringstream stream;
            SkipNode *node = &head[i];
            uintptr_t succ;
            SkipNode *next;
            int status;
            stream << "Level: " << i << "{";
            do{
                succ = node->succ;
                next = (SkipNode*)(succ & NEXT_MASK);
                status = (succ & STATUS_MASK);
                stream << "<" << node->key << ", " << stat_to_char(status)  << ">";
                node = next;
            }while(node);
            stream << "}\n";
            std::cout << stream.str();
        }
    }
};
