#include "prefixes.h"
#include <algorithm>
#include "skiptrie.h"
#include <iostream>

int main(){
	threadID = 0;    
	SkipTrie<5> trie(20);
	// for(int i = 512; i >= 0;--i){
	// 	trie.insert(i);
	// }
    for(int i = 0; i <= 512;++i){
        trie.insert(i);
    }
	std::cout << "backsteps: " << backsteps << std::endl;
	// trie.insert(5);
    // trie.remove(5);
    // DCSS_PTR<int64_t> ptr(0);
    // std::cout << "Should be 0: " << ptr.read() << std::endl;
    // ptr.cas(0, 256);
    // std::cout << "Should be 256: " << ptr.read() << std::endl;

    // std::atomic<uintptr_t> x;
    // x = 0;
    // bool result = ptr.dcss(&x,0, 256, 512);
    // std::cout << "Should be 512: " << ptr.read() << std::endl;
    // std::cout << "Shoudl be true: " << result << std::endl;
}
