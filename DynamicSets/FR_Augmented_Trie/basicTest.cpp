#include <iostream>
#include "augmentedTrie.h"
#include "../../common.h"
using std::cout;

int main(){
    threadID = 0;
    AS_Trie t(3);
    cout << "Search for 5: " << t.search(5) << std::endl;

    // for(int i = 0;i < 8;++i){
    //     t.insert(i);
    //     t.remove(i);
    t.insert(4);
    // }
    t.insert(5);
    cout << "Inserted 4 and 5. Search for 5: " << t.search(5) << std::endl;

    cout << "Predecessor 7: " << t.predecessor(7) << std::endl;
    cout << "Predecessor 5: " << t.predecessor(5) << std::endl;

    t.remove(5);
    cout << "Removed 5. Search for 5: " << t.search(5) << std::endl;
    
    cout << "Predecessor 7: " << t.predecessor(7) << std::endl;
    cout << "Predecessor 5: " << t.predecessor(5) << std::endl;
    return 0;
}