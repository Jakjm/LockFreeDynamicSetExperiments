#include "DynamicSets/DynamicSet.h"
#include "DynamicSets/Trie/trie.h"
#include "DynamicSets/FR_SkipList/FRSkipList.h"
#include "DynamicSets/FR_List/FRList.h"
#include "DynamicSets/SkipTrie/skiptrie.h"
#include "DynamicSets/FR_Augmented_Trie/augmentedTrie.h"
#include "common.h"
#include "DynamicSets/Trie/trieNodeTypes.h"
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
//#include <string>
#include <string.h>
#include <thread>
#include <unistd.h>


using std::cout;
#define NUM_OPS_BEFORE_TIME_CHECK 50 //The number of operations that should be performed in between each check if the experiment is over.
#define OUTPUT_CSV_NAME "resultData.csv"
//Data type used by a process to store information about its progress during the experiment.
struct ProcessData{
    volatile uint64_t insCount;
    volatile uint64_t succIns;
    volatile uint64_t remCount;
    volatile uint64_t succRem;
    volatile uint64_t predCount;
    volatile uint64_t opCount;
    volatile uint64_t startTime;
    volatile uint64_t endTime;
    volatile int64_t checkSum;
    volatile char padding[56];
    ProcessData(): insCount(0),remCount(0), predCount(0), opCount(0), startTime(0), endTime(0), checkSum(0){

    }
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

//Struct which contains variables which uniquely define a kind of experiment.
struct ExperimentType{
    double time; //Scheduled duration of experiment
    int numProcs; //Number of threads that participated
    int64_t universeSize; //Number of keys in the universe
    //Ratio of insert ops to remove ops to pred ops.
    int64_t insertRate; 
    int64_t removeRate;
    int64_t predRate;
    string dynamicSetType; //Name of dynamic set used during experiment.
    ExperimentType(double runtime, int threadCount, int64_t universe, int64_t inserts, int64_t removes, int64_t preds, string setType) : 
    time(runtime), numProcs(threadCount), universeSize(universe), insertRate(inserts), removeRate(removes), predRate(preds), dynamicSetType(setType){

    }

    

};


//Data type used to write information about an experiment and its results.
struct ResultData{
    ExperimentType type; //Type of experiment

    uint64_t minInserts, maxInserts, totalInserts;
    uint64_t minSuccIns, maxSuccIns, totalSuccIns;
    uint64_t minRemoves, maxRemoves, totalRemoves;
    uint64_t minSuccRem, maxSuccRem, totalSuccRem;
    uint64_t minPreds, maxPreds, totalPreds;
    double avgInserts, avgSuccIns, avgRemoves, avgSuccRem, avgPreds;

    uint64_t minStartTime, maxStartTime;
    uint64_t minEndTime, maxEndTime;
    double averageTime; //Actual duration of experiment on average
    uint64_t startTimeVariance; //Variance in start time in µs
    uint64_t endTimeVariance; //Variance in end time in µs
    uint64_t throughput; //Total ops performed by all processes
    double throughputPerSecond; //Throughput divided by actual time in seconds
    double averageThroughput; //Average ops performed by process
    double averageThroughputPerSecond; //Average thread throughput per second
    uint64_t maxOps; //Num ops performed by most productive thread
    uint64_t minOps; //Num ops performed by least productive thread
    double maxOpsPerSecond; //Ops/second of most productive thread
    double minOpsPerSecond; //Ops/second of least productive thread
    double maxPercentageDiff; //Percentage diff between maximum and average productivity
    double minPercentageDiff; //Percentage diff between average and min productivity


