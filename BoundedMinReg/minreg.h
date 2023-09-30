#include <cstdint>
#include <iostream>
#include <stdint.h>
#include <atomic>
#include <cassert>
#include <bit>
#include <mutex>
using std::cout;
#pragma once

//2 ^ Floor log_2 function 
//Function for rounding down to the next smallest power of 2.
//Taken from hacker's delight.
static inline int flp2(int32_t x){
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >> 16);
    return x - (x >> 1);
}

/*
*   https://oeis.org/A279521
*   Formula for calculating the number of nodes in the left subtree of a leveled binary tree.
*
*   a(n) = (n + 2^(h-1) - 1)*He + (-1)^He * (2^h - 1), where h = floor(log_2(n)) and is the height of the binary tree, 
*   He = H(-n + 3*2^(h-1) - 1) 
*   and H is the Heaviside step function (i.e., H(x) = 1 if x >= 0 and H(x) = 0 if x < 0).
*
*/
static inline int leftSubTreeSize(const int n){
    int twoH = flp2(n); // = 2^floor(log_2(n))
    int twoHminOne = twoH / 2; // = 2^(floor(log_2(n))-1)
    
    int he = (-n + 3*twoHminOne -1) >= 0;
    int heOdd = he & 1;  // = 1 if he odd, 0 if he even.
    int negOnePowHe = 1 - (heOdd << 1); // = (-1)^he

    int aN = (n + twoHminOne - 1) * he + negOnePowHe * (twoH - 1);
    return aN;
}



//Class for a Bounded Min Register using an array-based tree,
//based off of the implementation of a Max Register given by 
//Aspnes, Attiya and Censor.
template <int size>
class BoundedMinReg{
    public:
    std::atomic<char> array[size]; //An array of b atomic characters. TODO padding option?
    
    /**
    *   Tree elements are arranged like this.
    *       0
    *     1   2
    *   3  4 5  
    *   The left child of element x is at index 2x + 1, 
    *   The right child of element x is at index 2x + 2 
    */
    BoundedMinReg(int initialValue=size){
        std::fill_n(array, size, 1); //Initialize array to all 1s.
        assert(initialValue >= 0);
        assert(initialValue <= size);
        minWrite(initialValue);
    }
    void minWrite(int x, int subTreeRoot=0, int subTreeSize=size){
        if(x < 0 || x >= subTreeSize)return; //x is outside of the range of the min register.

        while(subTreeSize > 0){ //LI: subTreeRoot is the root of a minRegister with size subTreeSize
            int leftSize = leftSubTreeSize(subTreeSize); //Size of the left subtree = power of 2 less than subtree size - 1.
            if(x > leftSize){
                int sw = array[subTreeRoot];
                if(sw == 1){
                    subTreeRoot = subTreeRoot * 2 + 2;  //subTreeRoot = subTreeRoot.right
                    subTreeSize = subTreeSize - leftSize - 1; 
                    x = x - leftSize - 1;
                }
                else break;
            }
            else{
                subTreeRoot = subTreeRoot * 2 + 1; //subTreeRoot = subTreeRoot.left
                subTreeSize = leftSize;
            }
        }
        //Write 0 to switches on way back up...
        while(subTreeRoot > 0){
            int parent = (subTreeRoot - 1) >> 1;
            if(subTreeRoot & 1)array[parent] = 0; //Write 0 to parent if subTreeRoot is the parent's left child.
            subTreeRoot = parent; //subTreeRoot = subTreeRoot.parent.
        }
    }
    void minWriteRecursive(int x, int subtreeRoot=0, int subTreeSize=size){
        if(x < 0 || x >= subTreeSize)return; //x is outside of the range of the min register.
        if(subTreeSize == 0)return;
        if(subTreeSize == 1){
            if(x == 0)array[subtreeRoot] = 0;
            return;
        }
        
        int leftSize = leftSubTreeSize(subTreeSize); //Size of the left subtree = power of 2 less than subtree size - 1.
        if(x > leftSize){
            int sw = array[subtreeRoot];
            if(sw == 1){
                int rightIndex = subtreeRoot * 2 + 2;
                int rightSize = subTreeSize - leftSize - 1;
                minWrite(x - leftSize - 1, rightIndex, rightSize);
            }
        }
        else{
            int leftIndex = subtreeRoot * 2 + 1;
            minWrite(x, leftIndex, leftSize);
            array[subtreeRoot] = 0;
        }
    }
    int minRead(){
        int subTreeRoot=0, subTreeSize=size;
        int sum = 0;
        do{
            int sw = array[subTreeRoot];
            int leftSize = leftSubTreeSize(subTreeSize); 
            if(sw == 1){
                //Read the right child.
                //The size of the right subtree = subTreeSize - leftChildSubTreeSize - 1
                sum += (1 + leftSize);
                subTreeRoot = subTreeRoot * 2 + 2;
                subTreeSize = subTreeSize - leftSize - 1;
            }
            else{
                //Read the left child.
                subTreeRoot = subTreeRoot * 2 + 1;
                subTreeSize = leftSize;
            }
        }while(subTreeSize > 0);
        return sum;
    }
    int minReadRecursive(int subtreeRoot=0, int subTreeSize=size){
        if(subTreeSize == 0)return 0;
        int sw = array[subtreeRoot];
        if(subTreeSize == 1)return sw;

        int leftSize = leftSubTreeSize(subTreeSize); //Size of the left subtree = power of 2 less than subtree size - 1.
        if(sw == 1){
            //Read the right child.
            int rightIndex = subtreeRoot * 2 + 2;
            //The size of the right subtree = size - 1 
            int rightSize = subTreeSize - leftSize - 1;
            return minRead(rightIndex, rightSize) + leftSize + 1;
        }
        else{
            //Read the left child.
            int leftIndex = subtreeRoot * 2 + 1;
            return minRead(leftIndex, leftSize);
        }
    }

