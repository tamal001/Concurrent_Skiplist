#include <atomic>
#include "defines.h"

using namespace std;

class CCAS{
private:
    typedef struct CCASDESCRIPTOR{
        WORD *a, e, n;
        bool cond;
        CCASDESCRIPTOR(WORD *_a, WORD _e, WORD _n, bool _cond) 
                : a(a), e(_e), n(_n), cond(_cond) {}
    }CCASDesc;

public:
    CCAS();
    ~CCAS();

    bool IsCCASDesc(MCASDesc *d);
    bool doCCAS(WORD *a, WORD e, WORD n, bool cond);
    WORD MCASRead(WORD *a);
    bool MCASHelp(MCASDesc *d);
};

MCAS::MCAS(){
}

MCAS::~MCAS(){

}

bool MCAS::doMCAS(int N, WORD *a[], WORD e[], WORD n[]){
    MCASDesc *d = new MCASDesc(N, a, e, n);
    AddressSort(d);
    return MCASHelp(d);
}

WORD MCAS::MCASRead (WORD *a){
    WORD v;
    for(v = CCASRead(a); IsMCASDesc(v); v = CCASRead(a))
        MCASHelp((MCASDesc *)v);
    return v;
}

bool MCAS::MCASHelp(MCASDesc *d){
    WORD v;
    STATUS desired = FAILED;
    bool success;

    for(int i = 0; i < d->N; i++){
        while(true){
            CCAS(d->a[i], d->e[i], d, &d->status);
            if((v = *(d->a[i])) == d->e[i] && d->status == UNDECIDED ) 
                continue;
            if( v == d) break;
            if(!(IsMCASDesc(v))) goto decision_point;
            MCASHelp( (MCASDesc *) v);
        }
    }
    desired = SUCCEEDED;
decision_point:
    d->status.compare_exchange_strong(UNDECIDED, desired);
    success = (d->status==SUCCEEDED);
    for(int i = 0; i < d->N; i++){
        d->a[i]->compare_exchange_strong(d, success? d->n[i] : d->e[i]);
    }
    return success;
}

bool MCAS::IsMCASDesc(MCASDesc *d){
    return (uint64_t)d & 1;
}