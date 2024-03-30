#include <iostream>
#include <string>
#include <atomic>
#include <cstdint>
#include "../../common.h"
#define NEXT_bits 0xFFFFFFFFFFFFFFFE
#define MARK_bits 0x1
struct STNode{
    int64_t key;
    std::atomic<uintptr_t> NextMark; //Contains <next, mark>
    std::atomic<STNode*> back;
    std::atomic<STNode*> down;
    std::atomic<bool> stop;
    volatile char padding[64 - 4 * sizeof(STNode*) + sizeof(bool)];
    STNode(int64_t k=0) : key(k), NextMark(0), 
    back(nullptr), down(nullptr), stop(false){

    }
};
template <int levels>
struct SkipTrie{
    STNode heads[levels];
    STNode tail;
    SkipTrie(): tail(INT64_MAX){
        for(int l = levels - 1; l >= 0;--l){
            heads[l] = STNode(INT64_MIN);
            heads[l].NextMark = &tail;
            if(l > 0)heads[l].down = &heads[l - 1];
            else heads[l].down = heads[l];
        }
    }
    STNode *listSearch(STNode *&right, int64_t x, STNode *curr){
        uintptr_t nextmark = curr->NextMark;
        STNode *next = (STNode*)(nextmark & NEXT_bits);
        uint64_t mark = (nextmark & MARK_bits);
        do{
            if(mark){
                STNode *prev = curr->back;
                uintptr_t result = (uintptr_t)curr;
                prev->NextMark.compare_exchange_strong(result, (uintptr_t)next);    
                curr = prev;
            }

            nextmark = curr->NextMark;
            next = (STNode*)(nextmark & NEXT_bits);
            mark = (nextmark & MARK_bits);
            
        }while(next->key < x);
        right = next;
        return curr;
    }
    void insert(int64_t x){

    }
    void remove(int64_t x){
        int64_t pred = predecessor(x-1);
    }
    int64_t predecessor(int64_t x){
        return -1;
    }
    bool search(int64_t){
        return false;
    }
    std::string name(){
        return "Shavit SkipTrie";
    }
};

int main(int argc, char **argv){

}