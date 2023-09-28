#include <chrono>
#pragma once
int threadID(int id = -1){
    static thread_local int thread_id = -1;
    if(thread_id == -1)thread_id = id;
    
    return thread_id;
}
const int NUM_THREADS = 8;
uint64_t millis(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}