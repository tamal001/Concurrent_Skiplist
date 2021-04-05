#include <atomic>
#include "defines.h"

using namespace std;

enum STATUS
{
    UNDECIDED=0,
    FAILED=1,
    SUCCEEDED=2
};

class CCAS{
private:
    typedef struct CCASDESCRIPTOR{
        WORD *a;
        WORD e, n;
        STATUS *cond;
        CCASDESCRIPTOR(WORD *_a, WORD _e, WORD _n, STATUS *_cond) 
                : a(_a), e(_e), n(_n), cond(_cond) {}
    }CCASDesc;

public:
    CCAS();
    ~CCAS();

    bool IsCCASDesc(WORD d);
    void doCCAS(WORD *a, WORD e, WORD n, STATUS *cond);
    WORD CCASRead(WORD *a);
    void CCASHelp(CCASDesc *d);
};

CCAS::CCAS(){
}

CCAS::~CCAS(){
}

void CCAS::doCCAS(WORD *a, WORD e, WORD n, STATUS *cond){
    CCASDesc *d = new CCASDesc(a,e,n,cond);
    bool v = __sync_bool_compare_and_swap(d->a,d->e,((WORD)d)|2);
    while(!v){
        if(!IsCCASDesc(v)) return;
        CCASHelp((CCASDesc *)v);
        v = __sync_val_compare_and_swap(d->a,d->e,((WORD)d)|2);
    }
    CCASHelp(d);
}

WORD CCAS::CCASRead (WORD *a){
    WORD v;
    for(v = *a; IsCCASDesc(v); v = *a)
        CCASHelp((CCASDesc *)v);
    return v;
}

void CCAS::CCASHelp(CCASDesc *d){
    CCASDesc *dd = (CCASDesc *)((WORD)d & (~2));
    WORD dx = (WORD)d|2;
    bool success = *(dd->cond) == UNDECIDED;
    bool v = __sync_bool_compare_and_swap(dd->a,dx,success?dd->n:dd->e);
}

bool CCAS::IsCCASDesc(WORD d){
    return (WORD)d & 2;
}