    void printReg(){
        int rowCounter = 0;
        int rowSize = 1;
        for(int i = 0;i < size;++i){
            cout << std::to_string(array[i]);
            ++rowCounter;
            if(rowCounter == rowSize){
                rowSize = rowSize * 2;
                rowCounter = 0;
                cout << std::endl;
            }
            else{
                cout << " ";
            }
        }
        cout << std::endl;
    }
};



//Class for a Bounded Min Register using a bit-array-based tree,
//based off of the implementation of a Max Register given by 
//Aspnes, Attiya and Censor.
//Optimized for reducing space usage...
template <int numBytes>
class BoundedMinReg2{
    private: 
        std::atomic<uint8_t> array[numBytes]; //An array of 'capacity' atomic bytes. Treated as a bit array.
        //Precondition 0 <= index <= 8*numBytes-1
        //Returns 0 or 1, the value of the bit at index 'index' within the array.
        char readBit(int index){
            int arrayCell = index / 8;
            int bitIndex = index % 8;
            uint8_t mask = 1 << (7 - bitIndex); //All 0s except for the 'bitIndex'-th bit, starting from the left.
            char bit = ((array[arrayCell] & mask) == mask); 
            return bit;
        }
        //Precondition: 0 <= index <= 8*numBytes-1
        //Write 0 to the bit at the given 'index' within the array.
        void writeZero(int index){
            int arrayCell = index / 8;
            int bitIndex = index % 8;
            uint8_t mask = UINT8_MAX - (1 << (7 - bitIndex)); //All 1s except for the 'bitIndex'-th bit, starting from the left.
            array[arrayCell].fetch_and(mask);
        }
        
    public:
    #define treeSize 8*numBytes
    
