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


struct InsertDescPool{
    InsertDesc *d;
    volatile char padding [64 - sizeof(InsertDesc*)];
    InsertDescPool() : d(new InsertDesc()){
    }
    ~InsertDescPool(){
        delete d;
    }
};

InsertDescPool descNodePool[MAX_THREADS];

template <>
class RU_ALL_TYPE<AtomicCopyNotifyThreshold>{
    public:
        UpdateNode head, tail; //Head, tail of the linked list. 
    public:
        RU_ALL_TYPE() : head(INT64_MAX,INS), tail(-1,INS) {
            head.rSucc.store((uintptr_t)&tail);
        }
        ~RU_ALL_TYPE(){ 

        }

        uintptr_t helpInsert(UpdateNode *prev, InsertDesc *descNode){
            UpdateNode *newNode = descNode->newNode;
            UpdateNode *next = descNode->next;

            //Attempt to initialize newNode....
            uintptr_t result = 0;
            newNode->rSucc.compare_exchange_strong(result, (uintptr_t)next);

            uintptr_t expected = (uintptr_t)descNode + InsFlag;
            result = expected; 
            uintptr_t newSucc;
            
            //If insert node was marked....
            if((result & STATUS_MASK) == Marked){ 
                newSucc = (uintptr_t)next; //newNode has already been removed.
                //Attempt to CAS to remove descriptor.
            }
            else{
                newSucc = (uintptr_t)newNode; //Attempt to complete insertion of node.
            }
            prev->rSucc.compare_exchange_strong(result, (uintptr_t)newSucc);
            if(result == expected){
                trieDebra.retire(descNode);
                return (uintptr_t)newSucc;
            }
            else{
                return result;
            }
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
            delNode->rBacklink = prev;
            uintptr_t succ = delNode->rSucc.load(); //The value of delNode's succ pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its succ, if it is flagged....
                    succ = helpRemove(delNode, (UpdateNode*)next);
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    //Help with insertion...
                    succ = helpInsert(delNode,  (InsertDesc*)next);
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
            InsertDesc *desc = descNodePool[threadID].d; 
            desc->newNode = node;
            while(state == InsFlag || next != (uintptr_t)node){
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
                        desc->next = (UpdateNode*)next; //Set the next of the insert descriptor node.
                        succ = next;
                        //Attempt to place insDesc between curr and newVal
                        curr->rSucc.compare_exchange_strong(succ, (uintptr_t)desc + InsFlag); 
                        if(succ == next){ //If the CAS succeeded....
                            helpInsert(curr, desc);
                            descNodePool[threadID].d = new InsertDesc();
                            return;
                        }
                    }
                }
                else if(state == InsFlag){ //prev.next was an Insert Descriptor Node. Help with insertion.
                    //Help with insertion...
                    succ = helpInsert(curr,  (InsertDesc*)next);
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
                    //If next points to an UpdateNode of smaller key, node was not in list.
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
                    //Help with insertion...
                    succ = helpInsert(curr,  (InsertDesc*)next);
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
        UpdateNode *first(PredecessorNode<AtomicCopyNotifyThreshold> *pNode){
            return next(pNode, &head);
        }

        //Returns the node following node, or null if bottom was following node.
        UpdateNode *next(PredecessorNode<AtomicCopyNotifyThreshold> *pNode, UpdateNode *node){
            pNode->notifyThreshold.swcopy(&node->rSucc);
            UpdateNode *next = pNode->notifyThreshold.read();
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

        UpdateNode *nextN(UpdateNode *node, uintptr_t &state){
            uintptr_t succ = node->rSucc;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;

            
            if(state == InsFlag){ //Return the UpdateNode following the insert desc node after node.
                return ((InsertDesc*)next)->next;
            }
            else return (UpdateNode*)next; //Otherwise, return the following UpdateNode
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
