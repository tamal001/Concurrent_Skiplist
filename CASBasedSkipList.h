#pragma once
#include <tuple>
#include <atomic>
#include <cassert>
#include <chrono>
#include <random>

#include "defines.h"

using namespace std;

class CASBasedSkipList {
private:
    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    const int minKey;
    const int maxKey;
    volatile char padding1[PADDING_BYTES];
    typedef struct Node{
        volatile int key;
        atomic<int> value;
        int level;
        atomic<Node *> next[NR_LEVELS];
        Node (int _key, int _value, int _level) : key(_key), value(_value), level(_level) {}
    } node;
    node *head, *tail;

    volatile char padding2[PADDING_BYTES];

public:
    CASBasedSkipList(const int _numThreads, const int _minKey, const int _maxKey);
    ~CASBasedSkipList();
    
    int contains(const int tid, const int & key);
    bool insertOrUpdate(const int tid, const int & key, const int & value); 
    bool erase(const int tid, const int & key); 
    
    bool is_marked(node *n);
    void mark_node_ptrs(node *n);
    node * unmark_all(node *n);
    node * unmark_single_level(node *n);
    int determineLevel(int key);
    tuple<CASBasedSkipList::node **, CASBasedSkipList::node **> list_lookup(int tid,int key);

    int valueTraversal();
    int listTraversal();
    long getSumOfKeys(); 
    void printDebuggingDetails();
};

CASBasedSkipList::CASBasedSkipList(const int _numThreads, const int _minKey, const int _maxKey)
        : numThreads(_numThreads), minKey(_minKey), maxKey(_maxKey) {
    head = new node(MINVAL, MINVAL, NR_LEVELS);
    tail = new node(MAXVAL, MAXVAL, NR_LEVELS);
    for(int i=0;i<NR_LEVELS;i++){
        head->next[i] = tail;
        tail->next[i] = NULL;
    }
}

CASBasedSkipList::~CASBasedSkipList() {
    node *n =head, *cur;
    while(n!=NULL){
        cur = n;
        n = n->next[0];
        delete n;
    }
}

tuple<CASBasedSkipList::node **, CASBasedSkipList::node **> CASBasedSkipList::list_lookup(int tid, int key){
    retry:
    node * left = head;
    node *left_list[NR_LEVELS], *right_list[NR_LEVELS];
    for(int i=NR_LEVELS-1;i>=0;i--){
        node * left_next = left->next[i];
        if(is_marked(left_next)) goto retry; 
        node *right;
        while(true){
            right = left_next;
            while(true){
                node *right_next = right->next[i];
                if(!is_marked(right_next)) break;
                //This actually means right node is marked
                right = unmark_single_level(right_next);
            }
            if(right->key >= key) break;
            //if()
            left = right;
            left_next = left->next[i];
        }
        if(left_next != right && !left->next[i].compare_exchange_strong(left_next, right)){
            goto retry;
        }
        left_list[i] = left; right_list[i] = right;
    }
    return make_tuple(left_list, right_list);
}

int CASBasedSkipList::contains(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    node **pred, **succ;
    tie(pred,succ) = list_lookup(tid, key);
    //Just use the variables to avoid being optimized out. INTENSE HEADACHE
    for(int i=NR_LEVELS-1;i>=0;i--){
        if(pred[i]==NULL) return contains(tid, key);
        if(succ[i]==NULL) return contains(tid, key);
    }
    return (succ[0]->key == key) ? (int) succ[0]->value : MINVAL;
}

