#include <chrono>
#include <random>
#pragma once

const uint64_t Normal = 0;
const uint64_t DelFlag = 1;
const uint64_t Marked = 2; 
const uintptr_t InsFlag = 3;

const uint64_t NEXT_MASK = -8;  //61 1s, 2 0s = FFFF FFFF FFFF FFF8
const uint64_t STATUS_MASK = 7; //61 0s, 2 1s = 0000 0000 0000 0007

//The lowest 4 bits of a ListNode's successor pointer are used for the states...
//If the state is InsFlag or NotifFlag, then the information to access a descriptor node is contained as follows:
const uint64_t PROC_MASK = 0x0000000000000FF0; //Process ID contained within next lowest 8 bits
const uint64_t SEQ_MASK =  0xFFFFFFFFFFFFF000; //Sequence # contained within highest 52 bits




thread_local int threadID = -1;
const int MAX_THREADS = 512;
//Returns the number of microiseconds since the Unix epoch, January 1st 1970 at 00:00:00 UTC.
uint64_t micros(){
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
//Returns the number of milliseconds since the Unix epoch, January 1st 1970 at 00:00:00 UTC.
uint64_t millis(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

//Produces a random integer between 0 and range (inclusive).
inline int rng(int range){
    //A random number seed that is local to this thread.
	thread_local static std::random_device seed;
	//Random number generator using seed.
	thread_local static std::mt19937 rng(seed());
	//Ask for a distribution from this random number generator.
	std::uniform_int_distribution<int> dist(0,range-1);
	return dist(rng);
}
