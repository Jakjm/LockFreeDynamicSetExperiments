#include <atomic>

template <typename Value>
struct HTNode{
    uintptr_t nextState;
    uint64_t key;
    Value val;
};



struct table{

};