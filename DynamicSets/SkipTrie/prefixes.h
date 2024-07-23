#include <cstdint>
#include <bit>
#include <iostream>

//Gets the integer representing the len-bit prefix of k within size bits
int64_t getPrefix(const int64_t k, const int len, const int size){
    int64_t mask = (1 << len) - 1;
    int64_t prefix = (1 << len) + ((k >> (size - len)) & mask);
    return prefix;
}


//Considering the integer k as a length size integer,
//Obtains the bitstring from bit start in k to bit k + len - 1.
//Returns this bitstring appended to prefix.
int64_t mergePrefix(const int64_t prefix, const int64_t k, const int64_t start, const int len, const int size){
    int64_t newPrefix = prefix << len;
    int64_t mask = (1 << len) - 1;
    int64_t suffix = (k >> (size - (len + start))) & mask;
    return newPrefix + suffix;
}

//100001111  len = 9
//    11111  len = 4

bool isPrefix(const int64_t p, const int64_t x, const int size){
    int len = std::__bit_width(p) - 1;
    int64_t mask = (1 << len) - 1;
    return (p & mask) == ((x >> (size - len)) & mask); 
}
