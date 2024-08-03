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
#include "../../debra.h"
#include "../../../common.h"
#include "../trieNodeTypes.h"
#pragma once
using std::string;


class RUALL{
    public:
        UpdateNode head, tail; //Head, tail of the linked list. 
    public:
        //InsertDescNode insert_descs[MAX_THREADS]; //Insert descriptor nodes for each process
        RUALL() : head(INT64_MAX,INS), tail(-1,INS) {
            head.rSucc.store((uintptr_t)&tail);
        }
        ~RUALL(){ 

        }

        uintptr_t helpInsert(UpdateNode *prev, uint64_t seqNum,  uint64_t procID){
            InsertDescNode *desc = &insert_descs[procID];
            UpdateNode *newNode = desc->newNode;
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
            //assert(prev != delNode);
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
            InsertDescNode *desc = &insert_descs[threadID];
            uint64_t seqNum = desc->seqNum;
            //assert(seqNum < ((int64_t)1 << 50)); //Ensure the sequence number is less than 2^50
            desc->newNode = node;
            #ifdef COUNT_CONTENTION
                RUALLCounter &counter = ruallCounter[threadID];
            #endif 
            while(1){
                if(state == Normal){
                    //node should be placed further along in the list if next's key >= node's key
                    if(((UpdateNode*)next)->key >= node->key){ 
                        if(next == (uintptr_t)node)return;
                        curr = (UpdateNode*)next;
                        succ = curr->rSucc;
                        #ifdef COUNT_CONTENTION
                            ++counter.nodesTraversed;
                        #endif
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
                        #ifdef COUNT_CONTENTION
                            ++counter.numFailedCASes;
                        #endif 
                    }
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(curr,  seqNum, procID);
                    #ifdef COUNT_CONTENTION
                        ++counter.numInsertsHelped;
                    #endif
                }
                else if(next == (uintptr_t)node)return; //node was already in the list.
                else if(state == DelFlag){
                    succ = helpRemove(curr, (UpdateNode*)next);
                    #ifdef COUNT_CONTENTION
                        ++counter.numRemovesHelped;
                    #endif 
                }
                else{
                    UpdateNode *prev = curr->rBacklink;
                    succ = prev->rSucc;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && (UpdateNode*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                        #ifdef COUNT_CONTENTION
                            ++counter.numMarksHelped;
                        #endif 
                    }
                    curr = prev;
                    #ifdef COUNT_CONTENTION
                        ++counter.numBacktracks;
                    #endif 
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
            #ifdef COUNT_CONTENTION
                RUALLCounter &counter = ruallCounter[threadID];
            #endif 
            while(1){
                if(state == Normal){
                    [[likely]];
                    if(((UpdateNode*)next)->key < node->key){
                        return;
                    }
                    if((UpdateNode*)next != node){ //Advance...
                        curr = (UpdateNode*)next;
                        succ = curr->rSucc;
                        #ifdef COUNT_CONTENTION
                            ++counter.nodesTraversed;
                        #endif
                    }
                    else{
                        succ = (uintptr_t)node;
                        curr->rSucc.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                        #ifdef COUNT_CONTENTION
                            ++counter.numFailedCASes;
                        #endif 
                    }
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    uint64_t seqNum = (next & SEQ_MASK) >> 12;
                    uint64_t procID = (next & PROC_MASK) >> 4;
                    //Help with insertion...
                    succ = helpInsert(curr,  seqNum, procID);
                    #ifdef COUNT_CONTENTION
                        ++counter.numInsertsHelped;
                    #endif
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
                    #ifdef COUNT_CONTENTION
                        ++counter.numRemovesHelped;
                    #endif
                }
                else{
                    UpdateNode *prev = curr->rBacklink;
                    succ = prev->rSucc;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(state == DelFlag && (UpdateNode*)next == curr){ //Help remove curr from the list.
                        succ = helpMarked(prev, curr);
                        #ifdef COUNT_CONTENTION
                            ++counter.numMarksHelped;
                        #endif 
                    }
                    curr = prev;
                    #ifdef COUNT_CONTENTION
                        ++counter.numBacktracks;
                    #endif 
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
        }
        //Special RU-ALL traversal algorithms here: 
        //Returns the head of the linked list, or null if the list is empty...
        UpdateNode *first(PredecessorNode *pNode){
            return next(pNode, &head);
        }

        //Returns the node following node, or null if bottom was following node.
        UpdateNode *next(PredecessorNode *pNode, UpdateNode *node){
            UpdateNode *next = pNode->ruallPosition.copyNext(node);
            if(next == &tail)return nullptr;
            else return next;
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
};