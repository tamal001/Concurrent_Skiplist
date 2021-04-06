
#include <thread>
#include <cstdlib>
#include <string>
#include <cstring>
#include <iostream>
#include <atomic>
#include <chrono>
#include<math.h>

#include "defines.h"
#include "util.h"

#include "MCASBasedSkipList.h"
#include "CASBasedSkipList.h"

using namespace std;

template <class DataStructureType>
struct globals_t {
    RandomNatural rngs[MAX_THREADS];
    volatile char padding0[PADDING_BYTES];
    ElapsedTimer timer;
    volatile char padding1[PADDING_BYTES];
    ElapsedTimer timerFromStart;
    volatile char padding3[PADDING_BYTES];
    volatile bool done;
    volatile char padding4[PADDING_BYTES];
    volatile bool start;        
    volatile char padding5[PADDING_BYTES];
    atomic_int running;         
    volatile char padding6[PADDING_BYTES];
    DataStructureType * ds;
    counter numTotalOps;
    counter keyChecksum;
    counter sizeChecksum;
    int millisToRun;
    int totalThreads;
    int keyRangeSize;
    volatile char padding7[PADDING_BYTES];
    size_t garbage; 
    volatile char padding8[PADDING_BYTES];
    
    globals_t(int _millisToRun, int _totalThreads, int _keyRangeSize, DataStructureType * _ds) {
        for (int i=0;i<MAX_THREADS;++i) {
            rngs[i].setSeed(i+1);
        }
        done = false;
        start = false;
        running = 0;
        ds = _ds;
        millisToRun = _millisToRun;
        totalThreads = _totalThreads;
        keyRangeSize = _keyRangeSize;
        garbage = -1;
    }
    ~globals_t() {
        delete ds;
    }
} __attribute__((aligned(PADDING_BYTES)));

void runTrial(auto g, const long millisToRun, double insertPercent, double deletePercent) {
    g->done = false;
    g->start = false;
    
    // create and start threads
    thread * threads[MAX_THREADS]; 
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid] = new thread([&, tid]() {
            const int TIME_CHECKS = 500;
            size_t garbage = 0;
            
            // BARRIER WAIT
            g->running.fetch_add(1);
            while (!g->start) { TRACE TPRINT("waiting to start"<<endl); }               
            
            int key = 0;                
            int value = 0;
            for (int cnt=0; !g->done; ++cnt) {
                if ((cnt % TIME_CHECKS) == 0 && g->timer.getElapsedMillis() >= millisToRun)                  
                    g->done = true;
                
                double operationType = g->rngs[tid].nextNatural() / (double) numeric_limits<unsigned int>::max() * 100;
                
                key = (int) (1 + (g->rngs[tid].nextNatural() % g->keyRangeSize));
                value = (int) g->rngs[tid].nextNatural()% 10000000;

                // insert or delete this key (50% probability of each)
                if (operationType < insertPercent) {
                    value = value < 0? -value:value;
                    auto result = g->ds->insertOrUpdate(key, value);
                    //Checksum only updated the first time the key is inserted. Not added for update operation.
                    if (result) {
                        g->keyChecksum.add(tid, key);
                        g->sizeChecksum.add(tid, 1);
                        
                    }
                } else if (operationType < insertPercent + deletePercent) {
                    auto result = g->ds->erase(key);
                    if (result) {
                        g->keyChecksum.add(tid, -key);
                        g->sizeChecksum.add(tid, -1);
                    }
                } else {
                    auto result = g->ds->contains(key);
                    garbage += result;
                }
                
                g->numTotalOps.inc(tid);
            }
            
            g->running.fetch_add(-1);
            __sync_fetch_and_add(&g->garbage, garbage);
        });
    }
    
    while (g->running < g->totalThreads) {
        TRACE cout<<"main thread: waiting for threads to START running="<<g->running<<endl;
    }
    g->timer.startTimer();    
    __sync_synchronize(); 
    g->start = true; 
    while (g->running > 0) { }
    
    
    // join all threads
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid]->join();
        delete threads[tid];
    }
}

