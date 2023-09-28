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


uint64_t Normal = 0;
uint64_t DelFlag = 1;
uint64_t Marked = 2; 

uint64_t NEXT_MASK = -8; //62 1s, 2 0s    = FFFF FFFF FFFF FFFC
uint64_t STATUS_MASK = 7; //62 0s, 2 1s = 0000 0000 0000 0003


/**
* ListNode class
*/
class ListNode{
    public:
        std::atomic<uintptr_t> successor; //Contains <right, status>
        std::atomic<ListNode*> backlink;  
        ListNode() : successor(0), backlink(nullptr){

        }
        //TODO ifdef 
};

#endif