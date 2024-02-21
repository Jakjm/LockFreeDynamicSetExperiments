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
#include "../trieNodeTypes.h"
#pragma once
using std::string;

#define testStats

//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert
class UALL_Type {
    public:
        UpdateNode  head, tail; //Head, tail of the linked list. 
    public:
        InsertDescNode descs[MAX_THREADS]; //Insert descriptor nodes for each process
        UALL_Type(): head(INT64_MIN, INS), tail(INT64_MAX,INS){
            head.succ.store((uintptr_t)&tail);
        }
        ~UALL_Type(){ 
            
        }

        uintptr_t helpInsert(UpdateNode *prev, uint64_t seqNum,  uint64_t procID){
            InsertDescNode *desc = &descs[procID];
            UpdateNode *newNode = desc->newNode;
            UpdateNode *next = desc->next;
            if(seqNum != desc->seqNum){
                return prev->succ.load();
            }

            //Attempt to initialize newNode....
            uintptr_t result = 0;
            newNode->succ.compare_exchange_strong(result, (uintptr_t)next);

            uint64_t expected = (seqNum << 12) + (procID << 4) + InsFlag;
            result = expected; 
            uint64_t newSucc;
            
            //If insert node was marked....
            if((result & STATUS_MASK) == Marked){ 
                newSucc = (uintptr_t)next;//newNode has already been removed. Attempt to CAS to remove descriptor.
            }
            else{
                [[likely]];
                newSucc = (uintptr_t)newNode; //Attempt to complete insertion of node.
            }
            prev->succ.compare_exchange_strong(result, (uintptr_t)newSucc);
            if(result == expected){
                return (uintptr_t)newSucc;
            }
            else{
                return result;
            }
        }
        
        //Precondition: prev.succ was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(UpdateNode *prev, UpdateNode *delNode){
            UpdateNode *next = (UpdateNode*)((uintptr_t)delNode->succ & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->succ.compare_exchange_strong(result, (uintptr_t)next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition, prev.succ was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(UpdateNode *prev, UpdateNode *delNode){
            delNode->backlink = prev;
            uintptr_t succ = delNode->succ.load(); //The value of delNode's succ pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its succ, if it is flagged....
                    succ = helpRemove(delNode, (UpdateNode*)next);
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
                    delNode->succ.compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                    if(succ == next)break; //The CAS succeeded!
                }
                state = succ & STATUS_MASK;
                next = succ & NEXT_MASK;
            }
            succ = helpMarked(prev, delNode);
            return succ;
        }

        void insert(UpdateNode *node){
            if((node->succ & STATUS_MASK) == Marked)return;
            UpdateNode *curr = &head;
            uintptr_t succ = curr->succ;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;

            //This is the descriptor for this thread.
            InsertDescNode *desc = &descs[threadID];
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->newNode = node;
            while(1){
                if(state == Normal){
                    [[likely]];
                    //If Next's key <= node's key
                    if(((UpdateNode*)next)->key <= node->key){ 
                        if(next == (uintptr_t)node)return;
                        //node should be placed further along in the list if next <= node
                        curr = (UpdateNode*)next;
                        succ = curr->succ;
                    }
                    else{ //Next is either head or an UpdateNode of a greater key than node.
                        [[likely]];
                        if((node->succ & STATUS_MASK) == Marked){
                            [[unlikely]];
                            return;
                        }
                        desc->next = (UpdateNode*)next; //Set the next of the insert descriptor node.
                        succ = next;

                        uint64_t newVal = (seqNum << 12) + (threadID << 4) + InsFlag;
                        curr->succ.compare_exchange_strong(succ, (uintptr_t)newVal);
                        if(succ == next){ //If the CAS succeeded....
                            helpInsert(curr, seqNum, threadID);
                            desc->seqNum = seqNum + 1; //Increment the sequence number of insert descriptor node.
                            return;
                        }
                    }
                }
                else if(state == InsFlag){
                    [[unlikely]];
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr, seq, proc);
                }
                else if(next == (uintptr_t)node){
                    [[unlikely]];
                    return;
                }
                else if(state == DelFlag){
                    [[unlikely]];
                    succ = helpRemove(curr, (UpdateNode*)next);
                }
                else{ //State is marked
                    [[unlikely]];
                    UpdateNode *prev = curr->backlink;
                    succ = prev->succ;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && next == (uintptr_t)curr){ //Help remove curr from the list.
                        [[unlikely]];
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                //Read next and state from curr.succ.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
        }
        void remove(UpdateNode *node){
            UpdateNode *curr = &head;
            uintptr_t succ = curr->succ;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            while(1){
                if(state == Normal){
                    [[likely]];
                    if(((UpdateNode*)next)->key > node->key){
                        return;
                    }
                    if((UpdateNode*)next != node){ //Advance...
                        [[likely]];
                        curr = (UpdateNode*)next;
                        succ = curr->succ;
                    }
                    else{
                        [[unlikely]];
                        succ = (uintptr_t)node;
                        curr->succ.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                    }
                }
                else if(state == InsFlag){
                    [[unlikely]];
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr, seq, proc);
                }
                //next points to an UpdateNode with a larger key; node is not in the list.
                else if(((UpdateNode*)next)->key > node->key){
                    return;
                }
                else if(state == DelFlag){
                    [[unlikely]];
                    succ = helpRemove(curr, (UpdateNode*)next);
                    if((UpdateNode*)next == node)return;
                }
                else{
                    [[unlikely]];
                    UpdateNode *prev = curr->backlink;
                    succ = prev->succ;
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
        }

        //List traversal algorithms here: 

        //Returns the head of the linked list, or null if the list is empty...
        UpdateNode *first(){
            return next(&head);
        }
        UpdateNode *next(UpdateNode *node){
            uintptr_t succ = node->succ;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;

            
            while(state == InsFlag){
                [[unlikely]];
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                InsertDescNode *desc = &descs[proc];
                next = (uintptr_t)(UpdateNode*)desc->next;
                if(desc->seqNum == seq){
                    [[likely]];
                    break; //desc was still a valid insert descriptor node following node...
                }
                
                succ = node->succ; //Read node->succ again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a UpdateNode....

            //If the following UpdateNode was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (UpdateNode*)next; //Otherwise, return the following UpdateNode.
        }


        UpdateNode *next(UpdateNode *node, uint64_t state){
            uintptr_t succ = node->succ;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;

            
            while(state == InsFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                InsertDescNode *desc = &descs[proc];
                next = (uintptr_t)(UpdateNode*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
                
                succ = node->succ; //Read node->succ again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a UpdateNode....

            //If the following UpdateNode was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (UpdateNode*)next; //Otherwise, return the following UpdateNode.
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

        //Thread safe way of prqinting list
        void printList(std::string (*nodeToString)(UpdateNode*)){
            std::ostringstream stream;

            uint64_t status;
            UpdateNode *node = next(&head, status);
            stream << "{";
            while(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};
