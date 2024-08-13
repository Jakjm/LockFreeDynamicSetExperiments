#include <algorithm>
#include <iostream>

#include <vector>
#include "../DynamicSet.h"
#include "trieNodeTypes.h"
#include "trie.h"
using std::cout;

void testOne(){
    std::vector<InsNode*> set;
    InsNode iNode[10];
    for(int i = 4;i < 10;++i){
        iNode[i].key = i * 10;
        set.push_back(&iNode[i]);
    }


    cout << "In order print out: {";
    auto it = set.begin();
    cout << (*it)->key;
    ++it;
    for(;it != set.end();++it){
        cout << ", " << (*it)->key;
    }
    cout << "}" << std::endl;

    cout << "upper_bound(UpdateNode with key " << iNode[5].key <<  "): ";
    auto it2  = std::upper_bound(set.rbegin(), set.rend(), &iNode[5],UpdateNodeGreater());
    cout << *it2;
    //cout << (*set.upper_bound(&iNode[5]))->key << std::endl;
}
int main(){
    threadID = 0;
    // Trie t(3);
    // t.insert(2);
    // int64_t x = t.predecessor(6);
    // assert(x == 2);
    //testOne();
    DelNode x(0);    
	cout << ((uintptr_t)&x.lower1Boundary - (uintptr_t)&x) << std::endl;
    return 0;
}
