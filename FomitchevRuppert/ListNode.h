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

#ifndef LL_NODE
#define LL_NODE


const uint64_t Normal = 0;
const uint64_t DelFlag = 1;
const uint64_t Marked = 2; 

const uint64_t NEXT_MASK = -8;  //61 1s, 2 0s = FFFF FFFF FFFF FFF8
const uint64_t STATUS_MASK = 7; //61 0s, 2 1s = 0000 0000 0000 0007


/**
* ListNode class
*/
class ListNode{
    public:
        std::atomic<uintptr_t> successor; //Contains <right, status>. The status is contained within the lowest 3 bits of the pointer.
        std::atomic<ListNode*> backlink;  
        ListNode() : successor(0), backlink(nullptr){

        }
        //TODO ifdef 
};

#endif