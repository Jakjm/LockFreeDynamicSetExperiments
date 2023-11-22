#include <chrono>
#include <random>
#pragma once

thread_local int threadID = -1;
const int NUM_THREADS = 8;
uint64_t millis(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

//Produces a random integer between 0 and range (inclusive).
inline int randomNum(int range){
    //A random number seed that is local to this thread.
	thread_local static std::random_device seed;
	//Random number generator using seed.
	thread_local static std::mt19937 rng(seed());
	//Ask for a distribution from this random number generator.
	std::uniform_int_distribution<int> dist(0,range);
	return dist(rng);
}
