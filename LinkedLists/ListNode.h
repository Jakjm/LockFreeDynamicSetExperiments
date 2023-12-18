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

std::string toString(ListNode *lNode){
    return std::to_string(lNode->successor);
}

#endif