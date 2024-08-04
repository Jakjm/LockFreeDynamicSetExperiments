#include "prefixes.h"
#include <cassert>
int main(){
    for(int i = 0; i < 256;++i){
        assert(isPrefix(1,i,8));
    }
    for(int i = 0; i < 128;++i){
        assert(isPrefix(2,i,8) && !isPrefix(3,i,8));
    }
    for(int i = 128;i < 256;++i){
        assert(!isPrefix(2,i,8) && isPrefix(3,i,8));
    }
    
    for(int i = 0; i < 64;++i){
        assert(isPrefix(4,i,8));
        assert(!isPrefix(5,i,8));
        assert(!isPrefix(6,i,8));
        assert(!isPrefix(7,i,8));
    }
    for(int i = 64; i < 128;++i){
        assert(!isPrefix(4,i,8));
        assert(isPrefix(5,i,8));
        assert(!isPrefix(6,i,8));
        assert(!isPrefix(7,i,8));
    }
    for(int i = 128; i < 192;++i){
        assert(!isPrefix(4,i,8));
        assert(!isPrefix(5,i,8));
        assert(isPrefix(6,i,8));
        assert(!isPrefix(7,i,8));
    }
    for(int i = 192; i < 256;++i){
        assert(!isPrefix(4,i,8));
        assert(!isPrefix(5,i,8));
        assert(!isPrefix(6,i,8));
        assert(isPrefix(7,i,8));
    }

    assert(isPrefix(1024,493,20));
}