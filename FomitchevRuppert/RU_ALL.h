#include <cmath>
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
#include "list_extension.h"
#include "../setbench/common/recordmgr/record_manager.h"
#include "../common.h"
#include "../trieNodeTypes.h"
#pragma once
using std::string;

#define testStats

//Customized version of the linked list extension that is specifically used for the RUALL of Jeremy's Trie.
const uint64_t NotifFlag = 4; 

struct DescNode{
    std::atomic<uintptr_t> other;
    std::atomic<RU_ALL_Node*> next;
    std::atomic<int64_t> seqNum; //Sequence number.
    DescNode(): seqNum(0), next(nullptr), other((uintptr_t)nullptr){

    }
};

//The first 3 bits of a pointer are used for the states...
//If the state is InsFlag or NotifFlag, then the information to access a descriptor node is contained as follows:
const uint64_t PROC_MASK = 0x0000000000000FF0; //Process ID contained within lower 8 bits
const uint64_t SEQ_MASK =  0xFFFFFFFFFFFFF000; //Sequence # contained within upper 52 bits

//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert.
//compare is the function used to compare the nodes of the linked list
template <int(*compare)(RU_ALL_Node*, RU_ALL_Node*)>
class RU_ALL_TYPE {
    public:
        RU_ALL_Node tail, head; //Head, tail of the linked list. 
        DescNode descs[NUM_THREADS];
        //DescManager *const descMgr; //Record manager used to allocate insert_descriptor nodes.
    public:
        RU_ALL_TYPE() : tail(), head(){
            head.successor.store((uintptr_t)&tail);
            for(int i = 0;i < NUM_THREADS;++i){
                descs[i].seqNum = 0;
            }
        }
        ~RU_ALL_TYPE(){ 
            //Deinitialize all threads.

        }

        uintptr_t helpNotify(RU_ALL_Node *prev,  uint64_t seqNum,  uint64_t procID){
            DescNode *desc = &descs[procID];
            PredecessorNode *pNode = (PredecessorNode*)(uintptr_t)desc->other;
            RU_ALL_Node *next = desc->next;
            if(seqNum != desc->seqNum){
                return prev->successor.load();
            }

            UpdateNode *expected;
            if(prev == &head)expected = &INFINITY_THRES;
            else expected = (UpdateNode*)prev;
            //Update notify threshold.
            pNode->notifyThreshold.compare_exchange_strong(expected, (UpdateNode*)next);
            
            uint64_t result = (seqNum << 12) + (procID << 4) + NotifFlag;
            prev->successor.compare_exchange_strong(result, (uintptr_t)next);
            

            //Return contents of prev->successor immediately following CAS...
            if(result == (uintptr_t)desc + NotifFlag){
                return (uintptr_t)next;
            }
            else{
                return result;
            } 
        }

