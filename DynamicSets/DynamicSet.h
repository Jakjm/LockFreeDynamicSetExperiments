
#include <stdint.h>
#include <string>
#pragma once
//Base class for a Dynamic Set which supports the predecessor operation.
struct DynamicSet{
    virtual void insert(int64_t x) = 0;
    virtual void remove(int64_t x) = 0;
    virtual int64_t predecessor(int64_t x) = 0;
    virtual bool search(int64_t) = 0;
    virtual std::string name() = 0;
};