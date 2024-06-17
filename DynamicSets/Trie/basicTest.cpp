#include <iostream>
#include "trie.h"

using std::cout;
int main(){
    threadID = 0;
    Trie t(3);
    t.insert(2);
    int64_t x = t.predecessor(6);
    assert(x == 2);
    return 0;
}
