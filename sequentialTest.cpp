#include "DynamicSets/DynamicSet.h"
#include <iostream>
//Sequential implementation of a dynamic set based on std::set.
struct SeqSet : public DynamicSet{
    std::set<int64_t> s;

    bool search(int64_t x){
        return s.find(x) != s.end();
    }
    int64_t predecessor(int64_t x){
        auto predIt = (--s.lower_bound(x));
        return *predIt;
    }
    bool remove(int64_t x){
        auto iter = s.find(x);
        if(iter != s.end())s.erase(iter);
        return iter != s.end();
    }
    bool insert(int64_t x){
        //Return whether the insertion added x to the set.
        return s.insert(x).second == true;
    }
};

//Randomized sequential set tester...
int main(){
    SeqSet set;
    set.insert(5);
    set.insert(10);
    set.insert(18);
    set.remove(15);
    std::cout << set.predecessor(17) << std::endl;
}