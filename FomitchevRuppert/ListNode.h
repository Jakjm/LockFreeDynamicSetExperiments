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