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
class P_ALL_TYPE{
    public:
        PredecessorNode tail, head; //Head, tail of the linked list. 
    public:
        P_ALL_TYPE() : tail(0, nullptr), head(0, nullptr){
            head.succ.store((uintptr_t)&tail);
        }
        ~P_ALL_TYPE(){ 
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
        uintptr_t helpMarked(PredecessorNode *prev, PredecessorNode *delNode){
            PredecessorNode *next = (PredecessorNode*)((uintptr_t)delNode->succ & NEXT_MASK);
            uintptr_t expected = (uintptr_t)delNode + DelFlag;
            uintptr_t result = expected;
            prev->succ.compare_exchange_strong(result, (uintptr_t)next);
            
            if(result == expected)return (uintptr_t)next;
            else return result;
        }
        //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
        uintptr_t helpRemove(PredecessorNode *prev, PredecessorNode *delNode){
            delNode->backlink = prev;
            uintptr_t succ = delNode->succ.load(); //The value of delNode's successor pointer
            uintptr_t state = succ & STATUS_MASK;
            uintptr_t next = succ & NEXT_MASK;

            while(state != Marked){ //While delNode is not marked...
                if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(delNode, (PredecessorNode*)next);
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

        
        void remove(PredecessorNode *node){
            PredecessorNode *curr = &head;
            uintptr_t succ = curr->succ;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            while((PredecessorNode*)next != &tail){
                if(state == Normal){
                    if((PredecessorNode*)next != node){ //Advance...
                        curr = (PredecessorNode*)next;
                        succ = curr->succ;
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                    else{
                        succ = (uintptr_t)node;
                        curr->succ.compare_exchange_strong(succ, (uintptr_t)node + DelFlag);
                        if(succ == (uintptr_t)node){
                            helpRemove(curr, node);
                            return;
                        }
                        next = succ & NEXT_MASK;
                        state = succ & STATUS_MASK;
                    }
                }
                else if(state == DelFlag){
                    succ = helpRemove(curr, (PredecessorNode*)next);
                    if((PredecessorNode*)next == node)return;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else{
                    PredecessorNode *prev = curr->backlink;
                    succ = prev->succ;
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
        //Returns the first node in the linked list, or null if the list is empty.
        PredecessorNode *first(){
            return next(&head);
        }
        //If node is in the list, returns a pointer to the node following it, or nullptr if node is the last node in the list.
        //If node is not in the list, returns a pointer to the node that followed it when node was removed,
        //or nullptr if node is the last node in the list.
        PredecessorNode *next(PredecessorNode *node){            
            uintptr_t succ = node->succ;
            PredecessorNode *next = (PredecessorNode*)(succ & NEXT_MASK);
            if(next == &tail){
                return nullptr;
            }
            else return (PredecessorNode*)next;
        }
        PredecessorNode *next(PredecessorNode *node, uint64_t &state){            
            uintptr_t succ = node->succ;
            PredecessorNode *next = (PredecessorNode*)(succ & NEXT_MASK);
            state = succ & STATUS_MASK;
            if(next == &tail){
                return nullptr;
            }
            else return (PredecessorNode*)next;
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
        void printList(std::string (*nodeToString)(PredecessorNode*)){
            std::ostringstream stream;

            uint64_t status;
            PredecessorNode *node = next(&head, status);
            stream << "{";
            while(node){
                stream << "<" << nodeToString(node) << ", " << stat_to_char(status)  << ">";
                node = next(node, status);
            }
            stream << "}\n";
            std::cout << stream.str();
        }
};