    void writeLabels(std::ofstream &stream){
        stream << "Expected Runtime(s),Num Processes,Universe Size,";
        stream << "Insert Rate,Remove Rate,Pred Rate,Dynamic Set Type,";
        stream << "Average Runtime(s),Start Time Variance(µs),End Time Variance(µs),";
        stream << "Total Throughput,Throughput Per Second,Average Throughput,Average Throughput Per Second,";
        stream << "Max Proc Throughput,Max Throughput Per Second,Max Exceeds Average by x%,";
        stream << "Min Proc Throughput,Min Throughput Per Second,Min Below Average by x%,";
        stream << "Total Preds,Average Preds,Min Preds,Max Preds,";
        stream << "Total Inserts,Total Successful Inserts,Average Inserts,Average Successful Inserts,Min Inserts,Max Inserts,Min Successful Inserts,Max Successful Inserts,";
        stream << "Total Removes,Total Successful Removes,Average Removes,Average Successful Removes,Min Removes,Max Removes,Min Successful Removes,Max Successful Removes";
        stream << std::endl;
    }
    void writeValues(std::ofstream &stream){
        stream << std::setprecision(8);
        stream <<  type.time << "," << type.numProcs << "," << type.universeSize << ",";
        stream << type.insertRate << "," << type.removeRate << "," << type.predRate << "," << type.dynamicSetType << ",";
        stream << averageTime << "," << startTimeVariance << "," << endTimeVariance << ",";
        stream << throughput << "," << throughputPerSecond << "," << averageThroughput << "," << averageThroughputPerSecond << ",";
        stream << maxOps << "," << maxOpsPerSecond << "," << maxPercentageDiff << ",";
        stream << minOps << "," << minOpsPerSecond << "," << minPercentageDiff << ",";
        stream << totalPreds << "," << avgPreds << "," << minPreds << "," << maxPreds << ",";
        stream << totalInserts << "," << totalSuccIns << "," << avgInserts << "," << avgSuccIns << "," << minInserts << "," << maxInserts << "," << minSuccIns << "," << maxSuccIns <<",";
        stream << totalRemoves << "," << totalSuccRem << "," << avgRemoves << "," << avgSuccRem << "," << minRemoves << "," << maxRemoves << "," << minSuccRem << "," << maxSuccRem;
        stream << std::endl;
    }


    ResultData(ExperimentType exp_type, ExperimentData &data) : type(exp_type){
        ProcessData &pData = data.pData[0];
        minStartTime = maxStartTime = pData.startTime;
        minEndTime = maxEndTime = pData.endTime;
        throughput = pData.opCount;
        minOps = pData.opCount;
        maxOps = pData.opCount;

        minInserts = maxInserts = totalInserts = pData.insCount;
        minSuccIns = maxSuccIns = totalSuccIns = pData.succIns;
        minRemoves = maxRemoves = totalRemoves = pData.remCount;
        minSuccRem = maxSuccRem = totalSuccRem = pData.succRem;
        minPreds = maxPreds = totalPreds = pData.predCount;

        for(int i = 1;i < type.numProcs;++i){
            pData = data.pData[i];
            if(pData.startTime < minStartTime)minStartTime = pData.startTime;
            else if(pData.startTime > maxStartTime)maxStartTime = pData.startTime;

            if(pData.endTime < minEndTime)minEndTime = pData.endTime;
            else if(pData.endTime > maxEndTime)maxEndTime = pData.endTime;

            throughput += pData.opCount;
            if(pData.opCount < minOps)minOps = pData.opCount;
            else if(pData.opCount > maxOps)maxOps = pData.opCount;

            totalInserts += pData.insCount;
            if(pData.insCount < minInserts)minInserts = pData.insCount;
            else if(pData.insCount > maxInserts)maxInserts = pData.insCount;

            totalRemoves += pData.remCount;
            if(pData.remCount < minRemoves)minRemoves = pData.remCount;
            else if(pData.remCount > maxRemoves)maxRemoves = pData.remCount;

            totalPreds += pData.predCount;
            if(pData.predCount < minPreds)minPreds = pData.predCount;
            else if(pData.predCount > maxPreds)maxPreds = pData.predCount;

            totalSuccIns += pData.succIns;
            if(pData.succIns < minSuccIns)minSuccIns = pData.succIns;
            else if(pData.succIns > maxSuccIns)maxSuccIns = pData.succIns;

            totalSuccRem += pData.succRem;
            if(pData.succRem < minSuccRem)minSuccRem = pData.succRem;
            else if(pData.succRem > maxSuccRem)maxSuccRem = pData.succRem;
        }

        startTimeVariance = maxStartTime - minStartTime;
        endTimeVariance = maxEndTime - minEndTime;
        uint64_t averageStart = (minStartTime + maxStartTime) / 2;
        uint64_t averageEnd = (minEndTime + maxEndTime) / 2;
        averageTime = (double)(averageEnd - averageStart) / 1000000;
        throughputPerSecond = (double)throughput / averageTime;
        averageThroughput = (double)throughput / type.numProcs;

        avgInserts = (double)totalInserts / type.numProcs;
        avgRemoves = (double)totalRemoves / type.numProcs;
        avgPreds = (double)totalPreds / type.numProcs;
        avgSuccIns = (double)totalSuccIns / type.numProcs;
        avgSuccRem = (double)totalSuccRem / type.numProcs;
        averageThroughputPerSecond = averageThroughput / averageTime;
        maxOpsPerSecond = maxOps / averageTime;
        minOpsPerSecond = minOps / averageTime;
        maxPercentageDiff = ((double)(maxOps / averageThroughput) - 1.0) * 100;
        minPercentageDiff = (1.0 - (double)(minOps / averageThroughput)) * 100;
    }

