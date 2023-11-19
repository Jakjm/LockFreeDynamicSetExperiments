#include "DynamicSet.h"
#include "trie.h"
#include "OtherDynamicSets/FRList.h"
#include "common.h"
#include "trieNodeTypes.h"
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
void randomTask2(DynamicSet *set, int range, int time, int id, std::atomic<int64_t> *insOps, std::atomic<int64_t> *remOps, std::atomic<int64_t> *predOps){
    threadID = id;
    set->initThread();

    uint64_t startTime = millis();
    uint64_t endTime = startTime + (time * 1000);

    int64_t ins = 0;
    int64_t rems = 0;
    int64_t preds = 0;
    while(millis() < endTime){
        int randomN = randomNum(range);
        int opType = randomNum(2);
        if(opType == 0){
            set->insert(randomN);
            ins += 1;
        }
        else if(opType == 1){
            set->remove(randomN);
            rems += 1;
        }
        else{
            set->predecessor(randomN);
            preds += 1;
        }
    }
    *insOps = ins;
    *remOps = rems;
    *predOps = preds;
}
void multithreadTest(int trieSize, int time, int numThreads){
    threadID = 0;
    trieRecordManager.initThread(threadID);
    Trie trieSet(trieSize);
    LinkedListSet listSet;
    DynamicSet *set = &listSet;

    std::thread *th[NUM_THREADS];
    int range = (1 << trieSize) - 1; 
    cout << "Universe of " << (range+1) << " keys" << std::endl;
    cout << "Random test of " << numThreads << " threads doing random ops for " << time << " seconds." << std::endl;
    std::atomic<int64_t> insCount[numThreads];
    std::atomic<int64_t> remCount[numThreads];
    std::atomic<int64_t> predCount[numThreads];
    for(int i = 1;i < numThreads;++i){
        th[i] = new std::thread(randomTask2, set, range, time, i, &insCount[i], &remCount[i], &predCount[i]);
    }
    randomTask2(set, range, time, 0, &insCount[0], &remCount[0], &predCount[0]);
    cout << "Thread 0 performed " << insCount[0] << " insert ops, " << remCount[0] << " remove ops and " << predCount[0] << " predecessor ops." << std::endl;
    cout << (insCount[0] / (double)time) << " inserts/sec, " << (remCount[0] / (double)time) << " removes/sec, " << (predCount[0] / (double)time) << " preds/sec." << std::endl;  
    for(int i = 1;i < numThreads;++i){
        th[i]->join();
        cout << "Thread " << i << " performed " << insCount[i] << " insert ops, " << remCount[i] << " remove ops and " << predCount[i] << " predecessor ops." << std::endl;
        cout << (insCount[i] / (double)time) << " inserts/sec, " << (remCount[i] / (double)time) << " removes/sec, " << (predCount[i] / (double)time) << " preds/sec." << std::endl;  
        delete th[i];
    }

    //trie.printInterpretedBits();
    //cout << "Verifying interpreted bits: " << std::to_string(trie.verifyInterpretedBits()) << std::endl;
}

void simpleTest(){
    threadID = 0;
    trieRecordManager.initThread(threadID);
    Trie trie(3);
    cout << "Simple test." << std::endl;
    trie.printInterpretedBits();
    trie.insert(3);
    cout << "Inserted 3" << std::endl;
    trie.printInterpretedBits();
    int pred1 = trie.predecessor(2);
    int pred2 = trie.predecessor(5);
    cout << "Pred(2): " << pred1 << " Pred(5): " << pred2 << std::endl;
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
    multithreadTest(10, 10, 8);
    //printList();
    return 0;
}
