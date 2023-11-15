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



struct KeyNode {
    int64_t key;
    std::atomic<uintptr_t> successor; //Contains <next, state>. The state is contained within the lowest 3 bits of the pointer.
    std::atomic<KeyNode*> backlink;  //A pointer to the ListNode preceeding this node before it is removed.
    KeyNode(int64_t k) : key(k), successor(0), backlink(nullptr){

    }
};

//TODO memory reclamation
record_manager<reclaimer_debra<int>, allocator_new<int>, pool_none<int>, KeyNode> listRecordMgr(NUM_THREADS);

//An implementation of Eric Ruppert and Michhail Fomitchev's Lock-Free Linked List
//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert
//With an additional extension.
//compare is the function used to compare the nodes of the linked list
class LinkedListSet : public DynamicSet {
    private:
        KeyNode tail, head; //Head, tail of the linked list. 
    public:
        LinkedListSet() : tail(INT64_MAX), head(-1){
            head.successor.store((uintptr_t)&tail);
        }
        ~LinkedListSet(){ 
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(KeyNode *prev, KeyNode *delNode){
            KeyNode *next = (KeyNode*)(delNode->successor & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
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

        void insert(int64_t key){
            KeyNode *curr = &head;
            uintptr_t succ = curr->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);

            uint64_t state = succ & STATUS_MASK;
            KeyNode *newNode = listRecordMgr.allocate<KeyNode>(threadID, key); //TODO reuse node if node is not node not used....
            while(key != next->key){
                if(state == Normal){
                    if(key > next->key){ //node should be placed further along in the list if next <= key
                        curr = (KeyNode*)next;
                        succ = curr->successor;
                    }     
                    else{
                        newNode->successor = (uintptr_t)next;
                        succ = (uintptr_t)next;
                        curr->successor.compare_exchange_strong(succ, (uintptr_t)newNode);
                        if(succ == (uintptr_t)next){ //If the CAS succeeded, node has been successfully inserted and the operation can stop.
                            return;
                        }
                    }
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (KeyNode*)next);
                }
                else{
                    KeyNode *prev = (KeyNode*)curr->backlink;
                    succ = prev->successor;
                    next = (KeyNode*)(succ & NEXT_MASK);
                    state = succ & STATUS_MASK;
                    if(next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                //Read next and state from curr.successor.
                next = (KeyNode*)(succ & NEXT_MASK);
                state = succ & STATUS_MASK;
            }
            listRecordMgr.deallocate(threadID, newNode); //TODO reuse node instead of doing this....
        }
        void remove(int64_t key){
            KeyNode *curr = &head;
            uintptr_t succ = curr->successor;
            KeyNode *next = (KeyNode*)(succ & NEXT_MASK);
            uint64_t state = succ & STATUS_MASK;
            while(next->key <= key){
                if(state == Normal){
                    if(next->key < key){ //Advance if next's key is smaller than key
                        curr = (KeyNode*)next;
                        succ = curr->successor;
                    }
                    else{ //next->key = key
                        succ = (uintptr_t)next;
                        curr->successor.compare_exchange_strong(succ, ((uintptr_t)next) + DelFlag);
                        if(succ == (uintptr_t)next){
                            helpRemove(curr, next);
                            listRecordMgr.retire(threadID, next);
                            return;
                        }
                    }
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, next);
                }
                else{
                    KeyNode *prev = (KeyNode*)curr->backlink;
                    succ = prev->successor;
                    next = (KeyNode*)(succ & NEXT_MASK);
                    state = succ & STATUS_MASK;
                    if(next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                next = (KeyNode*)(succ & NEXT_MASK);
                state = succ & STATUS_MASK;
            }
        }
        bool search(int64_t key){
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
            return next->key == key;
        }
        int64_t predecessor(int64_t key){
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
            return curr->key;
        }

        void cleanup(){
            
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
        void printList(std::string (*nodeToString)(KeyNode*)){
            std::ostringstream stream;

            uint64_t status;
            KeyNode *node = next(&head, status);
            stream << "{";
            while(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};