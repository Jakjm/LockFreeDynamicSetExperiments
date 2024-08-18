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



struct alignas(64) KeyNode : public ReclaimableBase{
    int64_t key;
    std::atomic<uintptr_t> successor; //Contains <next, state>. The state is contained within the lowest 3 bits of the pointer.
    KeyNode *backlink;  //A pointer to the ListNode preceeding this node before it is removed.
    volatile char padding[64 - 3*sizeof(uintptr_t)];
    KeyNode(int64_t k = 0) : key(k), successor(0), backlink(nullptr){

    }
};

//Structure used to store pointers to KeyNodes that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated KeyNode can be used again.
struct KeyNodePool{
    KeyNode *keyNode;
    volatile char padding[64-sizeof(KeyNode*)];
    KeyNodePool(): keyNode(new KeyNode()){

    }
    ~KeyNodePool(){
        delete keyNode;
    }
};
KeyNodePool keyNodePool[MAX_THREADS];
//An implementation of Eric Ruppert and Michhail Fomitchev's Lock-Free Linked List
//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert
class LinkedListSet : public DynamicSet {
    private:
        KeyNode head, tail; //Head, tail of the linked list. 
    public:
        LinkedListSet() : head(-1), tail(INT64_MAX) {
            head.successor.store((uintptr_t)&tail);
        }
        ~LinkedListSet(){ 
            //listRecordMgr.startOp(threadID);

            //Start iterating through the list from head.
            //Reclaim any keynodes that remain in the list.
            KeyNode *curr = &head;
            intptr_t succ = curr->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);
            //While the next node is not the tail node....
            while(next != &tail){
                curr = next;
                succ = curr->successor;
                next = (KeyNode*)(succ & NEXT_MASK);
                delete curr;
                //listRecordMgr.deallocate(threadID, curr); //Reclaim the node...
            }

            //listRecordMgr.endOp(threadID);
            //for(int i = 0;i < MAX_THREADS;++i){
                //listRecordMgr.deinitThread(i);
            //}
        }
        //Prefill the linked list to contain numValues values from a universe of universeSize keys.
        //Much more efficient then just starting a search every time to prefill.
        void prefill(uint64_t universeSize, uint64_t numValues){
            std::set<int64_t> valSet;
            while(valSet.size() < numValues){
                int64_t key = rng(universeSize);
                valSet.insert(key);
            }
            KeyNode *curr = &head;
            for(int64_t val : valSet){
                KeyNode *newNode = new KeyNode(val);
                newNode->successor = (uintptr_t)&tail;
                curr->successor = (uintptr_t)newNode;
                curr = newNode;
            }
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(KeyNode *prev, KeyNode *delNode){
            KeyNode *next = (KeyNode*)((delNode->successor) & NEXT_MASK);
            //KeyNode *next = (KeyNode*)(delNode->successor & NEXT_MASK);
            uintptr_t expected = ((uintptr_t)delNode) + DelFlag;
            uintptr_t result = expected;
            prev->successor.compare_exchange_strong(result, (uintptr_t)next);

            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(KeyNode *prev, KeyNode *delNode){
            delNode->backlink = prev;
            uintptr_t succ = delNode->successor.load(); //The value of delNode's successor pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(delNode, (KeyNode*)next);
                }
                else{ //Attempt to mark the node if the status was normal...
                    uintptr_t markedSuccessor = (uintptr_t)next + Marked;
                    succ = next;
                    delNode->successor.compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                    if(succ == next)break; //The CAS succeeded!
                }
                state = succ & STATUS_MASK;
                next = succ & NEXT_MASK;
            }
            succ = helpMarked(prev, delNode);
            return succ;
        }

        bool insert(int64_t key){
            debra.startOp();
            KeyNode *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            KeyNode *newNode = keyNodePool[threadID].keyNode;
            newNode->key = key;
            while(((KeyNode*)next)->key != key){
                if(state == Normal){
                    if(key > ((KeyNode*)next)->key){ //node should be placed further along in the list if next <= key
                        curr = (KeyNode*)next;
                        succ = curr->successor;
                    }     
                    else{ //key < next->key
                        newNode->successor = (uintptr_t)next;
                        succ = (uintptr_t)next;
                        curr->successor.compare_exchange_strong(succ, (uintptr_t)newNode);
                        if(succ == (uintptr_t)next){ //If the CAS succeeded, node has been successfully inserted and the operation can stop.
                            keyNodePool[threadID].keyNode = new KeyNode(); //Need a new node for pool...
                            debra.endOp();
                            return true;
                        }
                    }
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (KeyNode*)next);
                }
                else{
                    KeyNode *prev = (KeyNode*)curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && next == (uintptr_t)curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                //Read next and state from curr.successor.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            debra.endOp();
            return false;
        }
        bool remove(int64_t key){
            debra.startOp();
            KeyNode *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            
            while(((KeyNode*)next)->key <= key){
                if(state == Normal){
                    if(((KeyNode*)next)->key < key){ //Advance if next's key is smaller than key
                        curr = (KeyNode*)next;
                        succ = curr->successor;
                    }
                    else{ //next->key = key
                        succ = (uintptr_t)next;
                        curr->successor.compare_exchange_strong(succ, ((uintptr_t)next) + DelFlag);
                        if(succ == (uintptr_t)next){
                            helpRemove(curr, (KeyNode*)next);
                            debra.reclaimLater((KeyNode*)next);
                            //listRecordMgr.retire(threadID, next); //Retire a node upon successfully giving its predecessor del flag.
                            debra.endOp();
                            return true;
                        }
                    }
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (KeyNode*)next);
                }
                else{
                    KeyNode *prev = curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && next == (uintptr_t)curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            debra.endOp();
            return false;
        }
        bool search(int64_t key){
            bool result;
            debra.startOp();
            KeyNode *curr = &head;
            uintptr_t succ = curr->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);
            //Invariant curr->key < next->key
            //next was equal to curr->next
            while(next->key < key){
                curr = next;
                succ = curr->successor;
                next = (KeyNode*)(succ & NEXT_MASK);
            }
            //next->key >= key
            result = (next->key == key);
            debra.endOp();
            return result;
        }
        int64_t predecessor(int64_t key){
            int64_t pred = -1;
            debra.startOp();

            KeyNode *curr = &head;
            uintptr_t succ = curr->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);
            //Invariant curr->key < next->key
            //next was equal to curr->next
            while(next->key < key){
                curr = next;
                succ = curr->successor;
                next = (KeyNode*)(succ & NEXT_MASK);
            }
            //next->key >= key
            pred = curr->key;
            debra.endOp();
            return pred;
        }
        string name(){
            return "Fomitchev/Ruppert ListSet";
        }

        //List traversal algorithms here: 
        //Returns the head of the linked list, or null if the list is empty...
        KeyNode *first(){
            return next(&head);
        }
        KeyNode *next(KeyNode *node){            
            uintptr_t succ = node->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);
            if(next == &tail){
                return nullptr;
            }
            else return (KeyNode*)next;
        }
        KeyNode *next(KeyNode *node, uint64_t &state){            
            uintptr_t succ = node->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);
            state = succ & STATUS_MASK;
            if(next == &tail){
                return nullptr;
            }
            else return (KeyNode*)next;
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
            std::ostringstream stream;

            uint64_t status;
            KeyNode *node = next(&head, status);
            stream << "{";
            while(node){
                stream << "<" << node << ", Key:" << node->key << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};