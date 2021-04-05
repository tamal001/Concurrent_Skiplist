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
        int64 *a;
        int64 e, n;
        STATUS *cond;
        CCASDESCRIPTOR(int64 *_a, int64 _e, int64 _n, STATUS *_cond) 
                : a(_a), e(_e), n(_n), cond(_cond) {}
    }CCASDesc;

public:
    CCAS();
    ~CCAS();

    bool IsCCASDesc(int64 d);
    void doCCAS(int64 *a, int64 e, int64 n, STATUS *cond);
    int64 CCASRead(int64 *a);
    void CCASHelp(CCASDesc *d);
};

CCAS::CCAS(){
}

CCAS::~CCAS(){
}

void CCAS::doCCAS(int64 *a, int64 e, int64 n, STATUS *cond){
    CCASDesc *d = new CCASDesc(a,e,n,cond);
    bool v = __sync_bool_compare_and_swap(d->a,d->e,((int64)d)|2);
    while(!v){
        if(!IsCCASDesc(v)) return;
        CCASHelp((CCASDesc *)v);
        v = __sync_val_compare_and_swap(d->a,d->e,((int64)d)|2);
    }
    CCASHelp(d);
}

int64 CCAS::CCASRead (int64 *a){
    int64 v;
    for(v = *a; IsCCASDesc(v); v = *a)
        CCASHelp((CCASDesc *)v);
    return v;
}

void CCAS::CCASHelp(CCASDesc *d){
    CCASDesc *dd = (CCASDesc *)((int64)d & (~2));
    int64 dx = (int64)d|2;
    bool success = *(dd->cond) == UNDECIDED;
    bool v = __sync_bool_compare_and_swap(dd->a,dx,success?dd->n:dd->e);
}

bool CCAS::IsCCASDesc(int64 d){
    return (int64)d & 2;
}