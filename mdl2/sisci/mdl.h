#ifndef MDL_HINCLUDED
#define MDL_HINCLUDED
#include <stdio.h>
#include <assert.h>
#include <sisci_api.h>

#define SRV_STOP		0

#define MDL_CACHE_SIZE		64000000
#define MDL_CACHELINE_BITS	3
#define MDL_CACHELINE_ELTS	(1<<MDL_CACHELINE_BITS)
#define MDL_CACHE_MASK		(MDL_CACHELINE_ELTS-1)
#define MDL_INDEX_MASK		(~MDL_CACHE_MASK)

/*
** A MDL Key must be large enough to hold the largest unique particle key.
** It must be (several times) larger than the total number of particles.
** An "unsigned long" is normally 32 bits on a 32 bit machine and
** 64 bits on a 64 bit machine.
*/
#ifndef MDLKEY
#define MDLKEY unsigned long
#endif
typedef MDLKEY mdlkey_t;
static const mdlkey_t MDL_INVALID_KEY = (mdlkey_t)(-1);

typedef struct cacheTag {
	mdlkey_t iKey;
	int nLock;
	int nLast;
	int iLink;
	} CTAG;

/*
 ** This structure should be "maximally" aligned, with 4 ints it
 ** should align up to at least QUAD word, which should be enough.
 */
typedef struct cacheHeader {
	int cid;
	int mid;
	int id;
	int iLine;
	} CAHEAD;


typedef struct cacheSpace {
	int iType;
	char *pData;
	int iDataSize;
	int nData;
	int iLineSize;
	int nLines;
	int iLine;
	int nTrans;
        int iKeyShift;
        int iInvKeyShift;
	mdlkey_t iTransMask;
        mdlkey_t iIdMask;
	int *pTrans;
	CTAG *pTag;
	char *pLine;
	int nCheckIn;
	int nCheckOut;
	CAHEAD caReq;
	void (*init)(void *);
	void (*combine)(void *,void *);
	/*	
	 ** Statistics stuff.
	 */
	int nAccess;
	int nAccHigh;
	long nMiss;
	long nColl;
	long nMin;
	int nKeyMax;
	} CACHE;

typedef struct serviceRec {
	int nInBytes;
	int nOutBytes;
	void *p1;
	void (*fcnService)(void *,void *,int,void *,int *);	
	} SERVICE;


typedef struct mdlContext {
	int nThreads;
	int idSelf;
	int bDiag;
	FILE *fpDiag;
	int dontcare;
	int allgrp;
	/*
	 ** Services stuff!
	 */
	int nMaxServices;
	int nMaxSrvBytes;
	SERVICE *psrv;
	char *pszIn;
	char *pszOut;
	char *pszBuf;
	/*
	 ** Swapping buffer.
	 */
	char *pszTrans;
	/*
	 ** Caching stuff!
	 */
	unsigned long uRand;
	int iMaxDataSize;
	int iCaBufSize;
	char *pszRcv;
	int *pmidRpl;
	//MPI_Request *pReqRpl;
	//MPI_Request ReqRcv;
	char **ppszRpl;
	char *pszFlsh;
	int nMaxCacheIds;
	CACHE *cache;
	} * MDL;


/*
 * MDL debug and Timer macros and prototypes 
 */
/* 
 * Compile time mdl debugging options
 *
 * mdl asserts: define MDLASSERT
 * Probably should always be on unless you want no mdlDiag output at all
 *
 * NB: defining NDEBUG turns off all asserts so MDLASSERT will not assert
 * however it will output using mdlDiag and the code continues.
 */
#define MDLASSERT
/* 
 * Debug functions active: define MDLDEBUG
 * Adds debugging mdldebug prints and mdldebugassert asserts
 */
#define MDLDEBUG
/* 
 * Timer functions active: define MDLTIMER
 * Makes mdl timer functions active
 */
#define MDLTIMER


void mdlprintf( MDL mdl, const char *format, ... );

#ifdef MDLASSERT
#ifdef __ANSI_CPP__
#define mdlassert(mdl,expr) \
    { \
      if (!(expr)) { \
             mdlprintf( mdl, "%s:%d Assertion `%s' failed.\n", __FILE__, __LINE__, # expr ); \
             assert( expr ); \
             } \
    }
#else
#define mdlassert(mdl,expr) \
    { \
      if (!(expr)) { \
             mdlprintf( mdl, "%s:%d Assertion `%s' failed.\n", __FILE__, __LINE__, "expr" ); \
             assert( expr ); \
             } \
    }
#endif
#else
#define mdlassert(mdl,expr)  assert(expr)
#endif

#ifdef MDLDEBUG
#define mdldebugassert(mdl,expr)   mdlassert(mdl,expr)
void mdldebug( MDL mdl, const char *format, ... );
#else
#define mdldebug
#define mdldebugassert
#endif

typedef struct {
  double wallclock;
  double cpu;
  double system;
} mdlTimer;

#ifdef MDLTIMER
void mdlZeroTimer(MDL mdl,mdlTimer *);
void mdlGetTimer(MDL mdl,mdlTimer *,mdlTimer *);
void mdlPrintTimer(MDL mdl,char *message,mdlTimer *);
#else
#define mdlZeroTimer
#define mdlGetTimer
#define mdlPrintTimer
#endif

/*
 ** General Functions
 */
double mdlCpuTimer(MDL);
int mdlInitialize(MDL *,char **,void (*)(MDL));
void mdlFinish(MDL);
int mdlThreads(MDL);
int mdlSelf(MDL);
int mdlSwap(MDL,int,size_t,void *,size_t,size_t *,size_t *);
void mdlDiag(MDL,char *);
void mdlAddService(MDL,int,void *,void (*)(void *,void *,int,void *,int *),
				   int,int);
void mdlReqService(MDL,int,int,void *,int);
void mdlGetReply(MDL,int,void *,int *);
void mdlHandler(MDL);
/*
 ** Caching functions.
 */
void *mdlMalloc(MDL,size_t);
void mdlFree(MDL,void *);
void mdlROcache(MDL,int,void *,int,int);
void mdlCOcache(MDL,int,void *,int,int,
				void (*)(void *),void (*)(void *,void *));
void mdlFinishCache(MDL,int);
void mdlCacheCheck(MDL);
void mdlCacheBarrier(MDL,int);
void *mdlAquire(MDL,int,int,int);
void mdlPrefetch(MDL,int,int,int);
void mdlRelease(MDL,int,void *);
/*
 ** Cache statistics functions.
 */
double mdlNumAccess(MDL,int);
double mdlMissRatio(MDL,int);
double mdlCollRatio(MDL,int);
double mdlMinRatio(MDL,int);

#endif