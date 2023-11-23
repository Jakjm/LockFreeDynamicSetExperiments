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
#include "../common.h"
#include "../trieNodeTypes.h"
#pragma once
using std::string;

#define testStats

//Customized version of PermaRemList that is specifically used for the RUALL of Jeremy's Trie.
//DescNode could either be an insert descriptor node or a notify descriptor node.
struct DescNode{
    std::atomic<uintptr_t> other; //Either a pointer to an update node to be inserted or a pointer to a predecessor node.
    std::atomic<RU_ALL_Node*> next;
    std::atomic<uint64_t> seqNum; //Sequence number.
    DescNode():  other((uintptr_t)nullptr), next(nullptr), seqNum(0){

    }
    volatile char padding[64 - 3 * sizeof(std::atomic<uintptr_t>)];
};

class RU_ALL_TYPE {
    public:
        RU_ALL_Node tail, head; //Head, tail of the linked list. 
    public:
        DescNode descs[NUM_THREADS];
        RU_ALL_TYPE() : tail(), head(){
            head.successor.store((uintptr_t)&tail);
        }
        ~RU_ALL_TYPE(){ 

        }

        uintptr_t helpNotify(RU_ALL_Node *prev,  uint64_t seqNum,  uint64_t procID){
            DescNode *desc = &descs[procID];
            PredecessorNode *pNode = (PredecessorNode*)(uintptr_t)desc->other;
            RU_ALL_Node *next = desc->next;
            if(seqNum != desc->seqNum){
                return prev->successor.load();
            }

            //Update notify threshold of predecessor node.
            if(next == &tail){
                pNode->notifyThreshold = &ZERO_THRES;
            }
            else{
                UpdateNode *oldThresh;
                if(prev == &head)oldThresh = &INFINITY_THRES;
                else oldThresh = (UpdateNode*)prev;
                pNode->notifyThreshold.compare_exchange_strong(oldThresh, (UpdateNode*)next);
            }
                        
            uint64_t expectedSucc = (seqNum << 12) + (procID << 4) + NotifFlag;
            uint64_t result = expectedSucc;
            prev->successor.compare_exchange_strong(result, (uintptr_t)next);
            
            //Return contents of prev->successor immediately following CAS...
            if(result == (uintptr_t)expectedSucc)return (uintptr_t)next;
            else return result;
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
            uint64_t newSucc;
            
            //If insert node was marked....
            if((result & STATUS_MASK) == Marked){ 
                newSucc = (uintptr_t)next;//newNode has already been removed.
                //Attempt to CAS to remove descriptor.
            }
            else{
                newSucc = (uintptr_t)newNode; //Attempt to complete insertion of node.
            }
            prev->successor.compare_exchange_strong(result, (uintptr_t)newSucc);
            if(result == expected){
                return (uintptr_t)newSucc;
            }
            else{
                return result;
            }
        }
        
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(RU_ALL_Node *prev, RU_ALL_Node *delNode){
            uintptr_t next = delNode->successor & NEXT_MASK;
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->successor.compare_exchange_strong(result, next);
            
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
                    uintptr_t markedSuccessor = next + Marked;
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

        void insert(RU_ALL_Node *node){
            if((node->successor & STATUS_MASK) == Marked)return;

            RU_ALL_Node *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            
            //This is the descriptor for this thread.
            DescNode *desc = &descs[threadID];
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->other = (uint64_t)node;
            while(state == InsFlag || state == NotifFlag || next != (uintptr_t)node){
                if(state == Normal){
                    if((next != (uintptr_t)&tail) && (((UpdateNode*)(RU_ALL_Node*)next)->key <= ((UpdateNode*)node)->key)){ //node should be placed further along in the list if next <= node
                        curr = (RU_ALL_Node*)next;
                        succ = curr->successor;
                    }
                    else{ //Next is either the tail or an update node with a larger key than node.
                        if((node->successor & STATUS_MASK) == Marked){
                            return;
                        }
                        desc->next = (RU_ALL_Node*)next; //Set the next of the insert descriptor node.

                        uint64_t newVal = (seqNum << 12) + (threadID << 4) + InsFlag;
                        succ = next;
                        //Attempt to place insDesc between curr and newVal
                        curr->successor.compare_exchange_strong(succ, (uintptr_t)newVal); 
                        if(succ == next){ //If the CAS succeeded....
                            helpInsert(curr, seqNum, threadID);
                            desc->seqNum = seqNum + 1; //Increment the sequence number of this process's desc node
                            return;
                        }
                    }
                }
                else if(state == InsFlag){
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr,seq, proc);
                }
                else if(state == NotifFlag){
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpNotify(curr, seq, proc);
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (RU_ALL_Node*)next);
                }
                else{
                    RU_ALL_Node *prev = curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && (RU_ALL_Node*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
        }
        void remove(RU_ALL_Node *node){
            RU_ALL_Node *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            while(1){
                if(state == Normal){
                    if((next == (uintptr_t)&tail || ((UpdateNode*)(RU_ALL_Node*)next)->key > ((UpdateNode*)node)->key))return;
                    if((RU_ALL_Node*)next != node){ //Advance...
                        curr = (RU_ALL_Node*)next;
                        succ = curr->successor;
                    }
                    else{
                        succ = (uintptr_t)node;
                        curr->successor.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                    }
                }
                else if(state == InsFlag){ 
                    //next is a seqNum/processID pair
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(curr, seq, proc);
                }
                else if(state == NotifFlag){ 
                    //next is a seqNum/processID pair
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpNotify(curr, seq, proc);
                }
                //next is a ListNode pointer, not a seqnum/processID pair
                else if((next == (uintptr_t)&tail) || (((UpdateNode*)(RU_ALL_Node*)next)->key >((UpdateNode*)node)->key))return;
                else if(state == DelFlag){
                    succ = helpRemove(curr, (RU_ALL_Node*)next);
                    if((RU_ALL_Node*)next == node)return;

                }
                else{
                    RU_ALL_Node *prev = curr->backlink;
                    succ = prev->successor;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && (RU_ALL_Node*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
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

            DescNode *desc = &descs[threadID];
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->other = (uint64_t)pNode;
            while(state != Marked){ //TODO fixContinue while next != tail...
                if(state == Normal){
                    desc->next = (RU_ALL_Node*)next;
                    succ = next;

                    uint64_t newVal = (seqNum << 12) + (threadID << 4) + NotifFlag;
                    node->successor.compare_exchange_strong(succ, newVal);
                    if(succ == next){
                        helpNotify(node, seqNum, threadID);
                        desc->seqNum = seqNum + 1; //Increment the sequence number of this process's desc node
                        if(next == (uintptr_t)&tail)return nullptr;
                        else return (RU_ALL_Node*)next; //CAS succeeded, therefore pNode->notifyThreshold was updated to next 
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
                    assert(false);
                }

                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            if(next == (uintptr_t)&tail){
                pNode->notifyThreshold.store(&ZERO_THRES);
                return nullptr;
            }
            else{
                pNode->notifyThreshold.store((UpdateNode*)(RU_ALL_Node*)next);
                return (RU_ALL_Node*)next;
            }
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

        RU_ALL_Node *nextN(RU_ALL_Node *node, uintptr_t &state){
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;

            
            while(state == InsFlag || state == NotifFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                DescNode *desc = &descs[proc];
                next = (uintptr_t)(RU_ALL_Node*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
                
                succ = node->successor; //Read node->successor again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a ListNode....
            //If the following ListNode was the tail, then return nullptr
            return (RU_ALL_Node*)next; //Otherwise, return the following ListNode.
        }
    

        //Thread safe way of printing list
        void printList(){
            
            std::ostringstream stream;

            uint64_t status;
            RU_ALL_Node *node = &head;
            stream << "{";
            while(node){
                stream << "<" << node << ", " << stat_to_char(status)  << ">";
                node = nextN(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};