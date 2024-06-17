#include "minreg.h"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>
using std::cout;

bool testOne(){
    MinReg reg;
    assert(reg.minRead() == 64);
    reg.minWrite(64);
    assert(reg.minRead() == 64);
    reg.minWrite(50);
    assert(reg.minRead() == 50);
    reg.minWrite(60);
    assert(reg.minRead() == 50);
    reg.minWrite(50);
    assert(reg.minRead() == 50);
    reg.minWrite(20);
    assert(reg.minRead() == 20);
    reg.minWrite(50);
    assert(reg.minRead() == 20);
    reg.minWrite(15);
    assert(reg.minRead() == 15);
    reg.minWrite(10);
    assert(reg.minRead() == 10);
    for(int i =10;i < 50;++i){
        reg.minWrite(i);
    }
    assert(reg.minRead() == 10);
    reg.minWrite(0);
    assert(reg.minRead() == 0);
    for(int i =0;i < 64;++i){
        reg.minWrite(i);
    }
    assert(reg.minRead() == 0);
    return true;
}
int main(){
    if(testOne()){
        std::cout << "Passed min register test." << std::endl;
    }
}
