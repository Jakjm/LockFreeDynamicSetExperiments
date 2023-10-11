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
#include "../common.h"
#include "ListNode.h"
#pragma once
using std::string;

#define testStats

//An extension of Fomitchev and Ruppert's linked list that allows concurrent insertions of the same node.
//Additional status used when a ListNode's successor is an insert descriptor node.
const uint64_t InsFlag = 3; 

//Definition of the Insert Descriptor Node object.
struct InsertDescNode{
    std::atomic<ListNode*> newNode;
    std::atomic<ListNode*> next;
    std::atomic<uint64_t> seqNum; //Sequence number.
    InsertDescNode():  newNode(nullptr), next(nullptr), seqNum(0){

    }
};

//The first 3 bits of a ListNode's successor pointer are used for the states...
//If the state is InsFlag or NotifFlag, then the information to access a descriptor node is contained as follows:
const uint64_t PROC_MASK = 0x0000000000000FF0; //Process ID contained within lower 8 bits
const uint64_t SEQ_MASK =  0xFFFFFFFFFFFFF000; //Sequence # contained within upper 52 bits


//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert
//compare is the function used to compare the nodes of the linked list
template <int(*compare)(ListNode*, ListNode*)>
class LinkedList_FRE {
    public:
        ListNode tail, head; //Head, tail of the linked list. 
    public:
        InsertDescNode descs[NUM_THREADS];
        LinkedList_FRE() : tail(), head(){
            head.successor.store((uintptr_t)&tail);
        }
        ~LinkedList_FRE(){ 
        }

        uintptr_t helpInsert(ListNode *prev, uint64_t seqNum,  uint64_t procID){
            InsertDescNode *desc = &descs[procID];
            ListNode *newNode = desc->newNode;
            ListNode *next = desc->next;
            if(seqNum != desc->seqNum){
                return prev->successor.load();
            }

            //Attempt to initialize newNode....
            uintptr_t result = 0;
            newNode->successor.compare_exchange_strong(result, (uintptr_t)next);

            uint64_t expected = (seqNum << 12) + (procID << 4) + InsFlag;
            result = expected; 
            //If insert node was marked....
            if((result & STATUS_MASK) == Marked){ 
                //newNode has already been removed.
                //Attempt to CAS to remove descriptor.
                prev->successor.compare_exchange_strong(result, (uintptr_t)next);
                if(result == expected){
                    return (uintptr_t)next;
                }
                else{
                    return result;
                }
            }
            else{
                //Attempt to complete insertion of insert node.
                prev->successor.compare_exchange_strong(result, (uintptr_t)newNode);
                if(result == expected){
                    return (uintptr_t)newNode;
                }
                else{
                    return result;
                }
            }
        }
        
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
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
                else if(state == InsFlag){ //Help with insertion if prev.next points to an InsertDescNode....
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(delNode,  seqNum, procID);
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
            ListNode *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;

            //This is the descriptor for this thread.
            InsertDescNode *desc = &descs[threadID];
            desc->seqNum++; //Increment the sequence number....
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->newNode = node;
            while(next != (uintptr_t)node){
                if(state == Normal){
                    if(compNode((ListNode*)next,node) <= 0){ //node should be placed further along in the list if next <= node
                        curr = (ListNode*)next;
                        succ = curr->successor;
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                        continue;
                    }
                    if((node->successor & STATUS_MASK) == Marked)return;
                    desc->next = (ListNode*)next; //Set the next of the insert descriptor node.
                    succ = next;

                    uint64_t newVal = (seqNum << 12) + (threadID << 4) + InsFlag;
                    curr->successor.compare_exchange_strong(succ, (uintptr_t)newVal);
                    if(succ == next){ //If the CAS succeeded....
                        helpInsert(curr, seqNum, threadID);
                        return;
                    }
                    //Read next and state from curr.successor.
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == InsFlag){
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr, seq, proc);
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
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr, seq, proc);
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
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;

            
            while(state == InsFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                InsertDescNode *desc = &descs[proc];
                next = (uintptr_t)(ListNode*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
                
                succ = node->successor; //Read node->successor again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a ListNode....

            //If the following ListNode was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (ListNode*)next; //Otherwise, return the following ListNode.
        }


        ListNode *next(ListNode *node, uint64_t state){
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;

            
            while(state == InsFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                InsertDescNode *desc = &descs[proc];
                next = (uintptr_t)(ListNode*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
                
                succ = node->successor; //Read node->successor again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a ListNode....

            //If the following ListNode was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (ListNode*)next; //Otherwise, return the following ListNode.
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
            while(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};