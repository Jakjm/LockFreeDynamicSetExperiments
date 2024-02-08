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
#include "../debra.h"
#include "../common.h"
#include "../trieNodeTypes.h"
#pragma once
using std::string;

#define testStats


//Either a notify descriptor node or an insert descriptor node.
struct DescNode{
    std::atomic<uintptr_t> node;
    std::atomic<uintptr_t> seqNum;
    std::atomic<UpdateNode*> next;
    DescNode() : node(0), seqNum(0), next(0) {

    }
};

//TODO pool memory from desc nodes that will not be used rather than reclaim
template <>
class RU_ALL_TYPE<NotifDescNotifyThreshold>{
    public:
        UpdateNode head, tail; //Head, tail of the linked list. 
        DescNode descs[MAX_THREADS];
    public:
        RU_ALL_TYPE() : head(INT64_MAX,INS), tail(INT64_MIN,INS) {
            head.rSucc.store((uintptr_t)&tail);
        }
        ~RU_ALL_TYPE(){ 

        }

        uintptr_t helpNotify(UpdateNode *prev,  uint64_t seqNum,  uint64_t procID){
            DescNode *desc = &descs[procID];
            PredecessorNode<NotifDescNotifyThreshold> *pNode = (PredecessorNode<NotifDescNotifyThreshold>*)(uintptr_t)desc->node;
            UpdateNode *next = desc->next;
            if(seqNum != desc->seqNum){
                return prev->rSucc.load();
            }

            //Update notify threshold of predecessor node.
            if(next == &tail){
                pNode->notifyThreshold.write((uintptr_t)&tail);
            }
            else{
                pNode->notifyThreshold.CAS((uintptr_t)prev,(uintptr_t)next);
            }

            uint64_t expectedSucc = (seqNum << 12) + (procID << 4) + NotifFlag;
            uint64_t result = expectedSucc;
            prev->rSucc.compare_exchange_strong(result, (uintptr_t)next);

            //Return contents of prev->successor immediately following CAS...
            if(result == (uintptr_t)expectedSucc)return (uintptr_t)next;
            else return result;
        }


