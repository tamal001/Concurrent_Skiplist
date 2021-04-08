#pragma once
#include <tuple>
#include <chrono>
#include <random>

#define IN 0
#define DELETED 1
#define DUPLICATE_KEY 2
#define NO_SUCH_NODE 3
#define maxLevel NR_LEVELS+1

#include "defines.h"

using namespace std;

class MikhailCASBased {
private:
    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    volatile char padding1[PADDING_BYTES];
    typedef struct Node{
        int key, value;
        Node *back_link, *succ;
        Node *down, *up;           //Incase search never returns root, delete up pointer and separate head;
        Node *tower_root; 
    } node;
    struct STATUS{
        int value;
    };
    node *head;
    volatile char padding2[PADDING_BYTES];

public:
    MikhailCASBased(const int _numThreads);
    ~MikhailCASBased();
    
    //Dictionary operations
    int contains(const int & key);
    bool insertOrUpdate(const int & key, const int & value); 
    bool erase(const int & key); 
    
    //Assisting methods
    void setNodeValues(node *, int, int, node *, node *);
    tuple<MikhailCASBased::node *, MikhailCASBased::node *> SearchToLevel_SL (int, int);
    tuple<MikhailCASBased::node *, int> FindStart_SL(int);
    tuple<MikhailCASBased::node *, MikhailCASBased::node *> SearchToLevel_SL(int, node*);
    tuple<MikhailCASBased::node *, MikhailCASBased::node *> SearchRight(int, node *);
    tuple<MikhailCASBased::node *, MikhailCASBased::node *> SearchRight2(int, node *);
    tuple<MikhailCASBased::node *, int, bool> TryFlagNode(node *, node *);
    tuple<MikhailCASBased::node *, MikhailCASBased::node *> InsertNode(node *, node *, node *);
    int determineLevel(int, double);
    MikhailCASBased::node * DeleteNode(node *, node *);
    void MikhailCASBased::HelpFlagged(node *, node *);
    void MikhailCASBased::TryMark(node *del_node);
    void MikhailCASBased::HelpMarked(node *prev_node, node *del_node);

    int valueTraversal();
    void listTraversal();
    long getSumOfKeys(); 
    void printDebuggingDetails();
};

MikhailCASBased::MikhailCASBased(const int _numThreads)
        : numThreads(_numThreads) {
    
}

MikhailCASBased::~MikhailCASBased() {
    /*
    node *n =head, *cur;
    while(n!=NULL){
        cur = n;
        n = n->next[0];
        delete cur;
    }
    */
}

void MikhailCASBased::setNodeValues(node *n, int _key, int _value, node *down, node *troot){
    n->key = _key;
    n->value = _value;
    n->back_link = NULL;
    n->succ = NULL;
    n->up = NULL;
    n->down = down;
    n->tower_root = troot;
}

tuple<MikhailCASBased::node *, MikhailCASBased::node *> MikhailCASBased::SearchToLevel_SL(int key, int level){
    node *curr_node, *next_node;
    int curr_v;
    tie(curr_node, curr_v) = FindStart_SL(level);
    while(curr_v>level){
        tie(curr_node, next_node) = SearchRight(key, curr_node);
        curr_node = curr_node->down;
        curr_v--;
    }
    tie(curr_node, next_node) = SearchRight(key, curr_node);
    return make_tuple(curr_node, next_node);
}

tuple<MikhailCASBased::node *, int> MikhailCASBased::FindStart_SL(int level){
    node *curr_node = head;
    int curr_v = 1;
    node *temp = (node *)((int)curr_node->up->succ & (~3));
    while((temp->key != MAXKEY) || (curr_v < level)){          //No need to unmark. Head tower never gets marked
        curr_node = curr_node->up;
        curr_v++;
        temp = (node *)((int)curr_node->up->succ & (~3));
    }
    return make_tuple(curr_node, curr_v);
}