    /**
    *   Tree elements are arranged like this.
    *       0
    *     1   2
    *   3  4 5  
    *   The left child of element x is at index 2x + 1, 
    *   The right child of element x is at index 2x + 2 
    *   The parent of element x is at index (x - 1) / 2
    */
    BoundedMinReg2(int initialValue=treeSize){
        std::fill_n(array, numBytes, 0xFF); //Initialize array to 255 in every cell
        if(initialValue != treeSize)minWrite(initialValue);
    }
    //Recursive variant of min write function.
    void minWriteRecursive(int x, int subtreeRoot=0, int subTreeSize=treeSize){
        if(x < 0 || x >= subTreeSize)return; //x is outside of the range of the min register.
        if(subTreeSize == 0)return;
        if(subTreeSize == 1){
            if(x == 0)writeZero(subtreeRoot);
            return;
        }
        
        int leftSize = leftSubTreeSize(subTreeSize); 
        if(x > leftSize){
            int sw = readBit(subtreeRoot);
            if(sw == 1){
                int rightIndex = subtreeRoot * 2 + 2;
                int rightSize = subTreeSize - leftSize - 1;
                minWriteRecursive(x - leftSize - 1, rightIndex, rightSize);
            }
        }
        else{
            int leftIndex = subtreeRoot * 2 + 1;
            minWriteRecursive(x, leftIndex, leftSize);
            writeZero(subtreeRoot);
        }
    }
    void minWrite(int x){
        if(x < 0 || x >= treeSize)return; //x is outside of the range of the min register, or this write is a no-op.
        int subTreeSize=treeSize;
        int subTreeRoot=0;
        do{ //LI: subTreeRoot is the root of a minRegister with size subTreeSize
            int leftSize = leftSubTreeSize(subTreeSize); //Size of the left subtree = power of 2 less than subtree size - 1.
            if(x > leftSize){
                int sw = readBit(subTreeRoot);
                if(sw == 1){
                    //Write x - leftSize - 1 to right subtree.
                    subTreeRoot = subTreeRoot * 2 + 2;  //subTreeRoot = subTreeRoot.right
                    subTreeSize = subTreeSize - leftSize - 1; 
                    x = x - leftSize - 1;
                }
                else break;
            }
            else{
                //Write x to left subtree.
                subTreeRoot = subTreeRoot * 2 + 1; //subTreeRoot = subTreeRoot.left
                subTreeSize = leftSize;
            }
        }while(subTreeSize > 0);
        //Backtrack up to finish writes.
        while(subTreeRoot > 0){
            int parent = (subTreeRoot - 1) >> 1;
            if(subTreeRoot & 1)writeZero(parent); //Write 0 to parent if subTreeRoot is the parent's left child.
            subTreeRoot = parent; //subTreeRoot = subTreeRoot.parent.
        }
    }
    //Recursive variant of min read function
    int minReadRecursive(int subtreeRoot=0, int subTreeSize=treeSize){
        if(subTreeSize == 0)return 0;
        int sw = readBit(subtreeRoot);
        if(subTreeSize == 1)return sw;

        //The size of the left subtree.
        int leftSize = leftSubTreeSize(subTreeSize);
        if(sw == 1){
            //Read the right child.
            int rightIndex = subtreeRoot * 2 + 2;
            //The size of the right subtree.
            int rightSize = subTreeSize - leftSize - 1;
            return minReadRecursive(rightIndex, rightSize) + leftSize + 1;
        }
        else{
            //Read the left child.
            int leftIndex = subtreeRoot * 2 + 1;
            return minReadRecursive(leftIndex, leftSize);
        }
    }
    int minRead(){
        int subTreeRoot=0, subTreeSize=treeSize;
        int sum = 0;
        do{
            int sw = readBit(subTreeRoot);
            int leftSize = leftSubTreeSize(subTreeSize); 
            if(sw == 1){
                //Read the right child. Result = right.minRead + 1 + leftSize.
                //The size of the right subtree = subTreeSize - leftSize - 1.
                sum += (1 + leftSize);
                subTreeRoot = subTreeRoot * 2 + 2;
                subTreeSize = subTreeSize - leftSize - 1;
            }
            else{
                //Read the left child.
                subTreeRoot = subTreeRoot * 2 + 1;
                subTreeSize = leftSize;
            }
        }while(subTreeSize > 0);
        return sum;
    }
};

//A class that implements a fast and compact atomic 63-bounded min register.
class SmallMinReg{ 
    private: 
        std::atomic<uint64_t> value; // Treated as a bit array.        
    public:
    #define MAX_CAPACITY 63
    SmallMinReg(int initialValue=MAX_CAPACITY) : value(UINT64_MAX){
        assert(initialValue < MAX_CAPACITY);
        if(initialValue != MAX_CAPACITY)minWrite(initialValue);
    }
    void minWrite(int x){
        if(x < 0 || x >= MAX_CAPACITY)return; //x is outside of the range of the min register, or this write is a no-op.
        int subTreeSize=MAX_CAPACITY;
        int subTreeRoot=0;
        uint64_t switches = value.load();
        uint64_t write_mask = UINT64_MAX;
        do{ //LI: subTreeRoot is the root of a minRegister with size subTreeSize
            int leftSize = (subTreeSize >> 1); //Size of left subtree = size of right subtree.
            if(x > leftSize){
                uint64_t read_mask = (1 << (63 - subTreeRoot));
                int sw = ((switches & read_mask) == read_mask); //sw = value of register at bit subTreeRoot starting from the left 
                if(sw == 1){
                    subTreeRoot = subTreeRoot * 2 + 2;  //subTreeRoot = subTreeRoot.right
                    subTreeSize = leftSize; 
                    x = x - leftSize - 1;
                }
                else break;
            }
            else{
                write_mask -= (1 << (63 - subTreeRoot)); //write 0 to write_mask at bit subTreeRoot.
                subTreeRoot = subTreeRoot * 2 + 1; //subTreeRoot = subTreeRoot.left
                subTreeSize = leftSize;
                
            }
        }while(subTreeSize > 0);
        //Previously, here we did backtracking and writing...
        //Instead, now perform all of the writes atomically in one instruction!
        value.fetch_and(write_mask);
    }
    int minRead(){
        int subTreeRoot=0;
        int subTreeSize=MAX_CAPACITY;
        int sum = 0;
        uint64_t switches = value.load(); //Value of min register at start of op. Only access to shared memory!
        do{
            uint64_t read_mask = (1 << (63 - subTreeRoot));
            int sw = (switches & read_mask) == read_mask; //Read bit subTreeRoot from switches, starting from the left.
            int leftSize = subTreeSize >> 1; //Size of the left subtree = size of right subtree
            if(sw == 1){
                //Read the right child.
                //The size of the right subtree = subTreeSize - leftSize - 1.
                sum += (1 + leftSize);
                subTreeRoot = subTreeRoot * 2 + 2; //return right.minRead() + leftSize + 1
                subTreeSize = leftSize;
            }
            else{
                subTreeRoot = subTreeRoot * 2 + 1; //return left.minRead()
                subTreeSize = leftSize;
            }
        }while(subTreeSize > 0);
        return sum;
    }
};


