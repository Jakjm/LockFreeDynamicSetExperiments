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
#include "../trieNodeTypes.h"
#pragma once
using std::string;

//An implementation of Eric Ruppert and Michhail Fomitchev's Lock-Free Linked List
//Specifically made for the P_ALL which is an unsorted linked list
template <typename NotifyThresholdType = AtomicCopyNotifyThreshold>
class PALL{
    public:
        PredecessorNode<NotifyThresholdType> tail, head; //Head, tail of the linked list. 
    public:
        PALL() : tail(0, nullptr), head(0, nullptr){
            head.succ.store((uintptr_t)&tail);
        }
        ~PALL(){ 
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(PredecessorNode<NotifyThresholdType> *prev, PredecessorNode<NotifyThresholdType> *delNode){
            PredecessorNode<NotifyThresholdType> *next = (PredecessorNode<NotifyThresholdType>*)((uintptr_t)delNode->succ & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->succ.compare_exchange_strong(result, (uintptr_t)next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(PredecessorNode<NotifyThresholdType> *prev, PredecessorNode<NotifyThresholdType> *delNode){
            delNode->backlink = prev;
            uintptr_t succ = delNode->succ.load(); //The value of delNode's successor pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(delNode, (PredecessorNode<NotifyThresholdType>*)next);
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

        void insert(PredecessorNode<NotifyThresholdType> *node){
            uintptr_t succ = head.succ.load();
            uintptr_t first = succ & NEXT_MASK;
            uintptr_t state = succ & STATUS_MASK;
            #ifdef COUNT_CONTENTION
                PALLCounter &counter = pallCounter[threadID];
            #endif
            do{
                if(state == Normal){
                    succ = first;
                    node->succ = (uintptr_t)first;
                    head.succ.compare_exchange_strong(succ, (uintptr_t)node);
                    if(succ == first)return;

                    #ifdef COUNT_CONTENTION
                        ++counter.numFailedInsertCASes;
                    #endif 
                }
                //Note that the head node is never marked.
                //Therefore, the state must be delFlag.
                else{ 
                    succ = helpRemove(&head, (PredecessorNode<NotifyThresholdType>*)first);
                    #ifdef COUNT_CONTENTION
                        ++counter.numRemovesHelped;
                    #endif 
                }
                first = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }while(1);
        }

        
        void remove(PredecessorNode<NotifyThresholdType> *node){
            PredecessorNode<NotifyThresholdType> *curr = &head;
            uintptr_t succ = curr->succ;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            #ifdef COUNT_CONTENTION
                PALLCounter &counter = pallCounter[threadID];
            #endif
            while((PredecessorNode<NotifyThresholdType>*)next != &tail){
                if(state == Normal){
                    if((PredecessorNode<NotifyThresholdType>*)next != node){ //Advance...
                        curr = (PredecessorNode<NotifyThresholdType>*)next;
                        succ = curr->succ;
                        #ifdef COUNT_CONTENTION
                            ++counter.nodesTraversed;
                        #endif
                    }
                    else{
                        succ = (uintptr_t)node;
                        curr->succ.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                        #ifdef COUNT_CONTENTION
                            ++counter.numFailedRemoveCASes;
                        #endif 
                    }
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (PredecessorNode<NotifyThresholdType>*)next);
                    #ifdef COUNT_CONTENTION
                        ++counter.numRemovesHelped;
                    #endif 
                    if((PredecessorNode<NotifyThresholdType>*)next == node)return;
                }
                else{
                    PredecessorNode<NotifyThresholdType> *prev = curr->backlink;
                    succ = prev->succ;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                    if(next == (uintptr_t)curr){ //Help remove curr from the list.
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

        //List traversal algorithms here: 
        //Returns the first node in the linked list, or null if the list is empty.
        PredecessorNode<NotifyThresholdType> *first(){
            return next(&head);
        }
        //If node is in the list, returns a pointer to the node following it, or nullptr if node is the last node in the list.
        //If node is not in the list, returns a pointer to the node that followed it when node was removed,
        //or nullptr if node is the last node in the list.
        PredecessorNode<NotifyThresholdType> *next(PredecessorNode<NotifyThresholdType> *node){            
            uintptr_t succ = node->succ;
            PredecessorNode<NotifyThresholdType> *next = (PredecessorNode<NotifyThresholdType>*)(succ & NEXT_MASK);
            if(next == &tail){
                return nullptr;
            }
            else return (PredecessorNode<NotifyThresholdType>*)next;
        }
        PredecessorNode<NotifyThresholdType> *nextN(PredecessorNode<NotifyThresholdType> *node, uint64_t &state){            
            uintptr_t succ = node->succ;
            PredecessorNode<NotifyThresholdType> *next = (PredecessorNode<NotifyThresholdType>*)(succ & NEXT_MASK);
            state = succ & STATUS_MASK;
            return (PredecessorNode<NotifyThresholdType>*)next;
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
        void printList(std::string (*nodeToString)(PredecessorNode<NotifyThresholdType>*)){
            std::ostringstream stream;

            uint64_t status;
            PredecessorNode<NotifyThresholdType> *node = next(&head, status);
            stream << "{";
            while(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }

        void printList(){
            
            std::ostringstream stream;

            uint64_t status;
            PredecessorNode<NotifyThresholdType> *node = &head;
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