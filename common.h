#include <chrono>
#pragma once

thread_local int threadID = -1;
const int NUM_THREADS = 8;
uint64_t millis(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}