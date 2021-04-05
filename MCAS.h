#include <atomic>
#include "defines.h"
#include "CCAS.h"

using namespace std;

class MCAS{
private:
    struct MCASDesc{
        int N;
        WORD *a[2*NR_LEVELS+1];   //Arrary of addresses to perform CAS on
        WORD e[2*NR_LEVELS+1], n[2*NR_LEVELS+1];  //Expected values and new values at those addresses
        STATUS status;  // Status of CAS
        MCASDesc() : status(UNDECIDED){}
    };//__attribute__((aligned(PADDING_BYTES)));
    volatile char padding0[PADDING_BYTES];
    CCAS *Ccas;
    volatile char padding1[PADDING_BYTES];
public:
    MCAS();
    ~MCAS();

    bool IsMCASDesc(WORD d);
    void AddressSort(MCASDesc *d);
    bool doMCAS(WORD *a[], WORD e[], WORD n[], int N);
    WORD MCASRead(WORD *a);
    bool MCASHelp(MCASDesc *d);
    //Read and write should be performed through MCAS. Because regular value is right shifted two paces. Descriptor can also be helped.
    void valueWrite(WORD *a, WORD b);
    void valueWriteInt(WORD *a, WORD b);
    WORD valueRead(WORD *a);
};

MCAS::MCAS(){
    Ccas= new CCAS();
}

MCAS::~MCAS(){
    delete Ccas;
}

bool MCAS::doMCAS(WORD *a[], WORD e[], WORD n[], int N){
    MCASDesc *d = new MCASDesc();
    for(int i = 0; i<N; i++){
        d->a[i]=a[i];
        d->e[i]=e[i]<<2;
        d->n[i]=n[i]<<2;
    }
    d->N = N;    
    AddressSort(d);
    return MCASHelp(d);
}

//Sort all the addresses in the MCAS using bubble sort
void MCAS::AddressSort(MCASDesc *d){
    for(int i = 0; i < d->N; i++){
        for (int j = i+1; j < d->N; j++){
            if((WORD)d->a[i] > (WORD)d->a[j]){
                WORD *temp = d->a[i];
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
    for(v = Ccas->CCASRead(a); IsMCASDesc(v); v = Ccas->CCASRead(a))
        MCASHelp((MCASDesc *)v);
    return v;
}

bool MCAS::MCASHelp(MCASDesc *d){
    WORD v;
    MCASDesc *dd = (MCASDesc *) ((WORD)d & (~1));
    STATUS desired = FAILED;
    for(int i = 0; i < dd->N; i++){
        while(true){
            Ccas->doCCAS(dd->a[i], dd->e[i], (WORD)d | 1, &dd->status);
            v = *(dd->a[i]);
            if(v == dd->e[i] && dd->status == UNDECIDED ) 
                continue;
            if( v == (WORD)d|1) break;
            if(!(IsMCASDesc(v))) goto decision_point;
            MCASHelp( (MCASDesc *) v);
        }
    }
    desired = SUCCEEDED;
decision_point:
    //d->status.compare_exchange_strong(UNDECIDED, desired);
    bool vv = __sync_bool_compare_and_swap(&dd->status,UNDECIDED,desired);
    bool success = (d->status==SUCCEEDED);
    for(int i = 0; i < d->N; i++){
        WORD temp = (WORD)d |1;
        //d->a[i]->compare_exchange_strong((WORD*)temp, success? d->n[i] : d->e[i]);
        vv = __sync_bool_compare_and_swap(dd->a[i],temp,success? d->n[i] : d->e[i]);
        //if(!vv) printf("Kept the old value expected: %ld, got: %ld\n",temp,*(dd->a[i]));
    }
    return success;
}

bool MCAS::IsMCASDesc(WORD d){
    return (WORD)d & 1;
}

void MCAS::valueWrite(WORD *a, WORD b){
    *a = b<<2;
}

void MCAS::valueWriteInt(WORD *a, WORD b){
    *a = b;
}

WORD MCAS::valueRead(WORD *a){
    WORD v = MCASRead(a);
    return v>>2;
}