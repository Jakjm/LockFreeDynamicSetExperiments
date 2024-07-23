#include "prefixes.h"
#include <algorithm>
#include "skiptrie.h"


int main(){
    int x = 200;
    int prefix = getPrefix(x,4,8);
    std::cout << "Length 4 prefix of 200 within 8 bits:" << prefix << std::endl;

    prefix = 7;
    std::cout << "Merging 11 with bit 1 through 4 of 200 (1001):" << mergePrefix(prefix,200,1,4,8) << std::endl;

    std::cout << "Is 11 a len 2 prefix of 200 within 8 bits:" << isPrefix(7, 200, 8) << std::endl;
    std::cout << "Is empty a len 2 prefix of 200 within 8 bits:" << isPrefix(1, 200, 8) << std::endl;
    std::cout << "Is 111 a len 3 prefix of 200 within 8 bits:" << isPrefix(15, 200, 8) << std::endl;

    SkipTrie<5> trie(20);
    trie.insert(5);
    trie.remove(5);
}