    void printResults(){
        cout << "Average actual time: " << std::setprecision(7) << averageTime << " s";
        cout << " Start time variance: " << startTimeVariance << 
        " µs End time variance: "  << endTimeVariance << " µs." << std::endl;
        cout << "Total thread throughput: " << throughput;
        cout << " ops ("  << throughputPerSecond << " ops/s)" << std::endl;
        cout << "Average thread throughput: " << averageThroughput;
        cout << " (" << averageThroughputPerSecond << " ops/s)." << std::endl;
        cout << "Maximum thread throughput: " << maxOps << " (" << maxOpsPerSecond << " ops/s), ";
        cout << std::setprecision(5) << maxPercentageDiff << "% above average" << std::endl;

        cout << std::setprecision(7);
        cout << "Minimum thread throughput: " << minOps << " (" << minOpsPerSecond << " ops/s), ";
        cout << std::setprecision(5) << minPercentageDiff << "% below average" << std::endl;
    }


    //Write the experiment result data to a CSV file.
    //TODO allow csv file to be specified or something?
    void writeToCSV(){
        string filename = OUTPUT_CSV_NAME;
        std::ifstream stream(filename.c_str());
        bool exists = stream.good();
        stream.close();

        std::ofstream os;
        //If file did not already exist, write labels to CSV.
        if(!exists){
            os.open(filename.c_str());
            writeLabels(os);
        }
        else{
            os.open(filename.c_str(), std::ofstream::app);
        }
        writeValues(os);
        os.close();
        //Else....
        cout << "Completed writing data to " << OUTPUT_CSV_NAME << "." << std::endl;
    }
};



/**
* This function has a process repeatedly perform update operations and predecessor operations on a dynamic set.
* For `time' seconds, a process will flip a coin to determine if it should perform an update operation or a predecessor operation.
* If performing an update operation, another coin is flipped to determine if the update operation will insert or remove a key from the set.
* Then the process will perform this update or predecessor operation with a randomly generated integer key, from 0 to range inclusive.
* id is the ID of the process performing the experiment.
*/
void randomExperiment(DynamicSet *set, int id, ExperimentData *data, ExperimentType type){
    threadID = id;
    uint64_t opCount = 0;
    
    ++data->numReady;
    while(data->numReady != type.numProcs){ //Continue iterating while not all threads are ready...
    }

    uint64_t startTime = micros();
    uint64_t currentTime = startTime;
    uint64_t endTime = startTime + (type.time * 1000000);
    uint64_t insCount = 0, remCount = 0, predCount = 0;
    uint64_t succIns = 0, succRem = 0;
    int ratioTotal = type.insertRate + type.removeRate + type.predRate;
    int64_t checkSum = 0;
    while(currentTime < endTime && !(data->done)){
        //Perform a series of operations....
        for(int i = 0;i < NUM_OPS_BEFORE_TIME_CHECK;++i){
            int64_t key = rng(type.universeSize); //Key of the operation that will be performed...
            int coinFlip = rng(ratioTotal); //Generate a number between 0 and ratioTotal - 1
            if(coinFlip < type.insertRate){ //Perform an insert operation at insertRate / ratioTotal percent chance.
                if(set->insert(key)){
                    ++succIns; 
                    checkSum += key;
                }
                ++insCount;
            }
            else if(coinFlip < (type.insertRate + type.removeRate)){ //Perform a remove operation at removeRate / ratioTotal percent chance.
                if(set->remove(key)){
                    ++succRem;
                    checkSum -= key;
                }
                ++remCount;
            }
            else{ //Perform a predecessor operation at predRate / ratioTotal percent chance.
                set->predecessor(key);
                ++predCount;
            }
        }
        opCount += NUM_OPS_BEFORE_TIME_CHECK;
        currentTime = micros();
    }
    data->done = true;
    uint64_t actualEndTime = micros();
    ProcessData &pData = data->pData[threadID];

    pData.opCount = opCount;
    pData.insCount = insCount;
    pData.succIns = succIns;
    pData.succRem = succRem;
    pData.remCount = remCount;
    pData.predCount = predCount;
    pData.startTime = startTime;
    pData.endTime = actualEndTime;
    pData.checkSum = checkSum;
}
//Converts current time in milliseconds millis into the current hour, minute and second in Eastern Time.
//millis is the current time in milliseconds.
void calcTime(int64_t millis, int &hours, int &minutes, int &seconds){
    millis -= (5 * 60 * 60 * 1000); //Subtract five hours for EST.
    int64_t numDays = millis / (60 * 60 * 24 * 1000);
    int64_t time = millis - numDays * (60 * 60 * 24 * 1000);
    hours = time / (60 * 60 * 1000);
    minutes = time % (60 * 60 * 1000) / 60000;
    seconds = time % (60000) / 1000; 

}


//time is the duration of the test in seconds.
//numProcs is the number of threads that will participate in the test.
//trieHeight is the base-2 logarithm of the universe size.
//prefill is the amount the data structure should be prefilled prior to testing.
void multithreadTest(DynamicSet *set, ExperimentType exp, bool verbose){
    threadID=0;
    std::thread *th[MAX_THREADS];
    //Prefill the set to a certain amount full

    double prefillRate = (double)exp.insertRate / (exp.insertRate + exp.removeRate);
    int64_t prefillAmount = ceil(exp.universeSize * prefillRate);
    set->prefill(exp.universeSize,prefillAmount);
    int64_t initialChecksum = set->getChecksum(exp.universeSize);
    
    ExperimentData data;
    cout << "Performing an experiment in which " << exp.numProcs << " threads will perform ops on "  << set->name() << " during " << exp.time << " seconds." << std::endl;
    cout << "Ratio of " << exp.insertRate << " insert ops to " << exp.removeRate << " remove ops to " << exp.predRate << " predecessor ops." << std::endl;
    cout << "Keys drawn uniformly from universe of " << exp.universeSize << " keys" << std::endl;
    cout << "Prior to start, the structure was filled with exactly " << prefillAmount << " keys." << std::endl;
    
    int hours, minutes, seconds;
    calcTime(millis(), hours, minutes, seconds);
    printf("Test starting at %02d:%02d:%02d\n",hours,minutes,seconds);
    calcTime(millis() + exp.time * 1000, hours, minutes, seconds);
    printf("Test will finish at %02d:%02d:%02d\n\n",hours,minutes,seconds);
    //cout << "Perf FDs: " << perfControlFDStr << "," << perfAckFDStr << std::endl;
    //Allocate NUMTHREADS threads
    for(int id = 0;id < exp.numProcs;++id){
        th[id] = new std::thread(randomExperiment, set, id, &data, exp);
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
        int64_t sleep_duration = exp.time * 1000;
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
        int64_t sleep_duration = exp.time * 1000 + 50;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration));
    }

   // int64_t totalThroughput = 0;
    //char timeStr[50];
    int64_t checkSumTotal = initialChecksum;
    for(int i = 0;i < exp.numProcs;++i){
        th[i]->join();
        delete th[i];
        checkSumTotal += data.pData[i].checkSum;
    }
    int64_t setChecksum = set->getChecksum(exp.universeSize);
    cout << "Key Checksum (initial + threads): " << checkSumTotal << " Actual set: " << setChecksum << std::endl; 
    assert(checkSumTotal == setChecksum);


    cout << "Experiment results:" << std::endl;
    ResultData results(exp, data);
    results.printResults();
    results.writeToCSV();

    // #ifdef COUNT_CONTENTION
    //     cout << std::endl;
    //     UALLContentionInfo uallInfo(uallCounter, exp.numProcs);
    //     uallInfo.printInfo();
    // #endif 
}