        uintptr_t helpInsert(UpdateNode *prev, uint64_t seqNum,  uint64_t procID){
            DescNode *desc = &descs[procID];
            UpdateNode *newNode = (UpdateNode*)(uintptr_t)desc->node;
            UpdateNode *next = desc->next;
            if(seqNum != desc->seqNum){
                return prev->rSucc.load();
            }

            //Attempt to initialize newNode....
            uintptr_t result = 0;
            newNode->rSucc.compare_exchange_strong(result, (uintptr_t)next);

            uint64_t expected = (seqNum << 12) + (procID << 4) + InsFlag;
            result = expected; 
            uint64_t newSucc;
            
            if((result & STATUS_MASK) == Marked){ //If newNode was marked, it was already removed from the RUALL.
                newSucc = (uintptr_t)next; //Attempt to CAS to remove descriptor.
            }
            else{
                newSucc = (uintptr_t)newNode; //Attempt to CAS to complete insertion of node.
            }
            prev->rSucc.compare_exchange_strong(result, (uintptr_t)newSucc);
            if(result == expected)return (uintptr_t)newSucc;
            else return result;
        }

        
        //Precondition: prev.rSucc was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(UpdateNode *prev, UpdateNode *delNode){
            uintptr_t next = delNode->rSucc & NEXT_MASK;
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->rSucc.compare_exchange_strong(result, next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition, prev.rSucc was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(UpdateNode *prev, UpdateNode *delNode){
            assert(prev != delNode);
            delNode->rBacklink = prev;
            uintptr_t succ = delNode->rSucc.load(); //The value of delNode's succ pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its succ, if it is flagged....
                    succ = helpRemove(delNode, (UpdateNode*)next);
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(delNode,  seqNum, procID);
                }
                else if(state == NotifFlag){
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with notification
                    succ = helpNotify(delNode, seqNum, procID);
                }
                else{ //Attempt to mark the node if the status was normal...
                    uintptr_t markedsucc = next + Marked;
                    succ = next;
                    delNode->rSucc.compare_exchange_strong(succ, markedsucc); //Try to update from <next, Normal> to <next, Marked>
                    if(succ == next)break; //The CAS succeeded!
                }
                state = succ & STATUS_MASK;
                next = succ & NEXT_MASK;
            }
            succ = helpMarked(prev, delNode);
            return succ;
        }

        void insert(UpdateNode *node){
            if((node->rSucc & STATUS_MASK) == Marked)return;
            UpdateNode *curr = &head;
            uintptr_t succ = curr->rSucc;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            
            //This is the descriptor for this thread.
            DescNode *desc = &descs[threadID];
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->node = (uintptr_t)node;
            while(state == InsFlag || state == NotifFlag || next != (uintptr_t)node){
                if(state == Normal){
                    //node should be placed further along in the list if next's key >= node's key
                    if(((UpdateNode*)next)->key >= node->key){ 
                        curr = (UpdateNode*)next;
                        succ = curr->rSucc;
                    }
                    else{ //Next is either the tail or an update node with a smaller key than node.
                        if((node->rSucc & STATUS_MASK) == Marked){
                            return;
                        }
                        desc->next = (UpdateNode*)next; //Set the next of the descriptor node.
                        succ = next;

                        uint64_t newVal = (seqNum << 12) + (threadID << 4) + InsFlag;
                        curr->rSucc.compare_exchange_strong(succ, (uintptr_t)newVal);
                        if(succ == next){ //If the CAS succeeded....
                            helpInsert(curr, seqNum, threadID);
                            desc->seqNum = seqNum + 1; //Increment the sequence number of insert descriptor node.
                            return;
                        }
                    }
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(curr,  seqNum, procID);
                }
                else if(state == NotifFlag){
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    succ = helpNotify(curr, seqNum, procID);
                    
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (UpdateNode*)next);
                }
                else{
                    UpdateNode *prev = curr->rBacklink;
                    succ = prev->rSucc;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && (UpdateNode*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                    }
                    curr = prev;
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
        }
        void remove(UpdateNode *node){
            UpdateNode *curr = &head;
            uintptr_t succ = curr->rSucc;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            while(1){
                if(state == Normal){
                    if(((UpdateNode*)next)->key < node->key){
                        return;
                    }
                    if((UpdateNode*)next != node){ //Advance...
                        curr = (UpdateNode*)next;
                        succ = curr->rSucc;
                    }
                    else{
                        succ = (uintptr_t)node;
                        curr->rSucc.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                    }
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(curr,  seqNum, procID);
                }
                else if(state == NotifFlag){
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    succ = helpNotify(curr, seqNum, procID);
                    
                }
                //If next points to an UpdateNode of smaller key, node was not in list.
                else if((((UpdateNode*)next)->key < node->key)){ 
                    return;
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (UpdateNode*)next);
                    if((UpdateNode*)next == node){
                        return;
                    }

                }
                else{
                    UpdateNode *prev = curr->rBacklink;
                    succ = prev->rSucc;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && (UpdateNode*)next == curr){ //Help remove curr from the list.
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
        UpdateNode *first(PredecessorNode<NotifDescNotifyThreshold> *pNode){
            return next(pNode, &head);
        }

        //Returns the node following node, or null if bottom was following node.
        UpdateNode *next(PredecessorNode<NotifDescNotifyThreshold> *pNode, UpdateNode *node){
            uintptr_t succ = node->rSucc;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;

            DescNode *desc = &descs[threadID];
            uint64_t seqNum = desc->seqNum;
            assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->node = (uintptr_t)pNode;
            while(state != Marked){ //TODO fixContinue while next != tail...
                if(state == Normal){
                    desc->next = (UpdateNode*)next;
                    succ = next;

                    uint64_t newVal = (seqNum << 12) + (threadID << 4) + NotifFlag;
                    node->rSucc.compare_exchange_strong(succ, newVal);
                    if(succ == next){
                        helpNotify(node, seqNum, threadID);
                        desc->seqNum = seqNum + 1; //Increment the sequence number of this process's desc node
                        if(next == (uintptr_t)&tail)return nullptr;
                        else return (UpdateNode*)next; //CAS succeeded, therefore pNode->notifyThreshold was updated to next 
                    } 
                }
                else if(state == NotifFlag) { //Help with notification while node points to a NotifyDescNode....
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpNotify(node, seq, proc);
                }
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    uint64_t seq = (next & SEQ_MASK) >> 12;
                    uint64_t proc = (next & PROC_MASK) >> 4;
                    succ = helpInsert(node, seq, proc);
                }
                else{ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(node, (UpdateNode*)next);
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            pNode->notifyThreshold.write(next);
            if(next == (uintptr_t)&tail)return nullptr;
            else return (UpdateNode*)next;
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

        UpdateNode *nextN(UpdateNode *node, uintptr_t &state){
            uintptr_t succ = node->rSucc;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;

            
            while(state == InsFlag || state == NotifFlag){
                uint64_t seq = (next & SEQ_MASK) >> 12;
                uint64_t proc = (next & PROC_MASK) >> 4;
                DescNode *desc = &descs[proc];
                next = (uintptr_t)(UpdateNode*)desc->next;
                if(desc->seqNum == seq)break; //desc was still a valid insert descriptor node following node...
                
                succ = node->rSucc; //Read node->succ again.
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            //next points to a UpdateNode....
            return (UpdateNode*)next; //Otherwise, return the following UpdateNode.
        }
    

        //Thread safe way of printing list
        void printList(){
            
            std::ostringstream stream;

            uint64_t status;
            UpdateNode *node = &head;
            stream << "{";
            while(node){
                
                stream << "<";
                if (node == &head){
                    stream << "Head";
                }
                else if(node == &tail){
                    stream << "Tail";
                }
                else{
                    stream << node;
                }
                stream << ", key:" << node->key;
                stream << ", Status:" << stat_to_char(status)  << ">";
                node = nextN(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};
