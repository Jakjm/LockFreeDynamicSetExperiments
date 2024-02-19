#include "DynamicSet.h"
#include "trie.h"
#include "OtherDynamicSets/FRSkipList.h"
#include "OtherDynamicSets/FRList.h"
#include "common.h"
#include "trieNodeTypes.h"
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string.h>
#include <thread>
#include <unistd.h>


using std::cout;
#define NUM_OPS_BEFORE_TIME_CHECK 50 //The number of operations that should be performed in between each check if the experiment is over.


struct ProcessData{
    volatile uint64_t opCount;
    volatile uint64_t startTime;
    volatile uint64_t endTime;
    volatile char padding [64 - 3*sizeof(uint64_t)];
};


//Data type that holds data shared by threads performing the experiment.
struct ExperimentData{
    std::atomic<int32_t> numReady;
    volatile char padding[64 - sizeof(int32_t)];
    std::atomic<bool> done;
    volatile char padding2[64 - sizeof(bool)];
    ProcessData pData[MAX_THREADS];
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
void randomExperiment(DynamicSet *set, int numProcs, int64_t universeSize, double time, int id, ExperimentData *data, long insertRate, long removeRate, long predRate){
    threadID = id;
    uint64_t opCount = 0;
    
    ++data->numReady;
    while(data->numReady != numProcs){ //Continue iterating while not all threads are ready...
    }

    uint64_t startTime = micros();
    uint64_t currentTime = startTime;
    uint64_t endTime = startTime + (time * 1000000);
    int ratioTotal = insertRate + removeRate + predRate;
    while(currentTime < endTime && !(data->done)){
        //Perform a series of operations....
        for(int i = 0;i < NUM_OPS_BEFORE_TIME_CHECK;++i){
            int64_t key = rng(universeSize); //Key of the operation that will be performed...
            int coinFlip = rng(ratioTotal);
            if(coinFlip < insertRate){ //Perform an insert operation at insertRate / ratioTotal percent chance.
                set->insert(key); 
            }
            else if(coinFlip < (insertRate + removeRate)){ //Perform a remove operation at removeRate / ratioTotal percent chance.
                set->remove(key);
            }
            else{ //Perform a predecessor operation at predRate / ratioTotal percent chance.
                set->predecessor(key);
            }
        }
        opCount += NUM_OPS_BEFORE_TIME_CHECK;
        currentTime = micros();
    }
    data->done = true;
    uint64_t actualEndTime = micros();
    ProcessData &pData = data->pData[threadID];

    pData.opCount = opCount;
    pData.startTime = startTime;
    pData.endTime = actualEndTime;
}
//Converts current time in milliseconds millis into the current hour, minute and second in Eastern Time.
//millis is the current time in milliseconds.
void calcTime(long millis, int &hours, int &minutes, int &seconds){
    millis -= (5 * 60 * 60 * 1000); //Subtract five hours for EST.
    long numDays = millis / (60 * 60 * 24 * 1000);
    long time = millis - numDays * (60 * 60 * 24 * 1000);
    hours = time / (60 * 60 * 1000);
    minutes = time % (60 * 60 * 1000) / 60000;
    seconds = time % (60000) / 1000; 

}
void microsToStr(uint64_t micros, char *str){
    micros -= (5 * 60 * 60 * (uint64_t)1000000);
    uint64_t microsInDay = (uint64_t)60 * 60 * 24 * 1000000;
    micros -= (micros / microsInDay) * microsInDay;
    uint64_t hours = micros / (60 * 60 * (uint64_t)1000000);
    uint64_t mins = (micros % (60 * 60 * (uint64_t)1000000)) / (60 * 1000000);
    double seconds = (double)(micros % (60 * (uint64_t)1000000)) / 1000000;
    sprintf(str,"%02ld:%02ld:%02.6f",hours,mins,seconds);
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
void multithreadTest(char *setType, double time, int numProcs, int trieHeight, double prefill, bool verbose, int insertRate, int removeRate, int predRate){
    threadID=0;
    
    int universeSize = (1 << trieHeight); //The number of keys in the universe.

    Trie<NotifDescNotifyThreshold> trieSetNotifDesc(trieHeight);
    Trie<AtomicCopyNotifyThreshold> trieSetSwCopy(trieHeight);
    LinkedListSet listSet;
    SkipListSet<25> skipList;
    
    trieDebra.setActiveThreads(numProcs);
    //bufferDebra.setActiveThreads(numProcs);
    keyNodeDebra.setActiveThreads(numProcs);
    skipDebra.setActiveThreads(numProcs);
    DynamicSet *set;
    
	if(strcmp(setType,"trie") == 0 || strcmp(setType, "trieSwCopy") == 0){
        set = &trieSetSwCopy;
    }
    else if(strcmp(setType, "skip") == 0){
        set = &skipList;
    }
    else if(strcmp(setType, "list") == 0){
        set = &listSet;
    }
    else{ // if(strcmp(setType, "trieSwCopy") == 0 ){
        set = &trieSetNotifDesc;
    }

    std::thread *th[MAX_THREADS];

    //Prefill the set to 50% full
    std::set<int64_t> valSet;
    while(valSet.size() < ceil(universeSize * prefill)){
        int64_t key = rng(universeSize);
        set->insert(key);
        valSet.insert(key);
    }
    
    ExperimentData data;
    //volatile int64_t opCount[MAX_THREADS];
    //vector<int64_t> dataPoints[MAX_THREADS];
    if(verbose){
        cout << numProcs << " threads performing random ops for " << std::setprecision(5) << time << " seconds on " << set->name() << "." << std::endl;
        cout << "Ratio of " << insertRate << " insert ops to " << removeRate << " remove ops to " << predRate << " predecessor ops." << std::endl;
        cout << "Keys drawn uniformly from universe of " << universeSize << " keys" << std::endl;
        cout << "Prior to start, the structure was filled with exactly " << valSet.size() << " keys." << std::endl;
        
        int hours, minutes, seconds;
        calcTime(millis(), hours, minutes, seconds);
        printf("Test starting at %02d:%02d:%02d\n",hours,minutes,seconds);
        calcTime(millis() + time * 1000, hours, minutes, seconds);
        printf("Test will finish at %02d:%02d:%02d\n",hours,minutes,seconds);
    }
    //cout << "Perf FDs: " << perfControlFDStr << "," << perfAckFDStr << std::endl;
    //Allocate NUMTHREADS threads
    for(int i = 0;i < numProcs;++i){
        th[i] = new std::thread(randomExperiment, set, numProcs, universeSize, time, i, &data, insertRate, removeRate, predRate);
    }

    int perfControlFD = -1;
    int perfAckFD = -1;
    if(getenv("PERF_CTL_FD") != NULL && verbose){
        cout << "Starting perf when experiment starts (which is now)." << std::endl;
        perfControlFD = atoi(getenv("PERF_CTL_FD"));
        perfAckFD = atoi(getenv("PERF_CTL_ACK_FD"));
        cout << "FDs: " << perfControlFD << "," << perfAckFD << std::endl; 

        //Send an enable message to perf.
        int numBytes = write(perfControlFD,"enable\n",8);
        assert(numBytes == 8);
        char ack[5] = {0,0,0,0,0};

        //Read the response with perf, and ensure it acknolwedged.
        numBytes = read(perfAckFD,ack,5);
        assert(numBytes == 5);
        assert(strcmp(ack,"ack\n") == 0);
    }
    
    
    
    if(perfControlFD != -1){
        //Put the thread asleep for time seconds to ensure it's not consuming cpu time during experiment.
        long sleep_duration = time * 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration));
        
        //Send disable message to perf, before joining processes to ensure statistics are not messed up. 
        int numBytes = write(perfControlFD, "disable\n", 9);
        assert(numBytes == 9);
        char ack[5] = {0,0,0,0,0};

        //Read the response with perf, and ensure it acknolwedged. 
        numBytes = read(perfAckFD,ack,5);
        assert(numBytes == 5);
        assert(strcmp(ack,"ack\n") == 0);
    }
    else{
        //Put the thread to sleep for time seconds plus 50 millis to ensure thread is not used in the place of others.
        long sleep_duration = time * 1000 + 50;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration));
    }

    long totalThroughput = 0;
    char timeStr[50];
    for(int i = 0;i < numProcs;++i){
        th[i]->join();
        ProcessData &pData = data.pData[i];
        totalThroughput += pData.opCount;
        if(verbose){
            cout << "Thread " << i << " performed " << pData.opCount << " ops, for an average of ";
            cout << std::setprecision(8) << ((double)pData.opCount / time) << " ops/sec." << std::endl;
            microsToStr(pData.startTime, &timeStr[0]);
            cout << "Started at " << timeStr << ", ";
            microsToStr(pData.endTime, &timeStr[0]);
            cout << "finished at " << timeStr << "." << std::endl;
        }
        //cout << data.startTime[i].value() << " " << data.endTime[i].value() << std::endl;
        delete th[i];
    }

    cout << "Total throughput (in ops): " << totalThroughput << std::endl;
}

