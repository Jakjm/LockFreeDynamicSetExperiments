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
#include "../setbench/common/recordmgr/record_manager.h"
#include "../common.h"
#include "list_extension.h"
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
    ListNode *next;
    NotifyDescNode(PredecessorNode *p) : pNode(p), next(nullptr) {

    }
};

typedef record_manager<reclaimer_debra<>, allocator_new<>, pool_none<>, InsertDescNode, NotifyDescNode> DescManager;

//Linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert.
//compare is the function used to compare the nodes of the linked list
template <int(*compare)(ListNode*, ListNode*)>
class RU_ALL_TYPE {
    public:
        ListNode tail, head; //Head, tail of the linked list. 
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

        uintptr_t helpNotify(ListNode *prev, NotifyDescNode *desc){
            ListNode *next = desc->next;
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

        uintptr_t helpInsert(ListNode *prev, InsertDescNode *desc){
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
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    succ = helpInsert(delNode, (InsertDescNode*)next);
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
        inline int __attribute__((always_inline)) compNode(ListNode *n1, ListNode *n2){
            if(n1 == &tail)return 1;
            else return compare(n1,n2);
        }

        void insert(ListNode *node){
            if((node->successor & STATUS_MASK) == Marked)return;

            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());

            ListNode *curr = &head;
            uintptr_t succ = curr->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            InsertDescNode *newDesc = descMgr->template allocate<InsertDescNode>(threadID(), node);
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
                    newDesc->next = (ListNode*)next; //Set the next of the insert descriptor node.
                    succ = next;
                    curr->successor.compare_exchange_strong(succ, (uintptr_t)newDesc + InsFlag);
                    if(succ == next){ //If the CAS succeeded....
                        helpInsert(curr, (InsertDescNode*)newDesc);
                        return;
                    }
                    //Read next and state from curr.successor.
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == InsFlag){
                    succ = helpInsert(curr, (InsertDescNode*)next);
                    if(((InsertDescNode*)next)->newNode == node)return;
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == NotifFlag){
                    succ = helpNotify(curr, (NotifyDescNode*)next);
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
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());

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
                    succ = helpInsert(curr, (InsertDescNode*)next);
                    next = succ & NEXT_MASK;
                    state = succ & STATUS_MASK;
                }
                else if(state == NotifFlag){
                    succ = helpNotify(curr, (NotifyDescNode*)next);
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

        //Special RU-ALL traversal algorithms here: 
        //Returns the head of the linked list, or null if the list is empty...
        ListNode *first(PredecessorNode *pNode){
            return next(pNode, &head);
        }

        //Returns the node following node, or null if bottom was following node.
        ListNode *next(PredecessorNode *pNode, ListNode *node){
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());
            
            assert(node != &tail);
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            NotifyDescNode *notifyDesc = descMgr->allocate<NotifyDescNode>(threadID(), pNode);
            while(next != (uintptr_t)&tail){
                if(state == Marked){ //If node is marked, then node.next is permanently equal to next.
                    pNode->notifyThreshold = (UpdateNode*)next;
                    descMgr->deallocate(threadID(), notifyDesc); //notifyDesc was not used
                    return (ListNode*)next;
                }
                else if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                    succ = helpRemove(node, (ListNode*)next);
                }
                else if(state == InsFlag){ //Help with insertion while it points to an insertion descriptor...
                    succ = helpInsert(node, (InsertDescNode*)next);
                }
                else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                    succ = helpNotify(node, (NotifyDescNode*)next);
                }
                else if(state == Normal){
                    notifyDesc->next = (ListNode*)next;
                    succ = next;
                    assert(next != (uintptr_t)&tail);
                    assert(next != (uintptr_t)0);
                    node->successor.compare_exchange_strong(succ, (uintptr_t)notifyDesc + NotifFlag);
                    if(succ == next){
                        helpNotify(node, notifyDesc);
                        return (ListNode*)next; //CAS succeeded, therefore pNode->notifyThreshold was updated to next 
                                                //while prev.next was equal to next.
                    } 
                }
                next = succ & NEXT_MASK;
                state = succ & STATUS_MASK;
            }
            descMgr->deallocate(threadID(), notifyDesc); //notifyDesc was not used
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

        ListNode *first(){
            return next(&head);
        }
        ListNode *next(ListNode *node){
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());
            
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            uint64_t state = succ & STATUS_MASK;
            if(state == InsFlag){
                next = (uintptr_t)((InsertDescNode*)next)->next;
            }
            else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                next = (uintptr_t)((NotifyDescNode*)next)->next;
            }

            //If the following ListNode was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (ListNode*)next; //Otherwise, return the following ListNode.
        }

        ListNode *next(ListNode *node, uint64_t &state){
            descMgr->initThread(threadID());
            auto guard = descMgr->getGuard(threadID());
            
            uintptr_t succ = node->successor;
            uintptr_t next = succ & NEXT_MASK;
            state = succ & STATUS_MASK;
            if(state == InsFlag){
                next = (uintptr_t)((InsertDescNode*)next)->next;
            }
            else if(state == NotifFlag){ //Help with notification while node points to a NotifyDescNode....
                next = (uintptr_t)((NotifyDescNode*)next)->next;
            }

            //If the following ListNode was the tail, then return nullptr.
            if(next == (uintptr_t)(&tail)){
                return nullptr;
            }
            else return (ListNode*)next; //Otherwise, return the following ListNode.
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