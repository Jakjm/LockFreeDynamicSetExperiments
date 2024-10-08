
#include <stdint.h>
#include <string>
#include <set>
#include "../common.h"
#include "debra.h"
#pragma once

struct ReclaimableBase{
    public:
    virtual ~ReclaimableBase(){
       
    }
};

Debra<ReclaimableBase,5> debra;

//Base class for a Dynamic Set which supports the predecessor operation.
struct DynamicSet{
    virtual bool insert(int64_t x) = 0;
    virtual bool remove(int64_t x) = 0;
    virtual int64_t predecessor(int64_t x) = 0;
    virtual bool search(int64_t) = 0;
    virtual std::string name(){
        return "dynamic set type not set";
    }

    //Prefills the dynamic set with 'numValues' keys that are randomly selected.
    virtual void prefill(uint64_t universeSize, uint64_t numValues){
        std::set<int64_t> valSet;
        while(valSet.size() < numValues){
            int64_t key = rng(universeSize);
            insert(key);
            valSet.insert(key);
        }
    }

    virtual int64_t getChecksum(int64_t universeSize){
        int64_t checkSum = 0;
        for(int64_t i = 0; i < universeSize;++i){
            if(search(i)){
                checkSum += i;
            }
        }
        return checkSum;
    }
};