tuple<MikhailCASBased::node *, MikhailCASBased::node *> MikhailCASBased::SearchRight(int key, node *curr_node){
    node *next_node = (node *)((int)curr_node->succ & (~3));
    while(next_node->key <= key){
        while((int)next_node->tower_root->succ & 1){
            int status;
            bool result;
            tie(curr_node, status, result) = TryFlagNode(curr_node, next_node);
            if(status == IN){
                HelpFlagged(curr_node, next_node);
            }
            next_node = (node *)((int)curr_node->succ & (~3));
        }
        if(next_node->key <= key){
            curr_node = next_node;
            next_node = (node *)((int)curr_node->succ & (~3));;
        }
    }
    return make_tuple(curr_node, next_node);
}

tuple<MikhailCASBased::node *, MikhailCASBased::node *> MikhailCASBased::SearchRight2(int key, node *curr_node){
    node *next_node = (node *)((int)curr_node->succ & (~3));;
    while(next_node->key < key){
        while((int)next_node->tower_root->succ & 1){
            STATUS *status;
            bool result;
            tie(curr_node, status, result) = TryFlagNode(curr_node, next_node);
            if(status->value == IN){
                HelpFlagged(curr_node, next_node);
            }
            next_node = (node *)((int)curr_node->succ & (~3));
        }
        if(next_node->key <= key){
            curr_node = next_node;
            next_node = (node *)((int)curr_node->succ & (~3));
        }
    }
    return make_tuple(curr_node, next_node);
}

int MikhailCASBased::contains(const int & key) {
    node *curr_node, next_node;
    tie(curr_node, next_node) = SearchToLevel_SL(key, 1);
    if(curr_node->key == key){
        return curr_node->tower_root->value;
    }
    return MINVAL;
}


bool MikhailCASBased::insertOrUpdate(const int & key, const int & value) {
    node *prev_node, *next_node, *result;
    tie(prev_node, next_node) = SearchToLevel_SL(key, 1);

    if(prev_node->key == key){ //duplicate key, update the value at the tower root
        int val;
        do{
            val = prev_node->tower_root->value;
        }while(!__sync_bool_compare_and_swap(&prev_node->tower_root->value, val, value));
    }
    
    node *rnode = new node();
    setNodeValues(rnode, key, value, NULL, rnode);
    node *new_node = rnode;
    int tH = determineLevel(key, 0.5);
    int curr_v = 1;
    while(true){
        tie(prev_node, result) = InsertNode(new_node, prev_node, next_node);
        if((int) result == DUPLICATE_KEY && curr_v == 1){
            delete new_node;
            return false;
        }
        if((int)rnode->succ & 2){
            if((int)result == (int) new_node && (int)new_node != (int)rnode){
                DeleteNode(prev_node, new_node);
            }
            return true;
        }
        curr_v++;
        if(curr_v == tH) return true;
        node *last_node = new_node;
        new_node = new node();
        setNodeValues(new_node, key, MINVAL, last_node, rnode);
        tie(prev_node, next_node) = SearchToLevel_SL(key, curr_v);
    }
    return true;
}

tuple<MikhailCASBased::node *, MikhailCASBased::node *> MikhailCASBased::InsertNode(node *newNode, node *prev_node, node *next_node){
    if(prev_node->key == newNode->key){
        return make_tuple(prev_node, (node *)DUPLICATE_KEY);
    }
    while(true){
        node * prev_succ = prev_node->succ;
        if((int)prev_succ & 1){
            HelpFlagged(prev_node, (node *)((int)prev_succ & (~1)));
        }
        else{
            newNode->succ = next_node;
            node * result = __sync_val_compare_and_swap(&prev_node->succ, next_node, newNode);
            if(result == (node *)((int)next_node & (~3))){
                return make_tuple(prev_node, newNode);
            }
            else{
                if((int)result & 1){
                    HelpFlagged(prev_node, (node *)((int)result->succ & (~1)));
                }
                while((int)prev_node->succ & 2){
                    prev_node = prev_node->back_link;
                }
            }
        }
        tie(prev_node, next_node) = SearchRight(newNode->key, prev_node);
        if(prev_node->key == newNode->key){
            return make_tuple(prev_node, (node *)DUPLICATE_KEY);
        }
    }
}

