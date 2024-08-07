#include "DynamicSets/DynamicSet.h"
#include "common.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string.h>
#include "DynamicSets/DynamicSet.h"
#include "DynamicSets/Trie/trie.h"
#include "DynamicSets/FR_SkipList/FRSkipList.h"
#include "DynamicSets/FR_List/FRList.h"
//#include "DynamicSets/SkipTrie/skiptrie.h"
#include "DynamicSets/FR_Augmented_Trie/augmentedTrie.h"



//Sequential implementation of a dynamic set based on std::set.
struct SeqSet : public DynamicSet{
    std::set<int64_t,std::greater<int64_t>> s;

    bool search(int64_t x){
        return s.find(x) != s.end();
    }
    int64_t predecessor(int64_t x){
        auto it = s.upper_bound(x);
        if(it == s.end())return -1;
        else return *it;
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

enum opType{ins,del,pred,search};

void randTest(int64_t universeSize, int opCount, DynamicSet *set){
    SeqSet seq;
    for(int i = 0; i < opCount;++i){
        int64_t k = rng(universeSize);
        int64_t opType = rng(4);
        
        bool ours, theirs;
        int64_t ourPred, theirPred;
        if(opType == ins){
            //std::cout<< "Inserted " << k << std::endl;
            ours = set->insert(k);
            theirs = seq.insert(k);
            assert(ours == theirs);
        }
        else if(opType == del){
            //std::cout<< "Deleted " << k << std::endl;
            ours = set->remove(k);
            theirs = seq.remove(k);
            assert(ours == theirs);
        }
        else if(opType == pred){
            ourPred = set->predecessor(k);
            theirPred = seq.predecessor(k); 
            assert(ourPred == theirPred);
        }
        else{
            ours = set->search(k);
            theirs = seq.search(k);
            assert(ours == theirs);
        }
    }
}

//Randomized sequential set tester...
int main(int argc, char **argv){
    int keyRange = 12;
    threadID = 0;
    int numProcs = 1;
    Trie trieSet(keyRange);
    LinkedListSet listSet;
    SkipListSet<25> skipList;
    AS_Trie augmentedTrie(keyRange);
    DynamicSet *set = &trieSet;
    trieDebra.setActiveThreads(numProcs);
    keyNodeDebra.setActiveThreads(numProcs);
    skipDebra.setActiveThreads(numProcs);
    versionDebra.setActiveThreads(numProcs);
    if(argc != 2){
        //std::cout << "Assuming use of Jeremy's trie." << std::endl;
        set = &trieSet;
    }
    else{
        char *setType = &argv[1][2];
        if(strcmp(setType, "skip") == 0){
            set = &skipList;
        }
        else if(strcmp(setType, "list") == 0){
            set = &listSet;
        }
        else if(strcmp(setType, "augTrie") == 0){
            set = &augmentedTrie;
        }
        else{
            set = &trieSet;
        }
    }
    std::cout << "Using " << set->name() << "." << std::endl;
    randTest(1 << keyRange, 5000000, set);
    std::cout << "Completed test without incident." << std::endl;
    return 0;
}