void simpleTest(){
    threadID = 0;
    Trie<> trie(3);
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

int experimentProg(int argc, char **argv){
    double runtime = 5.0;
    int numProcs = 4;
    int keyRange = 20;
    bool verbose = false;
    char defaultType[5] = "trie";
    char *setType = NULL;
    double prefill;
    int insertRatio = 1, removeRatio = 1, predRatio = 2;

    if(argc == 2 && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))){
            cout << "Usage: ./dynamicSetTest [options]" << std::endl;
            cout << "Options:"<< std::endl;
            cout << "\t-h, --help\t\t\t\t\tDisplay information on running dynamic test."<< std::endl;
            cout << "\t-t, --time <T>\t\t\t\tRun the experiment for <T> seconds."<< std::endl;
            cout << "\t-n, --numProcs <N>\t\t\tRun the experiment with <N> processes (threads)."<< std::endl;
            cout << "\t-k, --keyRange <K>\t\t\tRun the experiment with a universe of 2^K keys." << std::endl;
            cout << "\t-O, --opDist <I> <R> <P>\tRun the experiment with ratio of <I> Insert, <R> Remove and <P> Predecessor ops." << std::endl;
            cout << "\t-v, --verbose\t\t\t\tPrint additional information about the test." << std::endl;
            cout << "\t--skip\t\t\t\t\t\tPerform the experiment on the Fomitchev-Ruppert skip list." << std::endl;
            cout << "\t--list\t\t\t\t\t\tPerform the experiment on the Fomitchev-Ruppert linked list." << std::endl;
            cout << "\t--trieNotifDesc\t\tPerform the experiment on Jeremy's Binary Trie with notifDesc implementation of RUALL." << std::endl;
            cout << "\t--trie --trieSwCopy\t\t\t\tPerform the experiment on Jeremy's Binary Trie with swCopy implementation of RUALL." << std::endl;
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
            else if(curArg + 3 < argc && (strcmp(currentParam, "-O") == 0 || strcmp(currentParam, "--opDist") == 0)){
                insertRatio = strtol(paramSetting, nullptr, 10);
                paramSetting = argv[curArg + 2];
                removeRatio = strtol(paramSetting, nullptr, 10);
                paramSetting = argv[curArg + 3];
                predRatio = strtol(paramSetting, nullptr, 10);
                if(insertRatio <= 0 || removeRatio <= 0 || predRatio <= 0){
                    cout << "Invalid proportions for insert/remove/predecessor operations." << std::endl;
                    exit(1);
                }
                curArg += 4;
            }
            else if(!verbose && (strcmp(currentParam, "-v") == 0 || strcmp(currentParam, "--verbose") == 0)){
                verbose = true;
                curArg += 1;
            }
            else if(!setType){
                if(strcmp(currentParam, "--list") == 0 || strcmp(currentParam, "--skip") == 0 || 
					strcmp(currentParam, "--trie") == 0 || strcmp(currentParam, "--trieSwCopy") == 0|| 
					strcmp(currentParam, "--trieNotifDesc") == 0){
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
    prefill = (double)insertRatio / (insertRatio + removeRatio);
    multithreadTest(setType, runtime, numProcs, keyRange, prefill, verbose, insertRatio, removeRatio, predRatio);
    return 0;
}

int main(int argc, char **argv){
    //skipTest();
    experimentProg(argc, argv);
    return 0;
}