template <class DataStructureType>
void runExperiment(int keyRangeSize, int millisToRun, int totalThreads, double insertPercent, double deletePercent) {
    // create globals struct that all threads will access (with padding to prevent false sharing on control logic meta data)
    int minKey = 0;
    int maxKey = keyRangeSize;
    auto dataStructure = new DataStructureType(totalThreads);
    auto g = new globals_t<DataStructureType>(millisToRun, totalThreads, keyRangeSize, dataStructure);
    
    // Prefill the data structure

    g->timerFromStart.startTimer();
    if(keyRangeSize > 2){
        for (int attempts=0;;++attempts) {
            double totalUpdatePercent = insertPercent + deletePercent;
            double prefillingInsertPercent = (totalUpdatePercent < 1) ? 50 : (insertPercent / totalUpdatePercent) * 100;
            double prefillingDeletePercent = (totalUpdatePercent < 1) ? 50 : (deletePercent / totalUpdatePercent) * 100;
            auto expectedSize = keyRangeSize * prefillingInsertPercent / 100;

            runTrial(g, 200, prefillingInsertPercent, prefillingDeletePercent);

            // measure and print elapsed time
            cout<<"prefilling round "<<attempts<<" ending size "<<g->sizeChecksum.getTotal()<<" total elapsed time="<<(g->timerFromStart.getElapsedMillis()/1000.)<<"s"<<endl;

            // check if prefilling is done
            if (g->sizeChecksum.getTotal() > 0.95 * expectedSize) {
                cout<<"prefilling completed to size "<<g->sizeChecksum.getTotal()<<" (within 5% of expected size "<<expectedSize<<" with key checksum "<<g->keyChecksum.getTotal()<<")"<<endl;
                break;
            } else if (attempts > 100) {
                cout<<"failed to prefill in a reasonable time to within an error of 5% of the expected size; final size "<<g->sizeChecksum.getTotal()<<" expected "<<expectedSize<<endl;
                exit(0);
            }
        }
        cout<<endl;
    }
    else {
        cout<<"Prefilling skipped for small key range..."<<endl;
    }
    printf("prefill done\n");
    //Run Experiment
    
    cout<<"main thread: experiment starting..."<<endl;
    runTrial(g, g->millisToRun, insertPercent, deletePercent);
    cout<<"main thread: experiment finished..."<<endl;
    cout<<endl;
    
    //Check output
    
    g->ds->printDebuggingDetails();
    
    auto numTotalOps = g->numTotalOps.getTotal();
    auto dsSumOfKeys = g->ds->getSumOfKeys();
    auto dsSizeChecksum = g->ds->valueTraversal();
    auto threadsSumOfKeys = g->keyChecksum.getTotal();
    cout<<"Validation: sum of keys according to the data structure = "<<dsSumOfKeys<<" and sum of keys according to the threads = "<<threadsSumOfKeys<<".";
    cout<<((threadsSumOfKeys == dsSumOfKeys) ? " OK." : " FAILED.")<<endl;
    cout<<"sizeChecksum according to the counter ="<<g->sizeChecksum.getTotal()<<" size checksum according to data structure ="<<dsSizeChecksum<<endl;
    cout<<endl;

    cout<<"completedOperations="<<numTotalOps<<endl;
    cout<<"throughput="<<(long long) (numTotalOps * 1000. / g->millisToRun)<<endl;
    cout<<endl;
    
    if (threadsSumOfKeys != dsSumOfKeys) {
        cout<<"ERROR: validation failed!"<<endl;
        exit(0);
    }
    
    if (g->garbage == 0) cout<<endl; // "use" the g->garbage variable, so effectively the return values of all contains() are "used," so they can't be optimized out
    cout<<"total elapsed time="<<(g->timerFromStart.getElapsedMillis()/1000.)<<"s"<<endl;
    delete g;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        cout<<"USAGE: "<<argv[0]<<" [options]"<<endl;
        cout<<"Options:"<<endl;
        cout<<"    -t [int]     milliseconds to run"<<endl;
        cout<<"    -c [int]     CAS to be used for datastructure, 0 for CAS, 1 for MCAS"<<endl;
        cout<<"    -s [int]     size of the key range that random keys will be drawn from (i.e., range [1, s])"<<endl;
        cout<<"    -n [int]     number of threads that will perform inserts and deletes"<<endl;
        cout<<"    -i [double]  percent of operations that will be insert (example: 20)"<<endl;
        cout<<"    -d [double]  percent of operations that will be delete (example: 20)"<<endl;
        cout<<"                 (100 - i - d)% of operations will be contains"<<endl;
        cout<<endl;
        return 1;
    }
    
    int millisToRun = -1;
    int keyRangeSize = 0;
    int totalThreads = 0;
    int casType = 0;
    double insertPercent = 0;
    double deletePercent = 0;
    
    // read command line args
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-s") == 0) {
            keyRangeSize = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            totalThreads = atoi(argv[++i]);
        }else if (strcmp(argv[i], "-c") == 0) {
            casType = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0) {
            millisToRun = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0) {
            insertPercent = atof(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            deletePercent = atof(argv[++i]);
        } else {
            cout<<"bad arguments"<<endl;
            exit(1);
        }
    }
    
    // print command and args for debugging
    std::cout<<"Cmd:";
    for (int i=0;i<argc;++i) {
        std::cout<<" "<<argv[i];
    }
    std::cout<<std::endl;
    
    // print configuration for debugging
    PRINT(MAX_THREADS);
    PRINT(totalThreads);
    PRINT(keyRangeSize);
    PRINT(insertPercent);
    PRINT(deletePercent);
    PRINT(millisToRun);
    cout<<endl;
    // check for too large thread count
    if (totalThreads >= MAX_THREADS) {
        std::cout<<"ERROR: totalThreads="<<totalThreads<<" >= MAX_THREADS="<<MAX_THREADS<<std::endl;
        return 1;
    }
    if(casType == 0){
        runExperiment<CASBasedSkipList>(keyRangeSize, millisToRun, totalThreads, insertPercent, deletePercent);
    }else if(casType == 1){
        runExperiment<MCASBasedSkipList>(keyRangeSize, millisToRun, totalThreads, insertPercent, deletePercent);
    }else{
        std::cout <<"Wrong cas type"<<endl;
        exit(0);
    }
    
    return 0;
}
