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
    volatile char padding1[PADDING_BYTES];
    typedef struct Node{
        int64 key;
        int64 value;
        int64 level;
        Node * next[NR_LEVELS];    ///Change the next pointer according to the number of levels
    } node;
    node *head, *tail;
    struct search_pass{
        node *pred[NR_LEVELS],*succ[NR_LEVELS];
    };
    volatile char padding2[PADDING_BYTES];
    MCAS mcas;
    volatile char padding3[PADDING_BYTES];

public:
    MCASBasedSkipList(const int _numThreads);
    ~MCASBasedSkipList();
    
    //Dictionary operations
    int contains(const int & key);
    bool insertOrUpdate(const int & key, const int & value); 
    bool erase(const int & key); 
    
    //assisting methods
    void setNodeValues(node *n, int _key, int _value, int _level);
    int determineLevel();
    search_pass* list_lookup(int key);

    //Utility methods
    int valueTraversal();
    void listTraversal();
    long getSumOfKeys(); 
    void printDebuggingDetails();
};

MCASBasedSkipList::MCASBasedSkipList(const int _numThreads)
        : numThreads(_numThreads) {
    //mcas = new MCAS();
    head = new node();
    setNodeValues(head, MINVAL, MINVAL, NR_LEVELS);
    tail = new node();
    setNodeValues(tail, MAXVAL, MAXVAL, NR_LEVELS);
    for(int i=0;i<NR_LEVELS;i++){
        mcas.valueWrite((int64 *)&head->next[i],(int64)tail);
        mcas.valueWrite((int64 *)&tail->next[i],(int64)NULL);
    }
}

void MCASBasedSkipList::setNodeValues(Node *n, int _key, int _value, int _level){
    mcas.valueWrite(&n->key,_key);
    mcas.valueWrite(&n->value,_value);
    mcas.valueWrite(&n->level,_level);
}

MCASBasedSkipList::~MCASBasedSkipList() {
    node *n =head, *cur;
    while(n!=NULL){
        cur = n;
        n = (node *)mcas.valueRead((int64 *)&n->next[0]);
        delete n;
    }
}

MCASBasedSkipList::search_pass* MCASBasedSkipList::list_lookup(int key){
    node *x=head, *y;
    search_pass *pass = new search_pass();
    volatile int k = key;
    //node *left_list[NR_LEVELS], *right_list[NR_LEVELS];    //Smart pointer
    for(int i=NR_LEVELS-1; i>=0; i--){
        while(true){
            y=(node *)mcas.valueRead((int64 *)&x->next[i]);
            if(mcas.valueRead(&y->key)>=k) break;
            x=y;
        }
        pass->pred[i] = x; pass->succ[i]=y;
    }
    //return make_tuple(left_list, right_list);
    return pass;
}

int MCASBasedSkipList::contains(const int & key) {
    search_pass *pass = list_lookup(key);
    int val = (int) mcas.valueRead(&pass->succ[0]->key);
    delete pass;
    return (val == key) ? val : MINVAL;
}

bool MCASBasedSkipList::insertOrUpdate(const int & key, const int & value) {
    node *new_node = new node();
    int level = determineLevel();
    //int level = NR_LEVELS;
    setNodeValues(new_node,key,value,level);
    search_pass *pass;
    //printf("Inside insert with key %d\n",key);
    int64 *ptr[NR_LEVELS], old[NR_LEVELS], newv[NR_LEVELS];
    do{
        pass = list_lookup(key);
        int val;
        if(mcas.valueRead((int64 *)&pass->succ[0]->key) == key){
            do{
                val = pass->succ[0]->value;
                if(val == MINVAL) break;
                //printf("udpading value %d to %d for key %d\n",val>>2,value,key);
            }while(!__sync_bool_compare_and_swap(&pass->succ[0]->value, val, value<<2));
            return false;
        }
        for(int i=0;i<level;i++){
            //__sync_synchronize();
            mcas.valueWrite((int64 *)&new_node->next[i],(int64)pass->succ[i]);
            ptr[i] = (int64 *)&pass->pred[i]->next[i];
            old[i] = (int64) pass->succ[i];
            newv[i]= (int64) new_node;
        }
    }while(!mcas.doMCAS(ptr,old,newv,level));
    delete pass;
    return true;
}

bool MCASBasedSkipList::erase(const int & key){
    //printf("Inside delete with key %d\n",key);
    int count=0;
    int64 *ptr[NR_LEVELS*2+1], old[NR_LEVELS*2+1], newv[NR_LEVELS*2+1];
    int64 old_v;
    search_pass *pass;
    do{
        count = 0;
        pass = list_lookup(key);
        node *found = pass->succ[0];
        if(mcas.valueRead(&found->key) != key) return false;
        old_v = mcas.valueRead(&found->value);
        if(old_v == MINVAL) return false;
        int level = (int)mcas.valueRead(&found->level);
        for(int i = 0; i < level; i++){
            node * next = (node *)mcas.valueRead((int64 *)&found->next[i]);
            //__sync_synchronize();
            if(mcas.valueRead(&found->key) > mcas.valueRead(&next->key)) return false;
            ptr[2*i]   = (int64 *)&pass->pred[i]->next[i];
            old[2*i]   = (int64)found;
            newv[2*i]  = (int64)next;
            ptr[2*i+1] = (int64 *)&found->next[i];
            old[2*i+1] = (int64)next;
            newv[2*i+1]= (int64)pass->pred[i];
            count +=2;
        }
        ptr[count] = (int64 *)&found->value;
        old[count] = old_v;
        newv[count++]= MINVAL;
    }while(!mcas.doMCAS(ptr,old,newv,count));
    delete pass;
    return true;
}

long MCASBasedSkipList::getSumOfKeys() {
    long sum = 0;
    node *n = (node *)mcas.valueRead((int64*)&head->next[0]);
    while(n!=NULL){
        long key = (long)mcas.valueRead((int64*)&n->key);
        if(key < MAXVAL && key > MINVAL) sum += key;
        n = (node *)mcas.valueRead((int64*)&n->next[0]);
    }
    return sum;
}

void MCASBasedSkipList::printDebuggingDetails() {
    listTraversal();
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
        int key = (int)mcas.valueRead(&n->key);
        printf("%d ",key);
        n = (node *)mcas.valueRead((int64*)&n->next[0]);  
    }
    printf("\n");
}

int MCASBasedSkipList::valueTraversal(){
    node *n = head;
    int count = 0;
    //printf("Traversing list from head: ");
    while(n!=NULL){
        count++;
        int value = (int)mcas.valueRead((int64*)&n->value);
        //printf("%d ",value);
        n = (node *)mcas.valueRead((int64*)&n->next[0]); 
    }
    printf("\n");
    return count-2;
}
