#include "DynamicSet.h"
#include "trie.h"
#include "OtherDynamicSets/FRSkipList.h"
#include "OtherDynamicSets/FRList.h"
#include "common.h"
#include "trieNodeTypes.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <string.h>
#include <thread>


using std::cout;
#define NUM_OPS_BEFORE_TIME_CHECK 50 //The number of operations that should be performed in between each check if the experiment is over.

class PADDED_INT{
    volatile uint64_t i;
    volatile char padding[64 - sizeof(int64_t)]; 
    public:
    PADDED_INT(): i(0) {

    }
    void operator= (const uint64_t& val) volatile{
        i=val;
    }
    uint64_t value() volatile{
        return i;
    }
};
//Data type that holds data shared by threads performing the experiment.
struct ExperimentData{
    std::atomic<int32_t> numReady;
    volatile char padding[64 - sizeof(int32_t)];
    std::atomic<bool> done;
    volatile char padding2[64 - sizeof(bool)];
    volatile PADDED_INT opCount[MAX_THREADS];
    volatile PADDED_INT startTime[MAX_THREADS];
    volatile PADDED_INT endTime[MAX_THREADS];
    ExperimentData() : numReady(0), done(false){

    }
};

/**
* This function has a process repeatedly perform update operations and predecessor operations on a dynamic set.
* For `time' seconds, a process will flip a coin to determine if it should perform an update operation or a predecessor operation.
* If performing an update operation, another coin is flipped to determine if the update operation will insert or remove a key from the set.
* Then the process will perform this update or predecessor operation with a randomly generated integer key, from 0 to range inclusive.
* id is the ID of the process performing the experiment.
*/
void randomExperiment(DynamicSet *set, int numProcs, int64_t range, double time, int id, ExperimentData *data){
    threadID = id;
    uint64_t opCount = 0;
    
    ++data->numReady;
    while(data->numReady != numProcs){ //Continue iterating while not all threads are ready...
    }

    uint64_t startTime = micros();
    uint64_t currentTime = startTime;
    uint64_t endTime = startTime + (time * 1000000);
    while(currentTime < endTime && !(data->done)){
        //Perform a series of operations....
        for(int i = 0;i < NUM_OPS_BEFORE_TIME_CHECK;++i){
            int64_t key = randomNum(range); //Key of the operation that will be performed...
            int coinFlip = randomNum(1);
            if(coinFlip){ //Process should perform an update operation.
                coinFlip = randomNum(1); //Randomly chose whether to insert or remove key
                if(coinFlip)set->insert(key); //25% chance of inserting key
                else set->remove(key); //25% chance for removing key
            }
            else{
                set->predecessor(key); //50% chance of performing predecessor(key)
            }
        }
        opCount += NUM_OPS_BEFORE_TIME_CHECK;
        currentTime = micros();
    }
    data->done = true;
    uint64_t actualEndTime = micros();
    data->opCount[threadID] = opCount;
    data->startTime[threadID] = startTime;
    data->endTime[threadID] = actualEndTime;
}
void calcTime(long millis, int &hours, int &minutes, int &seconds){
    millis -= (5 * 60 * 60 * 1000); //Subtract five hours for EST.
    long numDays = millis / (60 * 60 * 24 * 1000);
    long time = millis - numDays * (60 * 60 * 24 * 1000);
    hours = time / (60 * 60 * 1000);
    minutes = time % (60 * 60 * 1000) / 60000;
    seconds = time % (60000) / 1000; 

}
// void outputData(int time, vector<int64_t> data[MAX_THREADS]){
//     FILE *f = fopen("threadData.txt","w");
//     for(int i = 0;i < MAX_THREADS;++i){
//         fprintf(f, "%d ",i);
//     }
//     fprintf(f, "\n");
//     for(uint64_t dp = 0;dp < (uint64_t)time * 20;++dp){
//         for(int i = 0;i < MAX_THREADS;++i){
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

