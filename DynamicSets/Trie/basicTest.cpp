#include <algorithm>
#include <iostream>
#include "trie.h"
#include "trieNodeTypes.h"
using std::cout;

void testOne(){
    std::set<InsNode<AtomicCopyNotifyThreshold>*, UpdateNodeGreater> set;
    InsNode<AtomicCopyNotifyThreshold> iNode[10];
    for(int i = 0;i < 10;++i){
        iNode[i].key = i * 10;
        set.insert(&iNode[i]);
    }


    cout << "In order print out: {";
    auto it = set.begin();
    cout << (*it)->key;
    for(;it != set.end();++it){
        cout << ", " << (*it)->key;
    }
    cout << "}" << std::endl;

    cout << "upper_bound(UpdateNode with key " << iNode[5].key <<  "): ";
    cout << (*set.upper_bound(&iNode[5]))->key << std::endl;
}
int main(){
    threadID = 0;
    // Trie t(3);
    // t.insert(2);
    // int64_t x = t.predecessor(6);
    // assert(x == 2);
    testOne();
    return 0;
}
