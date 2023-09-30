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

//Node that is used to set pNode.notifyThreshold
class NotifyDescNode{
    public:
    PredecessorNode *pNode;
    RU_ALL_Node *next;
    NotifyDescNode(PredecessorNode *p) : pNode(p), next(nullptr) {

    }
};

class RU_ALL_INS_DescNode{
    public:
    RU_ALL_Node *newNode;
    RU_ALL_Node *next;
    RU_ALL_INS_DescNode(RU_ALL_Node *newNode) : newNode(newNode), next(nullptr) {

    }
};


typedef record_manager<reclaimer_debra<>, allocator_new<>, pool_none<>, RU_ALL_INS_DescNode, NotifyDescNode> DescManager;

//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert.
//compare is the function used to compare the nodes of the linked list
template <int(*compare)(RU_ALL_Node*, RU_ALL_Node*)>
class RU_ALL_TYPE {
    public:
        RU_ALL_Node tail, head; //Head, tail of the linked list. 
        DescManager *const descMgr; //Record manager used to allocate insert_descriptor nodes.
    public:
        RU_ALL_TYPE() : tail(), head(), descMgr(new DescManager(NUM_THREADS)){
            head.successor.store((uintptr_t)&tail);
        }
        ~RU_ALL_TYPE(){ 
            //Deinitialize all threads.
            for(int i = 0;i < NUM_THREADS;++i){
                descMgr->deinitThread(i);
            }
            delete descMgr;
        }

        uintptr_t helpNotify(RU_ALL_Node *prev, NotifyDescNode *desc){
            RU_ALL_Node *next = desc->next;
            assert(next != &tail);
            assert(next != 0);
            UpdateNode *expected;
            if(prev == &head)expected = &INFINITY_THRES;
            else expected = (UpdateNode*)prev;
            //Update notify threshold.
            desc->pNode->notifyThreshold.compare_exchange_strong(expected, (UpdateNode*)next);
            
            uintptr_t result = (uintptr_t)desc + NotifFlag;
            prev->successor.compare_exchange_strong(result, (uintptr_t)next);
            

            //Return contents of prev->successor immediately following CAS...
            if(result == (uintptr_t)desc + NotifFlag){
                descMgr->retire(threadID(), desc);
                return (uintptr_t)next;
            }
            else{
                return result;
            } 
        }

        uintptr_t helpInsert(RU_ALL_Node *prev, RU_ALL_INS_DescNode *desc){
            uintptr_t expected = 0;
            uintptr_t result = expected;
            desc->newNode->successor.compare_exchange_strong(result, (uintptr_t)desc->next);

            //If insert node was marked....
            if((result & STATUS_MASK) == Marked){ 
                expected = (uintptr_t)desc + InsFlag;
                result = expected;
                //newNode has already been removed.
                //Attempt to CAS to remove descriptor.
                prev->successor.compare_exchange_strong(result, (uintptr_t)desc->next);
                if(result == expected){
                    descMgr->retire(threadID(), desc);
                    return (uintptr_t)desc->next;
                }
                else{
                    return result;
                }
            }
            else{
                expected = (uintptr_t)desc + InsFlag;
                result = expected;
                //Attempt to complete insertion of insert node.
                prev->successor.compare_exchange_strong(result, (uintptr_t)desc->newNode);
                if(result == expected){
                    descMgr->retire(threadID(), desc);
                    return (uintptr_t)desc->newNode;
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
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    succ = helpInsert(delNode, (RU_ALL_INS_DescNode*)next);
                }
                else if(state == NotifFlag){
                    succ = helpNotify(delNode, (NotifyDescNode*)next);
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

            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());

            RU_ALL_Node *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            RU_ALL_INS_DescNode *newDesc = descMgr->template allocate<RU_ALL_INS_DescNode>(threadID(), node);
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
                        descMgr->retire(threadID(), newDesc);
                        return;
                    }
                    newDesc->next = (RU_ALL_Node*)next; //Set the next of the insert descriptor node.
                    succ = next;
                    curr->successor.compare_exchange_strong(succ, (uintptr_t)newDesc + InsFlag);
                    if(succ == next){ //If the CAS succeeded....
                        helpInsert(curr, (RU_ALL_INS_DescNode*)newDesc);
                        return;
                    }
                    //Read next and state from curr.successor.
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == InsFlag){
                    succ = helpInsert(curr, (RU_ALL_INS_DescNode*)next);
                    if(((RU_ALL_INS_DescNode*)next)->newNode == node){
                        descMgr->retire(threadID(), newDesc);
                        return;
                    }
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == NotifFlag){
                    succ = helpNotify(curr, (NotifyDescNode*)next);
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
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());

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
                    succ = helpInsert(curr, (RU_ALL_INS_DescNode*)next);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == NotifFlag){
                    succ = helpNotify(curr, (NotifyDescNode*)next);
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
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());

            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            NotifyDescNode *notifyDesc = descMgr->allocate<NotifyDescNode>(threadID(), pNode);
            while(next != (uintptr_t)&tail){ //Continue while next != tail...
                if(state == Normal){
                    notifyDesc->next = (RU_ALL_Node*)next;
                    succ = next;

                    node->successor.compare_exchange_strong(succ, (uintptr_t)notifyDesc + NotifFlag);
                    if(succ == next){
                        helpNotify(node, notifyDesc);
                        return (RU_ALL_Node*)next; //CAS succeeded, therefore pNode->notifyThreshold was updated to next 
                                                //while prev.next was equal to next.
                    } 
                }
                else if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(node, (RU_ALL_Node*)next);
                }
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    succ = helpInsert(node, (RU_ALL_INS_DescNode*)next);
                }
                else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                    succ = helpNotify(node, (NotifyDescNode*)next);
                }
                else{ //node is marked, so node.next is permanently equal to next. No need to help!
                    pNode->notifyThreshold = (UpdateNode*)next;
                    descMgr->retire(threadID(), notifyDesc); //notifyDesc was not used
                    return (RU_ALL_Node*)next;
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            descMgr->retire(threadID(), notifyDesc); //notifyDesc was not used
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
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());
            
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            if(state == InsFlag){
                next = (uintptr_t)((RU_ALL_INS_DescNode*)next)->next;
            }
            else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                next = (uintptr_t)((NotifyDescNode*)next)->next;
            }

            //If the following RU_ALL_Node was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (RU_ALL_Node*)next; //Otherwise, return the following RU_ALL_Node.
        }

        RU_ALL_Node *next(RU_ALL_Node *node, uint64_t &state){
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());
            
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;
            if(state == InsFlag){
                next = (uintptr_t)((RU_ALL_INS_DescNode*)next)->next;
            }
            else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                next = (uintptr_t)((NotifyDescNode*)next)->next;
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