bool CASBasedSkipList::insertOrUpdate(const int tid, const int & key, const int & value) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    node * new_node = new Node(key, value, determineLevel(key));
    retry:
    node **pred, **succ;
    tie(pred, succ) = list_lookup(tid, key);
    if(succ[0]->key == key){   //update the value of an existing key
        int old_v;
        do{
            old_v = succ[0]->value;
            if(old_v == MINVAL){
                mark_node_ptrs(succ[0]);
                goto retry;
            }
        }while(!succ[0]->value.compare_exchange_strong(old_v, value));
        delete new_node;
        return false;       //Updated but not inserted.
    }
    //Key not present, insert in the list.
    for(int i=0; i < new_node->level; i++){
        new_node->next[i] = succ[i];
    }
    if(!pred[0]->next[0].compare_exchange_strong(succ[0], new_node)) {
        goto retry;
    }
    for(int i = 1; i < new_node->level; i++){
        while(true){
            node * preds = pred[i];
            node * succs = succ[i];
            node * new_next = new_node->next[i];
            node * new_next_um = unmark_single_level(new_node->next[i]);
            if((new_next != succs) && !(new_node->next[i].compare_exchange_strong(new_next_um, succs))){
                break;
            }
            if(succs->key == key) {
                succs = unmark_single_level(succs->next[i]);
            }
            if(preds->next[i].compare_exchange_strong(succs, new_node)){
                break;
            }
            tie(pred, succ) = list_lookup(tid, key);
        }
    }
    return true;   //Not sure in the pseudocode as new node insert does not return any value
}

bool CASBasedSkipList::erase(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    node **pred, **succ;
    tie(pred,succ) = list_lookup(tid, key);
    //Just use the variables to avoid being optimized out. INTENSE HEADACHE
    for(int i=NR_LEVELS-1;i>=0;i--){
        if(pred[i]==NULL) return erase(tid, key);
        if(succ[i]==NULL) return erase(tid, key);
    }
    if(succ[0]->key != key) return false;
    int v;
    do{
        v = succ[0]->value;
        if(v == MINVAL) return false;
    }while(!succ[0]->value.compare_exchange_strong(v, MINVAL));
    mark_node_ptrs(succ[0]);
    tie(pred,succ) = list_lookup(tid, key);
    return true;
}

long CASBasedSkipList::getSumOfKeys() {
    long sum = 0;
    node *n = head->next[0];
    while(n!=NULL){
        if(n->key < MAXVAL && n->value > MINVAL) sum += n->key;
        n = unmark_single_level(n->next[0]);
    }
    return sum;
}

void CASBasedSkipList::printDebuggingDetails() {
    
}

int CASBasedSkipList::determineLevel(int key){
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

bool CASBasedSkipList::is_marked(node *n){
    if(n==NULL) return false;
    if(((uint64_t)n) & 1) return true;
    return false;
}

CASBasedSkipList::node * CASBasedSkipList::unmark_all(node *n){
    for(int i = n->level-1; i >= 0; i--){
        node *n_next;
        do{
            n_next = n->next[i];
            if(is_marked(n_next)) break;
        }while(!n->next[i].compare_exchange_strong(n_next, (node *)(((uint64_t)n_next) & (~1))));
    }
        //n->next [i] = n->next[i] & 1;
    return n;                     //need CAS here
}

CASBasedSkipList::node * CASBasedSkipList::unmark_single_level(node *n){
    if (n==NULL) return NULL;
    return (node *)(((uint64_t) n) & (~1));
}

void CASBasedSkipList::mark_node_ptrs(node *n){
    for (int i = n->level-1; i>=0; i--){
        node * n_next;
        do{
            n_next = n->next[i];
            if(is_marked(n_next)) break;
        }while(!n->next[i].compare_exchange_strong(n_next, (node *)((uint64_t)n_next | 1)));
    }
}

int CASBasedSkipList::listTraversal(){
    node *n = head;
    int count = 0;
    printf("Traversing list from head: ");
    while(n!=NULL){
        count++;
        printf("%d ",n->key);
        if(is_marked(n->next[0])){
            n = unmark_single_level(n->next[0]);
        }else n = n->next[0];
    }
    printf("\n");
    return count-2;
}

int CASBasedSkipList::valueTraversal(){
    node *n = head;
    int count = 0;
    //printf("Traversing list from head: ");
    while(n!=NULL){
        count++;
        //printf("%d ",n->value.load(MOR));
        if(is_marked(n->next[0])){
            n = unmark_single_level(n->next[0]);
        }else n = n->next[0];
    }
    printf("\n");
    return count-2;
}