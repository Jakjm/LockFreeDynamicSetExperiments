#include <iostream>
#include <string>
#include <atomic>
#include <cstdint>
#include <stack>
#include "../DynamicSet.h"
#include "../../common.h"
#define NEXT_bits 0xFFFFFFFFFFFFFFFE
#define MARK_bits 0x1

using std::stack;
//Nodes of the skip trie.....
struct STNode{
    int64_t key;
    std::atomic<uintptr_t> NextMark; //Contains <next, mark>
    std::atomic<STNode*> back;
    std::atomic<STNode*> prev; 
    std::atomic<STNode*> down;
    std::atomic<bool> ready;
    std::atomic<bool> stop; //
    volatile char padding[64 - 4 * sizeof(STNode*) + sizeof(bool)];
    STNode(int64_t k=0) : key(k), NextMark(0), back(nullptr), down(nullptr), ready(false), stop(false){

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




//maxLevels - number of maxLevels in the skip list....
template <int maxLevels>
struct SkipTrie : public DynamicSet {
    STNode heads[maxLevels]; //levels are numbered 0, ..., levels - 1
    STNode tail;
    SkipTrie(): tail(INT64_MAX){
        //Initialize the skip trie...
        heads[0] = STNode(INT64_MIN);
        for(int l = 1; l < maxLevels; ++l){
            heads[l] = STNode(INT64_MIN);
            heads[l].NextMark = (uintptr_t)&tail;
            heads[l].down = heads[l - 1];
        }
    }
    //Performs a search for two nodes on a fixed level of the skip list.
    //Returns two pointers to STNodes, left and right, such that left.key < x <= right.key
    STNode *listSearch(STNode *&right, int64_t x, STNode *curr){
        uintptr_t nextmark = curr->NextMark;
        STNode *next = (STNode*)(nextmark & NEXT_bits);
        uint64_t mark = (nextmark & MARK_bits);
        do{
            if(mark){
                STNode *prev = curr->back;
                uintptr_t result = (uintptr_t)curr;
                prev->NextMark.compare_exchange_strong(result, (uintptr_t)next);    
                curr = prev;
            }

            nextmark = curr->NextMark;
            next = (STNode*)(nextmark & NEXT_bits);
            mark = (nextmark & MARK_bits);
            
        }while(next->key < x);
        right = next;
        return curr;
    }
    //Performs a search down to level 'level', and returns two nodes, left and right, such that
    // - left and right are on level 'level',
    // - left.key < x <= right.key
    // - at some point, left.nextmark = <right, unmarked>
    STNode *searchToLevel(STNode *&right, int64_t x, int target_level){
        int level = maxLevels - 1;
        STNode *curr = &heads[level];
        uintptr_t next_mark = curr->NextMark;
        STNode *next = (STNode*)(next_mark & NEXT_MASK);
        while(next == &tail && level > 0){
            --level;
            curr = &heads[level];
            next_mark = curr->NextMark;
            next = (STNode*)(next_mark & NEXT_MASK);
        }
        //At this point, curr is the head on level 'level' and its next is not the tail...

        //Start a search on this level...
        stack<STNode*> startingPlaces; //Starting points at levels 1 and up...
        curr = listSearch(next, x, curr);

        //If still not at level 0, continue search down to level 0.
        while(level > target_level){
            startingPlaces.push(curr);
            --level;
            curr = curr->down;
            curr = listSearch(next, x, curr);
        }

        right = next;
        return next;
    }
    void fixPrev(STNode *prev, STNode *node){
        STNode *left, *right;
        left = listSearch(right, node->key, prev);

        uintptr_t next_mark = node->NextMark;
        uint64_t mark = (next_mark & MARK_bits);
        while(!mark){
            STNode *prev = node->prev;
            atomic_noexcept{ //Use a transaction to perform a DCSS.
                if(left->NextMark == (uintptr_t)node){ //left.succ = <node, 0>
                    if(node->prev == prev){
                        node->prev = left;
                        break;
                    }
                }
            }

            next_mark = node->NextMark;
            mark = (next_mark & MARK_bits);
        }
        node->ready = true;
    }
    void insert(int64_t x){
        int height = 1;
        //Continue increasing height up to max level while flipping a fair coin
        while(rng(2) == 0 && height < maxLevels - 1){
            ++height;
        }

        //First insert the node at level 0....
        //while(level < height)

        //Find level to start from...
        int level = maxLevels - 1;
        STNode *curr = &heads[level];
        uintptr_t next_mark = curr->NextMark;
        STNode *next = (STNode*)(next_mark & NEXT_MASK);
        while(next == &tail && level > 0){
            --level;
            curr = &heads[level];
            next_mark = curr->NextMark;
            next = (STNode*)(next_mark & NEXT_MASK);
        }
        //At this point, curr is the head on level 'level' and its next is not the tail...

        //Start a search on this level...
        stack<STNode*> startingPlaces; //Starting points at levels 1 and up...
        curr = listSearch(next, x, curr);

        //If still not at level 0, continue search down to level 0.
        while(level > 0){
            startingPlaces.push(curr);
            --level;
            curr = curr->down;
            curr = listSearch(next, x, curr);
        }

        STNode *baseNode = node_pool[threadID].node;
        STNode *down = nullptr;
        STNode *newNode = node_pool[threadID].node;
        //while levelsInserted < height
        do{
            if(baseNode->stop){
                return;
            }

            newNode->key = x;
            newNode->NextMark = (uintptr_t)next;
            if(level == 0)newNode->down = newNode;
            else newNode->down = down;
            uintptr_t result = (uintptr_t)next;
            curr->NextMark.compare_exchange_strong(result, (uintptr_t)newNode);
            
            if(result == (uintptr_t)next){ //if CAS succeeded
                down = newNode;
                node_pool[threadID].node = new STNode(x);
                newNode = node_pool[threadID].node;
                ++level;
                //If we had a starting place while traversing down, use it!
                if(startingPlaces.empty())curr = &heads[level];
                else{
                    curr = startingPlaces.top();
                    startingPlaces.pop();
                }
            }
            //Perform a search from current node on this level...
            curr = listSearch(next, x, curr);
        }while(level < height);

    }
    void remove(int64_t x){
        
    }
    int64_t predecessor(int64_t x){
        STNode *curr, *next;
        curr = searchToLevel(next, x, 0);
        return curr->key;
    }
    bool search(int64_t x){
        STNode *curr, *next;
        curr = searchToLevel(next, x, 0);
        return next->key == x;
    }
    std::string name(){
        return "Shavit SkipTrie";
    }
};
