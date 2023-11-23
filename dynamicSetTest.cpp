#include "DynamicSet.h"
#include "trie.h"
#include "OtherDynamicSets/FRList.h"
#include "common.h"
#include "trieNodeTypes.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

using std::cout;


/**
* This function has a process repeatedly perform update operations and predecessor operations on a dynamic set.
* For `time' seconds, a process will flip a coin to determine if it should perform an update operation or a predecessor operation.
* If performing an update operation, another coin is flipped to determine if the update operation will insert or remove a key from the set.
* Then the process will perform this update or predecessor operation with a randomly generated integer key, from 0 to range inclusive.
* id is the ID of the process performing the experiment.
*/
void randomExperiment(DynamicSet *set, int64_t range, int time, int id, std::atomic<int64_t> *numOps){
    threadID = id;
    set->initThread();

    uint64_t startTime = millis();
    uint64_t endTime = startTime + (time * 1000);

    int64_t opCount = 0;
    while(millis() < endTime){
        int64_t key = randomNum(range);
        int coinFlip = randomNum(1);
        if(coinFlip){ //Process should perform an update operation.
            coinFlip = randomNum(1); //Randomly chose whether to insert key
            if(coinFlip)set->insert(key);
            else set->remove(key);
        }
        else{
            set->predecessor(key);
        }
        ++opCount;
    }
    *numOps = opCount;
}
void multithreadTest(){
    threadID=0;

    int time = 10; //The duration of the test in seconds.
    int trieHeight = 15; //The height of the trie.
    int range = (1 << trieHeight) - 1; //The number of keys = 2^(trie height) - 1

    Trie trieSet(trieHeight);
    LinkedListSet listSet;
    DynamicSet *set = &trieSet;
    std::thread *th[NUM_THREADS];
    
    std::atomic<int64_t> opCount[NUM_THREADS];

    cout << "Universe of " << (range+1) << " keys" << std::endl;
    cout << "Random test of " << NUM_THREADS << " threads doing random ops for " << time << " seconds." << std::endl;
    
    //Allocate NUMTHREADS threads
    for(int i = 0;i < NUM_THREADS;++i){
        th[i] = new std::thread(randomExperiment, set, range, time, i, &opCount[i]);
    }

    for(int i = 0;i < NUM_THREADS;++i){
        th[i]->join();
        cout << "Thread " << i << " performed " << opCount[i] << " ops, for an average of ";
        cout << std::setprecision(8) << ((double)opCount[i] / time) << " ops/sec." << std::endl;
        delete th[i];
    }
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

int main(int argc, char **argv){
    multithreadTest();
    return 0;
}