int experimentProg(int argc, char **argv){
    double runtime = 5.0;
    int numProcs = 4;
    int keyRange = 20;
    bool verbose = false;
    char *setType = NULL;
    int insertRatio = 1, removeRatio = 1, predRatio = 2;

    if(argc == 2 && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))){
        cout << "Usage: ./dynamicSetTest [options]" << std::endl;
        cout << "Options:"<< std::endl;
        cout << "\t-h, --help\t\t\t\tDisplay information on running dynamic test."<< std::endl;
        cout << "\t-t, --time <T>\t\t\t\tRun the experiment for <T> seconds."<< std::endl;
        cout << "\t-n, --numProcs <N>\t\t\tRun the experiment with <N> processes (threads)."<< std::endl;
        cout << "\t-k, --keyRange <K>\t\t\tRun the experiment with a universe of 2^K keys." << std::endl;
        cout << "\t-O, --opDist <I> <R> <P>\t\tRun the experiment with ratio of <I> Inserts to <R> Removes to <P> Predecessors." << std::endl;
        cout << "\t--skip\t\t\t\t\tPerform the experiment on the Fomitchev-Ruppert skip list." << std::endl;
        cout << "\t--list\t\t\t\t\tPerform the experiment on the Fomitchev-Ruppert linked list." << std::endl;
        cout << "\t--aug\t\t\t\t\tPerform the experiment on the Fatourou-Ruppert augmented static trie." << std::endl;
        cout << "\t--trie \t\t\t\t\tPerform the experiment on Jeremy's Binary Trie with swCopy implementation of RUALL." << std::endl;
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
            //If type of dynamic set has not been specified
            else if(!setType){
                //If this is a supported dynamic set...
                if(strcmp(currentParam, "--list") == 0 || strcmp(currentParam, "--skip") == 0 || 
					strcmp(currentParam, "--trie") == 0 || strcmp(currentParam, "--trieSwCopy") == 0|| 
					strcmp(currentParam, "--trieNotifDesc") == 0 || strcmp(currentParam, "--augTrie") == 0){
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

    DynamicSet *set = nullptr;;
    Trie trie(keyRange);
    LinkedListSet listSet;
    SkipListSet<22> skipList;
    AS_Trie augmentedTrie(keyRange);
    trieDebra.setActiveThreads(numProcs);
    keyNodeDebra.setActiveThreads(numProcs);
    skipDebra.setActiveThreads(numProcs);
    versionDebra.setActiveThreads(numProcs);

    if(!setType || strcmp(setType,"trie") == 0){
        set = &trie;
    }
    else if(strcmp(setType, "skip") == 0){
        set = &skipList;
    }
    else if(strcmp(setType, "list") == 0){
        set = &listSet;
    }
    else if(strcmp(setType, "augTrie") == 0){
        set = &augmentedTrie;
    }
    else{
        assert(false);
    }


    ExperimentType experimentType(runtime, numProcs, (1 << keyRange), insertRatio, removeRatio, predRatio, set->name());
    multithreadTest(set, experimentType, verbose);
    return 0;
}

int main(int argc, char **argv){
    //skipTest();
    experimentProg(argc, argv);
    return 0;
}
