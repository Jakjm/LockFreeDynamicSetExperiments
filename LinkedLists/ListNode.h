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

#ifndef LL_NODE
#define LL_NODE

const uint64_t Normal = 0;
const uint64_t DelFlag = 1;
const uint64_t Marked = 2; 
const uintptr_t InsFlag = 3;
const uintptr_t NotifFlag = 4;

const uint64_t NEXT_MASK = -8;  //61 1s, 2 0s = FFFF FFFF FFFF FFF8
const uint64_t STATUS_MASK = 7; //61 0s, 2 1s = 0000 0000 0000 0007


//The lowest 4 bits of a ListNode's successor pointer are used for the states...
//If the state is InsFlag or NotifFlag, then the information to access a descriptor node is contained as follows:
const uint64_t PROC_MASK = 0x0000000000000FF0; //Process ID contained within next lowest 8 bits
const uint64_t SEQ_MASK =  0xFFFFFFFFFFFFF000; //Sequence # contained within highest 52 bits
/**
* ListNode class
*/
class ListNode{
    public:
        std::atomic<uintptr_t> successor; //Contains <next, state>. The state is contained within the lowest 3 bits of the pointer.
        std::atomic<ListNode*> backlink;  //A pointer to the ListNode preceeding this node before it is removed.
        ListNode() : successor(0), backlink(nullptr){

        }
};


/**
* Like a listnode but used for the RU_ALL.
*/
class RU_ALL_Node{
    public:
        std::atomic<uintptr_t> successor; //Contains <next, status>. The status is contained within the lowest 3 bits of the pointer.
        std::atomic<RU_ALL_Node*> backlink;  
        RU_ALL_Node() : successor(0), backlink(nullptr){

        }
};

std::string toString(ListNode *lNode){
    return std::to_string(lNode->successor);
}

#endif