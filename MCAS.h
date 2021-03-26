#include <atomic>
#include "defines.h"

using namespace std;

enum STATUS
{
    UNDECIDED=0,
    FAILED=1,
    SUCCEEDED=2
};


class MCAS{
private:
    typedef struct MCASDESCRIPTOR{
        int N;
        atomic<WORD> *a[];
        WORD e[], n[];
        atomic<STATUS> status;
        MCASDESCRIPTOR(int _N, WORD *_a[], WORD _e[], WORD _n[]) : N(_N)
        {
            for(int i=0; i<N; i++){
                a[i]->store((uint64_t)_a[i], MOR);
                e[i] = _e[i];
                n[i] = _n[i];
            }
            status = UNDECIDED;
        }
    }MCASDesc;

public:
    MCAS();
    ~MCAS();

    bool IsMCASDesc(MCASDesc *d);
    void AddressSort(MCASDesc *d);
    bool doMCAS(int N, WORD *a[], WORD e[], WORD n[]);
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

void MCAS::AddressSort(MCASDesc *d){
    for(int i = 0; i < d->N; i++){
        for (int j = i+1; j < d->N; j++){
            if(d->a[i]>d->a[j]){
                WORD * temp = d->a[i];
                WORD Exp_temp = d->e[i];
                WORD New_temp = d->n[i];

                d->a[i] = d->a[j];
                d->e[i] = d->e[j];
                d->n[i] = d->n[j];

                d->a[j] = temp;
                d->e[j] = Exp_temp;
                d->n[j] = New_temp;
            }
        }
    }
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