#ifndef MDLBASE_H
#define MDLBASE_H
#include <stdio.h>
#include <stdint.h>

/*
** A MDL Key must be large enough to hold the largest unique particle key.
** It must be (several times) larger than the total number of particles.
** An "unsigned long" is normally 32 bits on a 32 bit machine and
** 64 bits on a 64 bit machine.  We use uint64_t to be sure.
*/
#ifndef MDLKEY
#define MDLKEY uint64_t
#endif
typedef MDLKEY mdlkey_t;
static const mdlkey_t MDL_INVALID_KEY = (mdlkey_t)(-1);

/*
** The purpose of this routine is to safely cast a size_t to an int.
** If this were done inline, the type of "v" would not be checked.
**
** We cast for the following reasons:
** - We have masked a mdlkey_t (to an local id or processor), so we
**   know it will fit in an integer.
** - We are taking a part of a memory block to send or receive
**   through MPI.  MPI takes an "int" parameter.
** - We have calculated a memory size with pointer subtraction and
**   know that it must be smaller than 4GB.
**
** The compiler will cast automatically, but warnings can be generated unless
** the cast is done explicitly.  Putting it inline is NOT type safe as in:
**   char *p;
**   int i;
**   i = (int)p;
** "works", while:
**   i = size_t_to_int(p);
** would fail.
*/
static inline int size_t_to_int(size_t v) {
    return (int)v;
    }

static inline int mdlkey_t_to_int(mdlkey_t v) {
    return (int)v;
    }

/*
* Compile time mdl debugging options
*
* mdl asserts: define MDLASSERT
* Probably should always be on unless you want no mdlDiag output at all
*
* NB: defining NDEBUG turns off all asserts so MDLASSERT will not assert
* however it will output uding mdlDiag and the code continues.
*/
#define MDLASSERT

typedef struct serviceRec {
    int nInBytes;
    int nOutBytes;
    void *p1;
    void(*fcnService)(void *, void *, int, void *, int *);
} SERVICE;

#define MAX_PROCESSOR_NAME      256
typedef struct {
    int nThreads; /* Global number of threads (total) */
    int idSelf;   /* Global index of this thread */
    int nProcs;   /* Number of global processes (e.g., MPI ranks) */
    int nCores;   /* Number of threads in this process */

    FILE *fpDiag;
    int bDiag;    /* When true, debug output is enabled */

    /* Services information */
    int nMaxServices;
    int nMaxInBytes;
    int nMaxOutBytes;
    SERVICE *psrv;
    char nodeName[MAX_PROCESSOR_NAME];
    } mdlBASE;

void mdlBaseInitialize(mdlBASE *base);
void mdlBaseFinish(mdlBASE *base);
void mdlBaseAddService(mdlBASE *base, int sid, void *p1,
    void(*fcnService)(void *, void *, int, void *, int *),
    int nInBytes, int nOutBytes);

#define mdlThreads(mdl) ((mdl)->base.nThreads)
#define mdlSelf(mdl) ((mdl)->base.idSelf)
const char *mdlName(void *mdl);

void mdlprintf(void *mdl, const char *format, ...);
#ifdef MDLASSERT
#ifndef __STRING
#define __STRING( arg )   (("arg"))
#endif
#define mdlassert(mdl,expr) \
    { \
    if (!(expr)) { \
    mdlprintf( mdl, "%s:%d Assertion `%s' failed.\n", __FILE__, __LINE__, __STRING(expr) ); \
    assert( expr ); \
        } \
    }
#else
#define mdlassert(mdl,expr)  assert(expr)
#endif

double mdlCpuTimer(void * mdl);

/*
* Timer functions active: define MDLTIMER
* Makes mdl timer functions active
*/
#ifndef _CRAYMPP
#define MDLTIMER
#endif

typedef struct {
    double wallclock;
    double cpu;
    double system;
    } mdlTimer;

#ifdef MDLTIMER
void mdlZeroTimer(void * mdl, mdlTimer *);
void mdlGetTimer(void * mdl, mdlTimer *, mdlTimer *);
void mdlPrintTimer(void *mdl, char *message, mdlTimer *);
#else
#define mdlZeroTimer
#define mdlGetTimer
#define mdlPrintTimer
#endif

#endif