#include <chrono>
#include <iostream>
#include <random>

#include "defines.h"

using namespace std;

class ElapsedTimer {
private:
    char padding0[PADDING_BYTES];
    bool calledStart = false;
    char padding1[PADDING_BYTES];
    chrono::time_point<std::chrono::high_resolution_clock> start;
    char padding2[PADDING_BYTES];
public:
    void startTimer() {
        calledStart = true;
        start = chrono::high_resolution_clock::now();
    }
    int64_t getElapsedMillis() {
        if (!calledStart) {
            printf("ERROR: called getElapsedMillis without calling startTimer\n");
            exit(1);
        }
        auto now = chrono::high_resolution_clock::now();
        return chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    }
};

class counter {
private:
    struct PaddedVLL {
        volatile char padding[PADDING_BYTES-sizeof(long long)];
        volatile long long v;
    };
    PaddedVLL data[MAX_THREADS+1];
public:
    void add(const int tid, const long long val) {
        data[tid].v += val;
    }
    void inc(const int tid) {
        add(tid, 1);
    }
    long long get(const int tid) {
        return data[tid].v;
    }
    long long getTotal() {
        long long result = 0;
        for (int tid=0;tid<MAX_THREADS;++tid) {
            result += get(tid);
        }
        return result;
    }
    void clear() {
        for (int tid=0;tid<MAX_THREADS;++tid) {
            data[tid].v = 0;
        }
    }
    counter() {
        clear();
    }
} __attribute__((aligned(PADDING_BYTES)));

class RandomNatural {
private:
    volatile char padding[PADDING_BYTES-sizeof(unsigned int)];
    unsigned int seed;
public:
    RandomNatural(void) {
        this->seed = 0;
    }
    RandomNatural(int seed) {
        this->seed = seed;
    }
    
    void setSeed(int seed) {
        this->seed = seed;
    }
    
    /** returns pseudorandom x satisfying 0 <= x < n. **/
    unsigned int nextNatural() {
        seed ^= seed << 6;
        seed ^= seed >> 21;
        seed ^= seed << 7;
        return seed;
    }
};