class MinReg64{
    private:
        std::atomic<std::uint64_t> value;
    public:
    #define MAX_VALUE 64

    //Precondition: 0 <= initVal <= 64
    MinReg64(int initVal = MAX_VALUE): value(UINT64_MAX){
        if(initVal != MAX_VALUE){
            minWrite(initVal);
        }
    }
    //Precondition: 0 <= value <= 64
    void minWrite(int x){
        if(x < 64){
            uint64_t mask = ~(1 << x);  //Create an AND mask of all 1s except at bit x starting from the right.
            value.fetch_and(mask);   //Turn off bit x within value
        }
    }
    int minRead(){
        uint64_t curValue = value.load(); //
        //Using library function given here for detecting bit 
        //https://en.cppreference.com/w/cpp/header/bit
        int minValue = std::__countr_one(curValue);
        return minValue;
    }
};

class MinReg64_BAD{
    private:
        std::atomic<std::uint64_t> value;
    public:
    #define MAX_VALUE 64

    //Precondition: 0 <= initVal <= 64
    MinReg64_BAD(int initVal = MAX_VALUE): value(UINT64_MAX){
        if(initVal != MAX_VALUE){
            minWrite(initVal);
        }
    }
    //Precondition: 0 <= value <= 64
    void minWrite(int x){
        if(x < 64){
            uint64_t mask = ~(1 << x);  //Create an AND mask of all 1s except at bit x starting from the right.
            value.fetch_and(mask);   //Turn off bit x within value
        }
    }
    int minRead(){
        uint64_t curValue = value.load(); 
        //Using library function given here for detecting bit 
        //https://en.cppreference.com/w/cpp/header/bit
        int minValue = std::__countr_one(curValue);
        return minValue;
    }
};


class MutexMinReg64{
    std::mutex lock;
    uint64_t value;
    public:
    MutexMinReg64(): value(64), lock(){

    }
    void minWrite(int x){
        lock.lock();    //acquire lock
        if(x < value)value = x;
        lock.unlock();   //release lock
    }
    int minRead(){
        lock.lock();    //acquire lock
        int min = value; 
        lock.unlock();  //release lock
        return min;
    }
};
class LockMinReg64{
    std::atomic_flag lock;
    uint64_t value;
    public:
    LockMinReg64(): value(64), lock(ATOMIC_FLAG_INIT){
   
    }
    void minWrite(int x){
        while (lock.test_and_set(std::memory_order_acquire)) {  // acquire lock
            #if defined(__cpp_lib_atomic_flag_test)
                while (lock.test(std::memory_order_relaxed));
            #endif
            
        }
        if(x < value)value = x;
        lock.clear(std::memory_order_release);   //release lock
    }
    int minRead(){
        while (lock.test_and_set(std::memory_order_acquire)) {  // acquire lock
            #if defined(__cpp_lib_atomic_flag_test)
                while (lock.test(std::memory_order_relaxed));
            #endif
            
        }
        int min = value; //This read operation must be done before clearing the lock; so we use memory order release
        lock.clear(std::memory_order_release);   //release lock
        return min;
    }
};