bool MikhailCASBased::erase(const int & key) {
    node *prev_node, *del_node;
    tie(prev_node, del_node) = SearchToLevel_SL(key-1, 1);
    if(del_node->key != key) return false;
    node * result = DeleteNode(prev_node, del_node);
    if((int)result == NO_SUCH_NODE) return false;
    SearchToLevel_SL(key, 2);
    return true;
}

MikhailCASBased::node * MikhailCASBased::DeleteNode(node *prev_node, node *del_node){
    int status;
    bool result;
    tie(prev_node, status, result) = TryFlagNode(prev_node, del_node);
    if(status == IN){
        HelpFlagged(prev_node, del_node);
    }
    if(result == false){
        return (node *)NO_SUCH_NODE;
    }
    return del_node;
}

tuple<MikhailCASBased::node *, int, bool> MikhailCASBased::TryFlagNode(node *prev_node, node *target_node){
    while(true){
        if((int)prev_node->succ == ((int)target_node | 1)){
            return make_tuple(prev_node, IN, false);
        }
        int targetnode = (int)target_node & (~3);
        int result = (int)__sync_val_compare_and_swap(&prev_node->succ, targetnode, targetnode | 1);
        if(result == targetnode){
            return make_tuple(prev_node, IN, true);
        }
        if(result == targetnode | 1){
            return make_tuple(prev_node, IN, false);
        }
        while((int)prev_node & 2){
            prev_node = prev_node->back_link;
        }
        node *del_node;
        tie(prev_node, del_node) = SearchRight2(target_node->key, prev_node);
        if((int)del_node != (int)target_node){
            return make_tuple(prev_node, DELETED, false);
        }
    }
}

void MikhailCASBased::HelpFlagged(node *prev_node, node *del_node){
    del_node->back_link = prev_node;
    if((int)del_node->succ & 2 == 0){
        TryMark(del_node);
    }
    HelpMarked(prev_node, del_node);
}

void MikhailCASBased::TryMark(node *del_node){
    do{
        int next_node = (int)del_node->succ & (~3);
        node * result = __sync_val_compare_and_swap(&del_node->succ, (node *)next_node, (node *)(next_node & 2));
        if((int)result & 1){
            HelpFlagged(del_node, (node *)((int)result & (~1)));
        }
    }while((int)del_node->succ & 2 == 0);
}

void MikhailCASBased::HelpMarked(node *prev_node, node *del_node){
    node *next_node = (node *)((int)del_node->succ &(~3));
    bool result = __sync_bool_compare_and_swap(&prev_node->succ, (node *)((int)del_node & 2), next_node);
}

long MikhailCASBased::getSumOfKeys() {
    long sum = 0;
    node *n = head;
    while(n!=NULL){
        if(n->key>MINKEY && n->key < MAXKEY){
            sum += n->key;
        }
        n = n->succ;
    }
    return sum;
}

void MikhailCASBased::printDebuggingDetails() {
    //listTraversal();
}

int MikhailCASBased::determineLevel(int key, double prob){
    mt19937_64 rng;
    uint64_t timeSeed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    seed_seq ss{uint32_t(timeSeed & 0xffffffff), uint32_t(timeSeed>>32)};
    rng.seed(ss);
    uniform_real_distribution<double> distribution(0.0,1.0);
    int tH = 1;
    double number = distribution(rng);
    while(tH<maxLevel-1){
        if(number<(1-prob)) break;
        number = distribution(rng);
        tH++;
    }
    return tH;
}

void MikhailCASBased::listTraversal(){
    node *n = head;
    printf("Traversing list from head: ");
    while(n!=NULL){
        printf("%d ",n->key);
        n = (node *)((int)n->succ & (~3));
    }
    printf("\n");
}

int MikhailCASBased::valueTraversal(){
    node *n = head;
    int count = 0;
    //printf("Traversing list from head: ");
    while(n!=NULL){
        count++;
        //printf("%d ",n->value);
        n = (node *)((int)n->succ & (~3));
    }
    printf("\n");
    return count-2;
}