#include "DynamicSet.h"
#include "trie.h"
#include "OtherDynamicSets/FRSkipList.h"
#include "OtherDynamicSets/FRList.h"
#include "common.h"
#include "trieNodeTypes.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <format>
#include <thread>

using std::cout;


/**
* This function has a process repeatedly perform update operations and predecessor operations on a dynamic set.
* For `time' seconds, a process will flip a coin to determine if it should perform an update operation or a predecessor operation.
* If performing an update operation, another coin is flipped to determine if the update operation will insert or remove a key from the set.
* Then the process will perform this update or predecessor operation with a randomly generated integer key, from 0 to range inclusive.
* id is the ID of the process performing the experiment.
*/
void randomExperiment(DynamicSet *set, int64_t range, int time, int id, int64_t volatile *numOps){
    threadID = id;

    uint64_t startTime = millis();
    uint64_t endTime = startTime + (time * 1000);
    

    int64_t opCount = 0;
    uint64_t currentTime = startTime;
    //uint64_t nextDatapoint = startTime + 50; //Record data points every 50 ms
    while(currentTime < endTime){
        // if(currentTime > nextDatapoint){
        //     dataPoints->push_back(opCount);
        //     nextDatapoint = currentTime + 50;
        // }
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
        currentTime = millis();
    }
    *numOps = opCount;
}
void calcTime(long millis, int &hours, int &minutes, int &seconds){
    millis -= (5 * 60 * 60 * 1000); //Subtract five hours for EST.
    long numDays = millis / (60 * 60 * 24 * 1000);
    long time = millis - numDays * (60 * 60 * 24 * 1000);
    hours = time / (60 * 60 * 1000);
    minutes = time % (60 * 60 * 1000) / 60000;
    seconds = time % (60000) / 1000; 

}
// void outputData(int time, vector<int64_t> data[NUM_THREADS]){
//     FILE *f = fopen("threadData.txt","w");
//     for(int i = 0;i < NUM_THREADS;++i){
//         fprintf(f, "%d ",i);
//     }
//     fprintf(f, "\n");
//     for(uint64_t dp = 0;dp < (uint64_t)time * 20;++dp){
//         for(int i = 0;i < NUM_THREADS;++i){
//             if(data[i].size() > dp){
//                 fprintf(f, "%ld ", data[i][dp]);
//             }
//             else{
//                 fprintf(f, "  ");
//             }
//         }
//         fprintf(f, "\n");
//     }
//     fclose(f);
// }
void multithreadTest(){
    threadID=0;

    int time = 5; //The duration of the test in seconds.
    const int trieHeight = 20; //The height of the trie.
    int range = (1 << trieHeight) - 1; //The number of keys = 2^(trie height) - 1

    Trie<trieHeight> trieSet;
    LinkedListSet listSet;
    SkipListSet<20> skipSet;
    DynamicSet *set = &trieSet;

    std::thread *th[NUM_THREADS];

    //Prefill the set to 50% full
    std::set<int64_t> valSet;
    while(valSet.size() < ((uint64_t)range / 2)){
        int64_t key = randomNum(range);
        set->insert(key);
        valSet.insert(key);
    }
    
    volatile int64_t opCount[NUM_THREADS];
    //vector<int64_t> dataPoints[NUM_THREADS];

    cout << "Universe of " << (range+1) << " keys" << std::endl;
    cout << "Random test of " << NUM_THREADS << " threads doing random ops for " << time << " seconds." << std::endl;
    int hours, minutes, seconds;
    calcTime(millis(), hours, minutes, seconds);
    printf("Test starting at %02d:%02d:%02d\n",hours,minutes,seconds);
    calcTime(millis() + time * 1000, hours, minutes, seconds);
    printf("Test will finish at %02d:%02d:%02d\n",hours,minutes,seconds);
    
    //Allocate NUMTHREADS threads
    for(int i = 0;i < NUM_THREADS;++i){
        th[i] = new std::thread(randomExperiment, set, range, time, i, &opCount[i]);
    }
    
    //outputData(time, dataPoints);

    for(int i = 0;i < NUM_THREADS;++i){
        th[i]->join();
        cout << "Thread " << i << " performed " << opCount[i] << " ops, for an average of ";
        cout << std::setprecision(8) << ((double)opCount[i] / time) << " ops/sec." << std::endl;
        delete th[i];
    }
}

void simpleTest(){
    threadID = 0;
    Trie<3> trie;
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


void skipTest(){
    threadID = 0;
    SkipListSet<5> skipSet;
    skipSet.printList();
    for(int i = 0;i < 20;++i){
        skipSet.insert(randomNum(50));
    }
    skipSet.printList();
    for(int i = 0;i < 20;++i){
        skipSet.remove(randomNum(50));
    }
    skipSet.printList();
}

int main(int argc, char **argv){
    //skipTest();
    multithreadTest();
    return 0;
}
