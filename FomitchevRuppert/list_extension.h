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
#include "../setbench/common/recordmgr/record_manager.h"
#include "../common.h"
#include "ListNode.h"
#pragma once
using std::string;

#define testStats

//An extension of Fomitchev and Ruppert's linked list that allows concurrent insertions of the same node.
//Possible statuses of a successor field.
uint64_t Normal = 0;
uint64_t DelFlag = 1;
uint64_t Marked = 2; 
uint64_t InsFlag = 3; 

uint64_t NEXT_MASK = -4; //62 1s, 2 0s    = FFFF FFFF FFFF FFFC
uint64_t STATUS_MASK = 3; //62 0s, 2 1s = 0000 0000 0000 0003
class ListNode;

class InsertDescNode{
    public:
    ListNode *newNode;
    ListNode *nextNode;
    InsertDescNode(ListNode *ins) : newNode(ins), nextNode(nullptr) {

    }
};


//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert
//With an additional extension.
//Key is the type of keys stored inside the list.
//compare is the function used to compare the keys of the linked list, in order to sort the list.
template <int(*compare)(ListNode*, ListNode*)>
class LinkedList_FRE {
    typedef record_manager<reclaimer_debra<>, allocator_new<>, pool_none<>, InsertDescNode> recordMgr_t;
    public:
        ListNode tail, head; //Head, tail of the linked list. 
        recordMgr_t *const recordMgr;
      
        //Head has -inf as a key, tail has + inf as a key.
    public:
        LinkedList_FRE() : tail(), head(), recordMgr(new recordMgr_t(NUM_THREADS)){
            head.successor.store((uintptr_t)&tail);
        }
        ~LinkedList_FRE(){ 
            //Deinitialize all threads.
            for(int i = 0;i < NUM_THREADS;++i){
                recordMgr->deinitThread(i);
            }
            delete recordMgr;
        }

        uintptr_t helpInsert(ListNode *prev, InsertDescNode *desc){
            uintptr_t expected = 0;
            uintptr_t result = expected;
            desc->newNode->successor.compare_exchange_strong(result, (uintptr_t)desc->nextNode);

            //If insert node was marked....
            if((result & STATUS_MASK) == Marked){ 
                expected = (uintptr_t)desc + InsFlag;
                result = expected;
                //newNode has already been removed.
                //Attempt to CAS to remove descriptor.
                prev->successor.compare_exchange_strong(result, (uintptr_t)desc->nextNode);
                if(result == expected){
                    //TODO retire desc
                }
            }
            else{
                expected = (uintptr_t)desc + InsFlag;
                result = expected;
                //Attempt to complete insertion of insert node.
                prev->successor.compare_exchange_strong(result, (uintptr_t)desc->newNode);
                if(result == expected){
                    //TODO retire desc
                }
            }
            return result;
        }

        uintptr_t helpMarked(ListNode *prev, ListNode *delNode){
            ListNode *next = (ListNode*)((uintptr_t)delNode->successor & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->successor.compare_exchange_strong(result, (uintptr_t)next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition, prev.successor was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(ListNode *prev, ListNode *delNode){
            delNode->backlink = prev;
            uintptr_t succ = delNode->successor.load(); //The value of delNode's successor pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(delNode, (ListNode*)next);
                }
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    InsertDescNode *desc = (InsertDescNode*)next;
                    succ = helpInsert(delNode, desc);
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

        //Used to compare two nodes in the list
        //Returns 1 if n1 must be placed after n2
        //Returns 0 if n1 may be placed after or before n2.
        //Returns -1 if n1 must be placed before n2
        inline int __attribute__((always_inline)) compNode(ListNode *n1, ListNode *n2){
            if(n1 == &tail)return 1;
            else return compare(n1,n2);
        }

        void insert(ListNode *node){
            if((node->successor & STATUS_MASK) == Marked)return;

            recordMgr->initThread(threadID());
            auto guard = recordMgr->getGuard(threadID());

            ListNode *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            InsertDescNode *newDesc = recordMgr->template allocate<InsertDescNode>(threadID(), node);
            while(next != (uintptr_t)node){
                if(state == Normal){
                    if(compNode((ListNode*)next,node) <= 0){
                        curr = (ListNode*)next;
                        succ = curr->successor;
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                        continue;
                    }
                    if((node->successor & STATUS_MASK) == Marked)return;
                    newDesc->nextNode = (ListNode*)next;
                    succ = next;
                    curr->successor.compare_exchange_strong(succ, (uintptr_t)newDesc + InsFlag);
                    if(succ == next){ //If the CAS succeeded....
                        helpInsert(curr, (InsertDescNode*)newDesc);
                        return;
                    }
                    //Read next and state from curr.successor.
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == InsFlag){
                    succ = helpInsert(curr, (InsertDescNode*)next);
                    if(((InsertDescNode*)next)->newNode == node)return;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (ListNode*)next);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else{
                    ListNode *prev = curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if((ListNode*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                    curr = prev;
                }
            }
        }
        void remove(ListNode *node){
            recordMgr->initThread(threadID());
            auto guard = recordMgr->getGuard(threadID());

            ListNode *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            while(1){
                if(state == Normal){
                    if(compNode((ListNode*)next, node) > 0)return;
                    if((ListNode*)next != node){ //Advance...
                        curr = (ListNode*)next;
                        succ = curr->successor;
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                    else{
                        succ = (uintptr_t)node;
                        curr->successor.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                }
                else if(state == InsFlag){
                    succ = helpInsert(curr, (InsertDescNode*)next);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(compNode((ListNode*)next, node) > 0){
                    return;
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (ListNode*)next);
                    if((ListNode*)next == node)return;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else{
                    ListNode *prev = curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(next == (uintptr_t)curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                    curr = prev;
                }
            }
        }

        //List traversal algorithms here: 

        //Returns the head of the linked list, or null if the list is empty...
        ListNode *first(){
            return next(&head);
        }
        ListNode *next(ListNode *node){
            recordMgr->initThread(threadID());
            auto guard = recordMgr->getGuard(threadID());
            
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            //If successor was pointing to an insert descriptor, 
            //return the ListNode following the descriptor.
            if(state == InsFlag)return ((InsertDescNode*)next)->nextNode;
            //Otherwise get successor, removing mark/flag status.
            else return (ListNode*)next;
        }
        ListNode *next(ListNode *node, uint64_t &state){
            recordMgr->initThread(threadID());
            auto guard = recordMgr->getGuard(threadID());
            
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            
            //If successor was pointing to an insert descriptor, 
            //return the ListNode following the descriptor.
            if(state == InsFlag)return ((InsertDescNode*)next)->nextNode;
            //Otherwise get successor, removing mark/flag status.
            else return (ListNode*)next;
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
            else if(status == InsFlag){
                return 'I';
            }
            return ' ';
        }

        //Thread safe way of printing list
        void printList(std::string (*nodeToString)(ListNode*)){
            std::ostringstream stream;

            uint64_t status;
            ListNode *node = next(&head, status);
            stream << "{";
            if(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
                while(node != nullptr){
                    stream << " - <" << nodeToString(node) << ", " << stat_to_char(status) << ">";
                    node = next(node);
                }
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};