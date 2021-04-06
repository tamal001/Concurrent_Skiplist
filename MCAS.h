#include <atomic>
#include "defines.h"
#include "CCAS.h"

using namespace std;

class MCAS{
public:
    struct MCASDesc{
        int N;
        int64 *a[2*NR_LEVELS+1];   //Arrary of addresses to perform CAS on
        int64 e[2*NR_LEVELS+1], n[2*NR_LEVELS+1];  //Expected values and new values at those addresses
        STATUS status;  // Status of CAS
        MCASDesc() : status(UNDECIDED){
            
        }
    };//__attribute__((aligned(PADDING_BYTES)));
    volatile char padding0[PADDING_BYTES];
    CCAS *Ccas;
    volatile char padding1[PADDING_BYTES];

    MCAS();
    ~MCAS();

    bool IsMCASDesc(int64 d);
    void AddressSort(MCASDesc *d);
    bool doMCAS(int64 *a[], int64 e[], int64 n[], int N);
    int64 MCASRead(int64 *a);
    bool MCASHelp(MCASDesc *d);
    //Read and write should be performed through MCAS. Because regular value is right shifted two paces. Descriptor can also be helped.
    void valueWrite(int64 *a, int64 b);
    void valueWriteInt(int64 *a, int64 b);
    int64 valueRead(int64 *a);
};

MCAS::MCAS(){
    Ccas= new CCAS();
}

MCAS::~MCAS(){
    delete Ccas;
}

bool MCAS::doMCAS(int64 *a[], int64 e[], int64 n[], int N){
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
            if((int64)d->a[i] > (int64)d->a[j]){
                int64 *temp = d->a[i];
                int64 Exp_temp = d->e[i];
                int64 New_temp = d->n[i];

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

int64 MCAS::MCASRead (int64 *a){
    int64 v;
    for(v = Ccas->CCASRead(a); IsMCASDesc(v); v = Ccas->CCASRead(a))
        MCASHelp((MCASDesc *)v);
    return v;
}

bool MCAS::MCASHelp(MCASDesc *d){
    int64 v;
    MCASDesc *dd = (MCASDesc *) ((int64)d & (~1));
    int64 desc = (int64)d | 1;
    STATUS desired = FAILED;
    for(int i = 0; i < dd->N; i++){
        while(true){
            Ccas->doCCAS(dd->a[i], dd->e[i], desc, &dd->status);
            v = *(dd->a[i]);
            if(v == dd->e[i] && dd->status == UNDECIDED ) 
                continue;
            if( v == desc) break;
            if(!(IsMCASDesc(v))) goto decision_point;
            MCASHelp( (MCASDesc *) v);
        }
    }
    desired = SUCCEEDED;
decision_point:
    //d->status.compare_exchange_strong(UNDECIDED, desired);
    bool vv = __sync_bool_compare_and_swap(&dd->status,UNDECIDED,desired);
    //if(!vv) printf("Status is %d\n",dd->status);
    bool success = (dd->status==SUCCEEDED);
    for(int i = 0; i < dd->N; i++){
        //int64 temp = (int64)d |1;
        //d->a[i]->compare_exchange_strong((int64*)temp, success? d->n[i] : d->e[i]);
        vv = __sync_bool_compare_and_swap(dd->a[i], desc, success? dd->n[i] : dd->e[i]);
    }
    return success;
}

bool MCAS::IsMCASDesc(int64 d){
    return (int64)d & 1;
}

void MCAS::valueWrite(int64 *a, int64 b){
    *a = b<<2;
}

void MCAS::valueWriteInt(int64 *a, int64 b){
    *a = b;
}

int64 MCAS::valueRead(int64 *a){
    int64 v = MCASRead(a);
    return v>>2;
}