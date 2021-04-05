#pragma once
#include <tuple>
#include <atomic>
#include <cassert>
#include <chrono>
#include <random>
#define MAX_CAS 7

#include "defines.h"
#include "MCAS.h"

using namespace std;

class MCASBasedSkipList {
private:
    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    const int minKey;
    const int maxKey;
    volatile char padding1[PADDING_BYTES];
    typedef struct Node{
        WORD key;
        WORD value;
        WORD level;
        Node * next[NR_LEVELS];
    } node;
    node *head, *tail;
    struct search_pass{
        node *pred[NR_LEVELS],*succ[NR_LEVELS];
    };
    volatile char padding2[PADDING_BYTES];
    MCAS *mcas;
    volatile char padding3[PADDING_BYTES];

public:
    MCASBasedSkipList(const int _numThreads, const int _minKey, const int _maxKey);
    ~MCASBasedSkipList();
    
    //Dictionary operations
    int contains(const int tid, const int & key);
    bool insertOrUpdate(const int tid, const int & key, const int & value); 
    bool erase(const int tid, const int & key); 
    
    //assisting methods
    void setNodeValues(node *n, int _key, int _value, int _level);
    int determineLevel();
    search_pass* list_lookup(int tid,int key);

    //Utility methods
    int valueTraversal();
    void listTraversal();
    long getSumOfKeys(); 
    void printDebuggingDetails();
};

MCASBasedSkipList::MCASBasedSkipList(const int _numThreads, const int _minKey, const int _maxKey)
        : numThreads(_numThreads), minKey(_minKey), maxKey(_maxKey) {
    mcas = new MCAS();
    head = new node();
    setNodeValues(head, MINVAL, MINVAL, NR_LEVELS);
    tail = new node();
    setNodeValues(tail, MAXVAL, MAXVAL, NR_LEVELS);
    for(int i=0;i<NR_LEVELS;i++){
        mcas->valueWrite((WORD *)&head->next[i],(WORD)tail);
        mcas->valueWrite((WORD *)&tail->next[i],(WORD)NULL);
    }
}

void MCASBasedSkipList::setNodeValues(Node *n, int _key, int _value, int _level){
    mcas->valueWrite(&n->key,_key);
    mcas->valueWrite(&n->value,_value);
    mcas->valueWrite(&n->level,_level);
}

MCASBasedSkipList::~MCASBasedSkipList() {
    node *n =head, *cur;
    while(n!=NULL){
        cur = n;
        n = (node *)mcas->valueRead((WORD *)&n->next[0]);
        delete n;
    }
}

MCASBasedSkipList::search_pass* MCASBasedSkipList::list_lookup(int tid, int key){
    node *x=head, *y;
    search_pass *pass = new search_pass();
    volatile int k = key;
    //node *left_list[NR_LEVELS], *right_list[NR_LEVELS];
    for(int i=NR_LEVELS-1; i>=0; i--){
        while(true){
            y=(node *)mcas->valueRead((WORD *)&x->next[i]);
            if(mcas->valueRead(&y->key)>=k) break;
            x=y;
        }
        pass->pred[i] = x; pass->succ[i]=y;
    }
    //return make_tuple(left_list, right_list);
    return pass;
}

int MCASBasedSkipList::contains(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    search_pass *pass = list_lookup(tid, key);
    int val = (int) mcas->valueRead(&pass->succ[0]->key);
    delete pass;
    return (val == key) ? val : MINVAL;
}

