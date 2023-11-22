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
#include "../setbench/common/recordmgr/record_manager.h"
#pragma once
using std::string;

const int MAX_LEVEL = 20;

struct KeyTower {
    int64_t key;
    std::atomic<uintptr_t> successor[MAX_LEVEL]; //Contains <next, state>. The state is contained within the lowest 3 bits of the pointer.
    std::atomic<KeyTower*> backlink[MAX_LEVEL];  //A pointer to the ListNode preceeding this node before it is removed.
    KeyTower(int64_t k) : key(k){
    }
};


//TODO memory reclamation
record_manager<reclaimer_debra<int>, allocator_new<int>, pool_none<int>, KeyTower> skipRecordMgr(NUM_THREADS);

//Structure used to store pointers to insert/delete nodes that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated insert/delete node can be used again.
struct KeyTowerPool{
    KeyTower *keyNode;
    KeyTowerPool(): keyNode(nullptr){

    }
};
KeyTowerPool keyTowerPool[NUM_THREADS];

#define reuse 1 //If reuse is defined, update nodes that are not inserted into the trie will be reused.

//An implementation of Eric Ruppert and Michhail Fomitchev's Lock-Free Skip List
class SkipListSet : public DynamicSet {
    private:
        KeyTower tail, head; //Head, tail of the linked list. 
    public:
        SkipListSet() : tail(INT64_MAX), head(-1){
            for(int level = 0;level < MAX_LEVEL;++level){
                head.successor[level].store((uintptr_t)&tail);
            }
        }
        void initThread(){
            skipRecordMgr.initThread(threadID);
        }
        ~SkipListSet(){ 
            skipRecordMgr.initThread(threadID);
            skipRecordMgr.startOp(threadID);
            for(int i = 0;i < NUM_THREADS;++i){
                KeyTower * volatile keyN = keyTowerPool[i].keyNode;
                if(keyN)skipRecordMgr.deallocate(threadID, keyN);
            }

            //Start iterating through the list from head.
            //Reclaim any keynodes that remain in the list.
            KeyTower *curr = &head;

            intptr_t succ = curr->successor[0];
            KeyTower *next = (KeyTower*)(succ & NEXT_MASK);
            //While the next node is not the tail node....
            while(next != &tail){
                curr = next;
                succ = curr->successor[0];
                next = (KeyTower*)(succ & NEXT_MASK);
                skipRecordMgr.deallocate(threadID, curr); //Reclaim the node...
            }

            skipRecordMgr.endOp(threadID);
            for(int i = 0;i < NUM_THREADS;++i){
                skipRecordMgr.deinitThread(i);
            }
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(KeyTower *prev, KeyTower *delNode, int level){
            KeyTower *next = (KeyTower*)(delNode->successor[level] & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->successor[level].compare_exchange_strong(result, (uintptr_t)next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(KeyTower *prev, KeyTower *delNode, int level){
            delNode->backlink[level] = prev;
            uintptr_t succ = delNode->successor[level].load(); //The value of delNode's successor pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(delNode, (KeyTower*)next, level);
                }
                else{ //Attempt to mark the node if the status was normal...
                    uintptr_t markedSuccessor = (uintptr_t)next + Marked;
                    succ = next;
                    delNode->successor[level].compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                    if(succ == next)break; //The CAS succeeded!
                }
                state = succ & STATUS_MASK;
                next = succ & NEXT_MASK;
            }
            succ = helpMarked(prev, delNode, level);
            return succ;
        }

        void insert(int64_t key){

            KeyTower *newTower;
            #ifdef reuse
            if(keyTowerPool[threadID].keyNode){
                newTower = keyTowerPool[threadID].keyNode;
                newTower->key = key;
            }
            else{
                newTower = skipRecordMgr.allocate<KeyTower>(threadID, key); 
            } 
            #else
            newNode = listRecordMgr.allocate<KeyTower>(threadID, key); 
            #endif

            int numLevels = 0;
            while(randomNum(1) == 1 && numLevels < MAX_LEVEL){
                ++numLevels;
            }
            int level;
            for(int level = 0;level < numLevels; ++level){
                KeyTower *curr = &head;
                uintptr_t succ = curr->successor[level];
                KeyTower *next = (KeyTower*)(succ & NEXT_MASK);
                uint64_t state = succ & STATUS_MASK;
                while(key != next->key){
                    if(state == Normal){
                        if(key > next->key){ //node should be placed further along in the list if next <= key
                            curr = (KeyTower*)next;
                            succ = curr->successor[level];
                        }     
                        else{
                            newTower->successor[level] = (uintptr_t)next;
                            succ = (uintptr_t)next;
                            curr->successor[level].compare_exchange_strong(succ, (uintptr_t)newTower);
                            if(succ == (uintptr_t)next){ //If the CAS succeeded, node has been successfully inserted and the operation can stop.
                                #ifdef reuse
                                keyTowerPool[threadID].keyNode = nullptr; //Do not reuse keynode...
                                #endif
                                break;
                            }
                        }
                    }
                    else if(state == DelFlag){
                        succ = helpRemove(curr, (KeyTower*)next, level);
                    }
                    else{
                        KeyTower *prev = (KeyTower*)curr->backlink[level];
                        succ = prev->successor[level];
                        next = (KeyTower*)(succ & NEXT_MASK);
                        state = succ & STATUS_MASK;
                        if(next == curr){ //Help remove curr from the list.
                            succ = helpMarked(prev, curr, level);
                        }
                        curr = prev;
                    }
                    //Read next and state from curr.successor.
                    next = (KeyTower*)(succ & NEXT_MASK);
                    state = succ & STATUS_MASK;
                }
                    
            }
            #ifdef reuse 
            keyTowerPool[threadID].keyNode = newTower; //This node can be reused for subsequent insert operations.
            #else 
            listRecordMgr.deallocate(threadID, newNode); //Reclaim node every time...
            #endif 
        }
        // void remove(int64_t key){
        //     KeyTower *curr = &head;
        //     uintptr_t succ = curr->successor;
        //     KeyTower *next = (KeyTower*)(succ & NEXT_MASK);
        //     uint64_t state = succ & STATUS_MASK;
        //     while(next->key <= key){
        //         if(state == Normal){
        //             if(next->key < key){ //Advance if next's key is smaller than key
        //                 curr = (KeyTower*)next;
        //                 succ = curr->successor;
        //             }
        //             else{ //next->key = key
        //                 succ = (uintptr_t)next;
        //                 curr->successor.compare_exchange_strong(succ, ((uintptr_t)next) + DelFlag);
        //                 if(succ == (uintptr_t)next){
        //                     helpRemove(curr, next);
        //                     listRecordMgr.retire(threadID, next); //Retire a node upon successfully giving its predecessor del flag.
        //                     return;
        //                 }
        //             }
        //         }
        //         else if(state == DelFlag){
        //             succ = helpRemove(curr, next);
        //         }
        //         else{
        //             KeyTower *prev = (KeyTower*)curr->backlink;
        //             succ = prev->successor;
        //             next = (KeyTower*)(succ & NEXT_MASK);
        //             state = succ & STATUS_MASK;
        //             if(next == curr){ //Help remove curr from the list.
        //                 succ = helpMarked(prev, curr);
        //             }
        //             curr = prev;
        //         }
        //         next = (KeyTower*)(succ & NEXT_MASK);
        //         state = succ & STATUS_MASK;
        //     }
        // }
        // bool search(int64_t key){
        //     KeyTower *curr = &head;
        //     uintptr_t succ = curr->successor;
        //     KeyTower *next = (KeyTower*)(succ & NEXT_MASK);
        //     //Invariant curr->key < next->key
        //     //next was equal to curr->next
        //     while(next->key < key){
        //         curr = next;
        //         succ = curr->successor;
        //         next = (KeyTower*)(succ & NEXT_MASK);
        //     }
        //     //next->key >= key
        //     return next->key == key;
        // }
        // int64_t predecessor(int64_t key){
        //     KeyTower *curr = &head;
        //     uintptr_t succ = curr->successor;
        //     KeyTower *next = (KeyTower*)(succ & NEXT_MASK);
        //     //Invariant curr->key < next->key
        //     //next was equal to curr->next
        //     while(next->key < key){
        //         curr = next;
        //         succ = curr->successor;
        //         next = (KeyTower*)(succ & NEXT_MASK);
        //     }
        //     //next->key >= key
        //     return curr->key;
        // }
};