        uintptr_t helpInsert(RU_ALL_Node *prev, uint64_t seqNum,  uint64_t procID){
            DescNode *desc = &descs[procID];
            RU_ALL_Node *newNode = (RU_ALL_Node*)(uintptr_t)desc->other;
            RU_ALL_Node *next = desc->next;
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
        uintptr_t helpMarked(RU_ALL_Node *prev, RU_ALL_Node *delNode){
            RU_ALL_Node *next = (RU_ALL_Node*)((uintptr_t)delNode->successor & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->successor.compare_exchange_strong(result, (uintptr_t)next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition, prev.successor was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(RU_ALL_Node *prev, RU_ALL_Node *delNode){
            delNode->backlink = prev;
            uintptr_t succ = delNode->successor.load(); //The value of delNode's successor pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(delNode, (RU_ALL_Node*)next);
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(delNode,  seqNum, procID);
                }
                else if(state == NotifFlag){ //prev.next was a Notify Descriptor Node. Help with notification.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with setting notify threshold
                    succ = helpNotify(delNode,  seqNum, procID);
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
        //Returns a positive value if n1 must be later in the list than n2
        inline int __attribute__((always_inline)) compNode(RU_ALL_Node *n1, RU_ALL_Node *n2){
            if(n1 == &tail)return 1;
            else return compare(n1,n2);
        }

        void insert(RU_ALL_Node *node){
            if((node->successor & STATUS_MASK) == Marked)return;

            RU_ALL_Node *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            
            //This is the descriptor for this thread.
            DescNode *desc = &descs[threadID()];
            desc->seqNum++; //Increment the sequence number....
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->other = (uint64_t)node;
            while(next != (uintptr_t)node){
                if(state == Normal){
                    if(compNode((RU_ALL_Node*)next,node) <= 0){ //node should be placed further along in the list if next <= node
                        curr = (RU_ALL_Node*)next;
                        succ = curr->successor;
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                        continue;
                    }
                    if((node->successor & STATUS_MASK) == Marked){
                        return;
                    }
                    desc->next = (RU_ALL_Node*)next; //Set the next of the insert descriptor node.
                    succ = next;

                    uint64_t newVal = (seqNum << 12) + (threadID() << 4) + InsFlag;
                    curr->successor.compare_exchange_strong(succ, (uintptr_t)newVal);
                    if(succ == next){ //If the CAS succeeded....
                        helpInsert(curr, seqNum, threadID());
                        return;
                    }
                    //Read next and state from curr.successor.
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == InsFlag){
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr,seq, proc);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == NotifFlag){
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpNotify(curr, seq, proc);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (RU_ALL_Node*)next);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else{
                    RU_ALL_Node *prev = curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if((RU_ALL_Node*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                    curr = prev;
                }
            }
        }
        void remove(RU_ALL_Node *node){
            RU_ALL_Node *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            while(1){
                if(state == Normal){
                    if(compNode((RU_ALL_Node*)next, node) > 0)return;
                    if((RU_ALL_Node*)next != node){ //Advance...
                        curr = (RU_ALL_Node*)next;
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
                else if(state == NotifFlag){
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpNotify(curr, seq, proc);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(compNode((RU_ALL_Node*)next, node) > 0){
                    return;
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (RU_ALL_Node*)next);
                    if((RU_ALL_Node*)next == node)return;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else{
                    RU_ALL_Node *prev = curr->backlink;
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

        //Special RU-ALL traversal algorithms here: 
        //Returns the head of the linked list, or null if the list is empty...
        RU_ALL_Node *first(PredecessorNode *pNode){
            return next(pNode, &head);
        }

        //Returns the node following node, or null if bottom was following node.
        RU_ALL_Node *next(PredecessorNode *pNode, RU_ALL_Node *node){
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;


            DescNode *desc = &descs[threadID()];
            desc->seqNum++; //Increment the sequence number....
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->other = (uint64_t)pNode;
            while(next != (uintptr_t)&tail){ //Continue while next != tail...
                if(state == Normal){
                    desc->next = (RU_ALL_Node*)next;
                    succ = next;

                    uint64_t newVal = (seqNum << 12) + (threadID() << 4) + NotifFlag;
                    node->successor.compare_exchange_strong(succ, newVal);
                    if(succ == next){
                        helpNotify(node, seqNum, threadID());
                        return (RU_ALL_Node*)next; //CAS succeeded, therefore pNode->notifyThreshold was updated to next 
                    } 
                }
                else if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(node, (RU_ALL_Node*)next);
                }
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(node, seq, proc);
                }
                else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpNotify(node, seq, proc);
                }
                else{ //node is marked, so node.next is permanently equal to next. No need to use a descriptor node!
                    pNode->notifyThreshold = (UpdateNode*)next;
                    return (RU_ALL_Node*)next;
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            return nullptr;
        }
        char stat_to_char(uint64_t status){
            if(status == Normal){
                return ' ';
            }
            else if(status == NotifFlag){
                return 'F';
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

        RU_ALL_Node *first(){
            return next(&head);
        }
        RU_ALL_Node *next(RU_ALL_Node *node){
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;

            
            while(state == InsFlag || state == NotifFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                DescNode *desc = &descs[proc];
                next = (uintptr_t)(RU_ALL_Node*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid descriptor node following node...
                
                succ = node->successor; //Read node->successor again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a ListNode....

            //If the following RU_ALL_Node was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (RU_ALL_Node*)next; //Otherwise, return the following RU_ALL_Node.
        }

        RU_ALL_Node *next(RU_ALL_Node *node, int64_t state){
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;

            
            while(state == InsFlag || state == NotifFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                DescNode *desc = &descs[proc];
                next = (uintptr_t)(RU_ALL_Node*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid descriptor node following node...
                
                succ = node->successor; //Read node->successor again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }

            //If the following RU_ALL_Node was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (RU_ALL_Node*)next; //Otherwise, return the following RU_ALL_Node.
        }

        //Thread safe way of printing list
        void printList(std::string (*nodeToString)(RU_ALL_Node*)){
            
            std::ostringstream stream;

            uint64_t status;
            RU_ALL_Node *node = next(&head, status);
            stream << (std::to_string(head.successor) + " " + std::to_string((uintptr_t)&tail)) << "{";
            while(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};