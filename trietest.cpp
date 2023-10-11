#include "trie.h"
#include "common.h"
#include <iostream>
#include <string>
#include <thread>
#include <random>
using std::cout;

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
void randomTask2(Trie *trie, int range, int testSize, int id){
    threadID = id;
    for(int i = 0;i < testSize;++i){
        int randomN = randomNum(range);
        int opType = randomNum(2);
        if(opType == 0){
            trie->insert(randomN);
        }
        else if(opType == 1){
            trie->remove(randomN);
        }
        else{
            trie->predecessor(randomN);
        } 
    }
}
void multithreadTest(int trieSize, int testSize, int numThreads){
    threadID = 0;
    Trie trie(trieSize);
    std::thread *th[NUM_THREADS];
    int range = (1 << trieSize) - 1; 
    cout << "Random test of " << numThreads << " threads doing " << testSize << " random ops each." << std::endl;
    for(int i = 1;i < numThreads;++i){
        th[i] = new std::thread(randomTask2, &trie, range, testSize, i);
    }
    randomTask2(&trie, range, testSize, 0);
    for(int i = 1;i < numThreads;++i){
        th[i]->join();
        delete th[i];
    }

    trie.printInterpretedBits();
    cout << "Verifying interpreted bits: " << std::to_string(trie.verifyInterpretedBits()) << std::endl;
}

void simpleTest(){
    threadID = 0;
    Trie trie(3);
    cout << "Simple test." << std::endl;
    trie.printInterpretedBits();
    trie.insert(3);
    cout << "Inserted 3" << std::endl;
    trie.printInterpretedBits();
    cout << "Pred(2): " << trie.predecessor(2) << " Pred(5): " << trie.predecessor(5) << std::endl;
    trie.insert(5);
    cout << "Inserted 5" << std::endl;
    trie.printInterpretedBits();
    cout << "Pred(5): " << trie.predecessor(5) << " Pred(7): " << trie.predecessor(7) << std::endl;
    cout << "Del(3)" << std::endl;
    trie.remove(3);
    cout << "Del(5)" << std::endl;
    trie.printInterpretedBits();
}

int compare(int i1, int i2){
    return i1 - i2;
}

int main(int argc, char **argv){
    //simpleTest();
    multithreadTest(3, 1000000, 4);
    //printList();
    return 0;
}