//time is the duration of the test in seconds.
//numProcs is the number of threads that will participate in the test.
//trieHeight is the base-2 logarithm of the universe size.
//prefill is the amount the data structure should be prefilled prior to testing.
void multithreadTest(char *setType, double time, int numProcs, int trieHeight, double prefill, bool verbose){
    threadID=0;
    
    int universeSize = (1 << trieHeight); //The number of keys in the universe.
    int range = universeSize - 1; //The maximum key in the universe.

    Trie trieSet(trieHeight);
    LinkedListSet listSet;
    SkipListSet<20> skipList;
    DynamicSet *set;
    if(strcmp(setType,"trie") == 0){
        set = &trieSet;
    }
    else if(strcmp(setType, "list") == 0){
        set = &listSet;
    }
    else if(strcmp(setType, "skip") == 0){
        set = &skipList;
    }
    else{
        perror("Did not recognize set type.\n");
        exit(1);
    }

    std::thread *th[MAX_THREADS];

    //Prefill the set to 50% full
    std::set<int64_t> valSet;
    while(valSet.size() < ceil(universeSize * prefill)){
        int64_t key = randomNum(range);
        set->insert(key);
        valSet.insert(key);
    }
    
    ExperimentData data;
    //volatile int64_t opCount[MAX_THREADS];
    //vector<int64_t> dataPoints[MAX_THREADS];
    if(verbose){
        cout << numProcs << " threads performing random ops for " << std::setprecision(5) << time << " seconds on " << set->name() << "." << std::endl;
        cout <<"Prior to start, the structure was filled with exactly " << valSet.size() << " keys." << std::endl;
        cout << "50% Update Operations, 50% Predecessor Ops, keys drawn uniformly from " << "Universe of " << (universeSize) << " keys" << std::endl;
    
        int hours, minutes, seconds;
        calcTime(millis(), hours, minutes, seconds);
        printf("Test starting at %02d:%02d:%02d\n",hours,minutes,seconds);
        calcTime(millis() + time * 1000, hours, minutes, seconds);
        printf("Test will finish at %02d:%02d:%02d\n",hours,minutes,seconds);
    }
    
    //Allocate NUMTHREADS threads
    for(int i = 0;i < numProcs;++i){
        th[i] = new std::thread(randomExperiment, set, numProcs, range, time, i, &data);
    }
    
    //Put the thread asleep for time seconds + 50 milliseconds to ensure it's not consuming cpu time.
    long sleep_duration = time * 1000 + 50;
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration));

    long totalThroughput = 0;
    for(int i = 0;i < numProcs;++i){
        th[i]->join();
        totalThroughput += data.opCount[i].value();
        if(verbose){
            cout << "Thread " << i << " performed " << data.opCount[i].value() << " ops, for an average of ";
            cout << std::setprecision(8) << ((double)data.opCount[i].value() / time) << " ops/sec." << std::endl;
        }
        //cout << data.startTime[i].value() << " " << data.endTime[i].value() << std::endl;
        delete th[i];
    }
    cout << "Total throughput (in ops): " << totalThroughput << std::endl;
}

void simpleTest(){
    threadID = 0;
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

//Test for the skip list...
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
    
    double runtime = 5.0;
    int numProcs = 4;
    double prefill = 0.5;
    int keyRange = 20;
    bool verbose = false;
    char defaultType[5] = "trie";
    char *setType = NULL;

    if(argc == 2 && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))){
            cout << "Usage: ./dynamicSetTest [options]" << std::endl;
            cout << "Options:"<< std::endl;
            cout << "\t-h, --help\t\t\t\t\t\tDisplay information on running dynamic test."<< std::endl;
            cout << "\t-t, --time <T>\t\t\t\t\tRun the experiment for <T> seconds."<< std::endl;
            cout << "\t-n, --numProcs <N>\t\t\t\tRun the experiment with <N> processes (threads)."<< std::endl;
            cout << "\t-k, --keyRange <K>\t\t\t\tRun the experiment with a universe of 2^K keys." << std::endl;
            cout << "\t-v, --verbose\t\t\t\t\tPrint additional information about the test." << std::endl;
            cout << "\t--skip\t\t\t\t\t\tPerform the experiment on the Fomitchev-Ruppert skip list." << std::endl;
            cout << "\t--list\t\t\t\t\t\tPerform the experiment on the Fomitchev-Ruppert linked list." << std::endl;
            cout << "\t--trie\t\t\t\t\t\tPerform the experiment on Jeremy's Binary Trie." << std::endl;
            return 0;
    }
    else{ 
        int curArg = 1;
        while(curArg < argc){
            char *currentParam = argv[curArg];
            char *paramSetting = argv[curArg+1];
            if(curArg + 1 < argc && (strcmp(currentParam, "-t") == 0 || strcmp(currentParam, "--time") == 0)){
                runtime = strtod(paramSetting, nullptr);
                if(runtime <= 0){
                    cout << "Command line arguments invalid. Test length must be a positive number of seconds." << std::endl;
                    exit(1);
                }
                curArg += 2;  
            }
            else if(curArg + 1 < argc && (strcmp(currentParam, "-n") == 0 || strcmp(currentParam, "--numProcs") == 0)){
                numProcs = strtol(paramSetting, nullptr, 10);
                if(numProcs < 1 || numProcs > MAX_THREADS){
                    cout << "Command line arguments invalid. Number of threads must be in range [1, 512] inclusive." << std::endl;
                    exit(1);
                }
                curArg += 2;
            }
            else if(curArg + 1 < argc && (strcmp(currentParam, "-k") == 0 || strcmp(currentParam, "--keyRange") == 0)){
                keyRange = strtod(paramSetting, nullptr);
                if(keyRange <= 2 || keyRange >= 25){
                    cout << "Key range outside of acceptable limit." << std::endl;
                    exit(1);
                }
                curArg += 2;
            }
            else if(!verbose && (strcmp(currentParam, "-v") == 0 || strcmp(currentParam, "--verbose") == 0)){
                verbose = true;
                curArg += 1;
            }
            else if(!setType){
                if(strcmp(currentParam, "--list") == 0 || strcmp(currentParam, "--skip") == 0 || strcmp(currentParam, "--trie") == 0){
                    setType = &currentParam[2]; //Remove two dashes...
                }
                else{
                    cout << "Did not recognize command line arguments." << std::endl;
                    exit(1);
                }
                curArg += 1;
            }
            else{
                cout << "Did not recognize command line arguments." << std::endl;
                exit(1);
            }
        }
    }
    if(!setType)setType = defaultType;
    multithreadTest(setType, runtime, numProcs, keyRange, prefill, verbose);
    return 0;
}
