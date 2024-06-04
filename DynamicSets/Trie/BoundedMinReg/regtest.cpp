#include "minreg.h"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>
using std::cout;
//Returns absolute time in milliseconds.
uint64_t millis(){
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

template <class RegClass>
bool writeAndRead(RegClass &reg, int value, int expectedValue){
    reg.minWrite(value);
    int read = reg.minRead();
    cout << "Min reg value after writing " << std::to_string(value) << ": "   << read << 
            ", expected " << expectedValue << "." << std::endl;
    return read == expectedValue;
}

template <class RegClass>
void consumer(RegClass *reg, uint64_t endTime){
    int64_t count = 0;
    while(millis() != endTime){
        reg->minRead();
        ++count;
    }
    cout << ("Consumer performed " + std::to_string(count) +  " minRead ops!\n"); 
}
template <class RegClass>
void producer(RegClass *reg, uint64_t endTime){
    int64_t count = 0;
    while(millis() != endTime){
        reg->minWrite(endTime % 65);
        ++count;
    }
    cout << ("Producer performed " + std::to_string(count) + " minWrite ops!\n"); 
}


template <class RegClass>
void testReg(){
    RegClass reg;

    uint64_t startTime = millis();
    uint64_t endTime = startTime + 5000;
    std::thread t1(producer<RegClass>, &reg, endTime);
    std::thread t2(producer<RegClass>, &reg, endTime);
    std::thread t3(consumer<RegClass>, &reg, endTime);
    std::thread t4(consumer<RegClass>, &reg, endTime);
    t1.join();
    t2.join();
    t3.join();
    t4.join();

}

void speedTest(){
    cout << "Testing spin-lock based min register\n" << std::endl;
    testReg<LockMinReg64>();
    cout << "\nTesting mutex-based min register\n" << std::endl;
    testReg<MutexMinReg64>();
    cout << "\nTesting wait-free min register\n" << std::endl;
    testReg<MinReg>();
}


template <class RegClass>
void regTest(){
    RegClass r(64);
    cout << "Min reg initial value: " << r.minRead() << std::endl;

    assert(writeAndRead(r,10,10));
    assert(writeAndRead(r, 500,10));
    assert(writeAndRead(r,16,10));
    assert(writeAndRead(r, -1, 10));

    cout << std::endl;

    assert(writeAndRead(r, 10,10));
    assert(writeAndRead(r,9,9));
    assert(writeAndRead(r, 10, 9));

    cout << std::endl;

    assert(writeAndRead(r, 500,9));
    assert(writeAndRead(r, 4, 4));
    for(int i = 5;i < 12;++i){
        assert(writeAndRead(r, i, 4));
    }

    cout << std::endl;

    assert(writeAndRead(r, 2, 2));
    for(int i = 3;i < 12;++i){
        assert(writeAndRead(r, i, 2));
    }
    assert(writeAndRead(r,-1,2));
    assert(writeAndRead(r, 500, 2));
    
    cout << std::endl;

    assert(writeAndRead(r, 0, 0));
    for(int i = 3;i < 12;++i){
        assert(writeAndRead(r, i, 0));
    }
    assert(writeAndRead(r,-1,0));
    assert(writeAndRead(r, 1000, 0));
}  




int main(){
    //regTest<MinReg64>();
    speedTest();
}