bool MCASBasedSkipList::insertOrUpdate(const int tid, const int & key, const int & value) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    node *new_node = new node();
    int level = determineLevel();
    setNodeValues(new_node,key,value,level);
    search_pass *pass;
    //printf("Inside insert with key %d\n",key);
    WORD *ptr[NR_LEVELS], old[NR_LEVELS], newv[NR_LEVELS];
    do{
        pass = list_lookup(tid,key);
        int val;
        if(mcas->valueRead((WORD *)&pass->succ[0]->key) == key){
            do{
                val = pass->succ[0]->value;
                if(val == MINVAL) break;
                //printf("udpading value %d to %d for key %d\n",val>>2,value,key);
            }while(!__sync_bool_compare_and_swap(&pass->succ[0]->value, val, value<<2));
            return false;
        }
        for(int i=0;i<level;i++){
            //__sync_synchronize();
            mcas->valueWrite((WORD *)&new_node->next[i],(WORD)pass->succ[i]);
            ptr[i] = (WORD *)&pass->pred[i]->next[i];
            old[i] = (WORD) pass->succ[i];
            newv[i]= (WORD) new_node;
        }
    }while(!mcas->doMCAS(ptr,old,newv,level));
    delete pass;
    return true;
}

bool MCASBasedSkipList::erase(const int tid, const int & key){
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    //printf("Inside delete with key %d\n",key);
    int count=0;
    WORD *ptr[NR_LEVELS*2+1], old[NR_LEVELS*2+1], newv[NR_LEVELS*2+1];
    WORD old_v;
    search_pass *pass;
    do{
        count = 0;
        pass = list_lookup(tid, key);
        node *found = pass->succ[0];
        if(mcas->valueRead(&found->key) != key) return false;
        old_v = mcas->valueRead(&found->value);
        if(old_v == MINVAL) return false;
        int level = (int)mcas->valueRead(&found->level);
        for(int i = 0; i < level; i++){
            node * next = (node *)mcas->valueRead((WORD *)&found->next[i]);
            //__sync_synchronize();
            if(mcas->valueRead(&found->key) > mcas->valueRead(&next->key)) return false;
            ptr[2*i]   = (WORD *)&pass->pred[i]->next[i];
            old[2*i]   = (WORD)found;
            newv[2*i]  = (WORD)next;
            ptr[2*i+1] = (WORD *)&found->next[i];
            old[2*i+1] = (WORD)next;
            newv[2*i+1]= (WORD)pass->pred[i];
            count +=2;
        }
        ptr[count] = (WORD *)&found->value;
        old[count] = old_v;
        newv[count++]= MINVAL;
    }while(!mcas->doMCAS(ptr,old,newv,count));
    delete pass;
    return true;
}

long MCASBasedSkipList::getSumOfKeys() {
    long sum = 0;
    node *n = (node *)mcas->valueRead((WORD*)&head->next[0]);
    while(n!=NULL){
        long key = (long)mcas->valueRead((WORD*)&n->key);
        if(key < MAXVAL && key > MINVAL) sum += key;
        n = (node *)mcas->valueRead((WORD*)&n->next[0]);
    }
    return sum;
}

void MCASBasedSkipList::printDebuggingDetails() {
    //listTraversal();
}

int MCASBasedSkipList::determineLevel(){
    mt19937_64 rng;
    uint64_t timeSeed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    seed_seq ss{uint32_t(timeSeed & 0xffffffff), uint32_t(timeSeed>>32)};
    rng.seed(ss);
    uniform_real_distribution<double> distribution(0.0,1.0);

    double number = distribution(rng);
    int level = 0;
    while(level<NR_LEVELS){
        if(number<(1-pow(2,(-level)))) break;
        level++;
    }
    return level;
}

void MCASBasedSkipList::listTraversal(){
    node *n = head;
    printf("Traversing list key from head: ");
    while(n!=NULL){
        int key = (int)mcas->valueRead(&n->key);
        printf("%d ",key);
        n = (node *)mcas->valueRead((WORD*)&n->next[0]);  
    }
    printf("\n");
}

int MCASBasedSkipList::valueTraversal(){
    node *n = head;
    int count = 0;
    printf("Traversing list from head: ");
    while(n!=NULL){
        count++;
        int value = (int)mcas->valueRead((WORD*)&n->value);
        printf("%d ",value);
        n = (node *)mcas->valueRead((WORD*)&n->next[0]); 
    }
    printf("\n");
    return count-2;
}
