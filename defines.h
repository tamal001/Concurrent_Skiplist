#pragma once

#ifndef MAX_THREADS
#define MAX_THREADS 256
#endif

#ifndef PADDING_BYTES
#define PADDING_BYTES 64
#endif

#ifndef NR_LEVELS
#define NR_LEVELS 12
#endif

#ifndef int64
#define int64 int64_t
#endif 

#ifndef DEBUG
#define DEBUG if(0)
#define DEBUG1 if(0)
#define DEBUG2 if(0)
#endif

#ifndef VERBOSE
#define VERBOSE if(0)
#endif

#ifndef TRACE
#define TRACE if(0)
#endif

#ifndef TPRINT
#define TPRINT(str) cout<<"tid="<<tid<<": "<<str;
#endif

#define PRINT(name) { cout<<(#name)<<"="<<name<<endl; }

#define MINVAL INT32_MIN
#define MAXVAL INT32_MAX

#define MOR memory_order_relaxed

