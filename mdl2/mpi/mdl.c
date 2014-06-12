/*
 ** MPI version of MDL.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#if !defined(HAVE_CONFIG_H) || defined(HAVE_MALLOC_H)
#include <malloc.h>
#endif
#include <math.h>
#include <limits.h>
#include <assert.h>
#include <stdarg.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef __linux__
#include <sys/resource.h>
#endif
#include "mpi.h"
#include "mdl.h"

#define MDL_NOCACHE			0
#define MDL_ROCACHE			1
#define MDL_COCACHE			2

#define MDL_DEFAULT_BYTES		80000
#define MDL_DEFAULT_CACHEIDS	5

#define MDL_TRANS_SIZE		5000000
#define MDL_TAG_INIT 		1
#define MDL_TAG_SWAPINIT 	2
#define MDL_TAG_SWAP		3
#define MDL_TAG_REQ	   	4
#define MDL_TAG_RPL		5
#define MDL_TAG_SEND            6

typedef struct {
    OPA_Queue_element_hdr_t hdr;
    } serviceElement;

/*
 ** This structure should be "maximally" aligned, with 4 ints it
 ** should align up to at least QUAD word, which should be enough.
 */
typedef struct srvHeader {
    int32_t idFrom;
    int16_t coreTo;
    int16_t sid;
    int32_t nInBytes;
    int32_t nOutBytes;
    } SRVHEAD;

void mdlInitCommon(MDL mdl0, int iMDL,int bDiag,const char * argv0) {
    MDL mdl = mdl0->pmdl[iMDL];
    int i;

    if (iMDL) {
	mdlBaseInitialize(&mdl->base);
//	mdl->base = mdl0->base;
	mdl->base.iCore = iMDL;
	mdl->base.idSelf = mdl0->base.idSelf + iMDL;
	mdl->pmdl = mdl0->pmdl;
	}
    OPA_Queue_init(&mdl->inQueue);

    /*
    ** Set default "maximums" for structures. These are NOT hard
    ** maximums, as the structures will be realloc'd when these
    ** values are exceeded.
    */
    mdl->nMaxSrvBytes = 0;
    /*
    ** Allocate service buffers.
    */
    mdl->pszIn = NULL;
    mdl->pszOut = NULL;
    mdl->pszBuf = NULL;


    /*
    ** Allocate initial cache spaces.
    */
    mdl->nMaxCacheIds = MDL_DEFAULT_CACHEIDS;
    mdl->cache = malloc(mdl->nMaxCacheIds*sizeof(CACHE));
    assert(mdl->cache != NULL);
    /*
    ** Initialize caching spaces.
    */
    mdl->cacheSize = MDL_CACHE_SIZE;
    for (i = 0; i<mdl->nMaxCacheIds; ++i) {
	mdl->cache[i].iType = MDL_NOCACHE;
	}

    /*
    ** Allocate caching buffers, with initial data size of 0.
    ** We need one reply buffer for each thread, to deadlock situations.
    */
    mdl->iMaxDataSize = 0;
    mdl->iCaBufSize = sizeof(CAHEAD);
    mdl->pszRcv = malloc(mdl->iCaBufSize);
    assert(mdl->pszRcv != NULL);
    mdl->ppszRpl = malloc(mdl->base.nThreads*sizeof(char *));
    assert(mdl->ppszRpl != NULL);
    mdl->pmidRpl = malloc(mdl->base.nThreads*sizeof(int));
    assert(mdl->pmidRpl != NULL);
    for (i = 0; i<mdl->base.nThreads; ++i)
	mdl->pmidRpl[i] = -1;
    mdl->pReqRpl = malloc(mdl->base.nThreads*sizeof(MPI_Request));
    assert(mdl->pReqRpl != NULL);
    for (i = 0; i<mdl->base.nThreads; ++i) {
	mdl->ppszRpl[i] = malloc(mdl->iCaBufSize);
	assert(mdl->ppszRpl[i] != NULL);
	}
    mdl->pszFlsh = malloc(mdl->iCaBufSize);
    assert(mdl->pszFlsh != NULL);

    mdl->base.bDiag = bDiag;
    if (mdl->base.bDiag) {
	char achDiag[256], ach[256];
	const char *tmp = strrchr(argv0, '/');
	if (!tmp) tmp = argv0;
	else ++tmp;
	sprintf(achDiag, "%s/%s.%d", ach, tmp, mdlSelf(mdl));
	mdl->base.fpDiag = fopen(achDiag, "w");
	assert(mdl->base.fpDiag != NULL);
	}


    }


int mdlLaunch(int argc,char **argv,int (*fcnMaster)(MDL,int,char **),void (*fcnChild)(MDL)) {
    MDL mdl;
    int i,j,bDiag,bThreads;
    char *p, ach[256];

    mdl = malloc(sizeof(struct mdlContext));
    assert(mdl != NULL);
    mdlBaseInitialize(&mdl->base);

    /*
    ** Do some low level argument parsing for number of threads, and
    ** diagnostic flag!
    */
    for (argc = 0; argv[argc]; argc++);
    bDiag = 0;
    bThreads = 0;
    i = 1;
    while (argv[i]) {
	if (!strcmp(argv[i], "-sz") && !bThreads) {
	    ++i;
	    mdl->base.nCores = atoi(argv[i]);
	    if (argv[i]) bThreads = 1;
	    }
	if (!strcmp(argv[i], "+d") && !bDiag) {
	    p = getenv("MDL_DIAGNOSTIC");
	    if (!p) p = getenv("HOME");
	    if (!p) sprintf(ach, "/tmp");
	    else sprintf(ach, "%s", p);
	    bDiag = 1;
	    }
	++i;
	}
    argc = i;

    /* MPI Initialization */
    MPI_Init(&argc, &argv);
    mdl->commMDL = MPI_COMM_WORLD;
    MPI_Comm_size(mdl->commMDL, &mdl->base.nProcs);
    MPI_Comm_rank(mdl->commMDL, &mdl->base.iProc);

    /* Construct the thread/processor map */
    mdl->base.iProcToThread = malloc((mdl->base.nProcs + 1) * sizeof(int));
    assert(mdl->base.iProcToThread != NULL);
    mdl->base.iProcToThread[0] = 0;
    MPI_Allgather(&mdl->base.nCores, 1, MPI_INT, mdl->base.iProcToThread + 1, 1, MPI_INT, mdl->commMDL);
    for (i = 1; i < mdl->base.nProcs; ++i) mdl->base.iProcToThread[i + 1] += mdl->base.iProcToThread[i];
    mdl->base.nThreads = mdl->base.iProcToThread[mdl->base.nProcs];
    mdl->base.idSelf = mdl->base.iProcToThread[mdl->base.iProc];



    /*
     ** Allocate swapping transfer buffer. This buffer remains fixed.
     */
    mdl->pszTrans = malloc(MDL_TRANS_SIZE);
    assert(mdl->pszTrans != NULL);

    mdl->pmdl = malloc(mdl->base.nCores * sizeof(struct mdlContext *));
    mdl->pmdl[0] = mdl;
    mdl->threadid = malloc(mdl->base.nCores * sizeof(pthread_t));
    mdl->threadid[0] = 0;

    /* Allocate the other MDL structures for any threads. */
    for (i = 1; i < mdl->base.nCores; ++i) {
	mdl->pmdl[i] = malloc(sizeof(struct mdlContext));
	assert(mdl->pmdl[i] != NULL);
	}
    for (i = 0; i < mdl->base.nCores; ++i)
	mdlInitCommon(mdl, i, bDiag, argv[0]);

    /* This is true when there is a single thread per process. */
//    assert(mdl->base.nThreads == mdl->base.nProcs);
//    assert(mdl->base.idSelf == mdl->base.iProc);



    /* Launch child threads */
    if (mdl->base.nCores > 1) {
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	for (i = 1; i < mdl->base.nCores; ++i) {
	    pthread_create(&mdl->threadid[i], &attr,
		(void *(*)(void *))fcnChild,
		mdl->pmdl[i]);
	    }
	pthread_attr_destroy(&attr);
	}

    /* Child thread */
    if (mdl->base.idSelf) {
	(*fcnChild)(mdl);
	}
    else {
	fcnMaster(mdl,argc,argv);
	}
    mdlFinish(mdl);
    return(mdl->base.nThreads);
    }

void mdlFinish(MDL mdl) {
    int i;

    MPI_Barrier(mdl->commMDL);
    MPI_Finalize();
    /*
     ** Close Diagnostic file.
     */
    if (mdl->base.bDiag) {
	fclose(mdl->base.fpDiag);
	}
    /*
     ** Deallocate storage.
     */
    free(mdl->pszIn);
    free(mdl->pszOut);
    free(mdl->pszBuf);
    free(mdl->pszTrans);
    free(mdl->cache);
    free(mdl->pszRcv);
    free(mdl->pszFlsh);
    for (i=0;i<mdl->base.nThreads;++i) free(mdl->ppszRpl[i]);
    free(mdl->ppszRpl);
    free(mdl->pmidRpl);
    free(mdl->pReqRpl);
    free(mdl->base.iProcToThread);
    mdlBaseFinish(&mdl->base);
    free(mdl);
    }



/*
** This needs to be improved by abstracting away more of the MPI functionality
*/

int mdlBcast ( MDL mdl, void *buf, int count, MDL_Datatype datatype, int root ) {
    return MPI_Bcast( buf, count, datatype, root, mdl->commMDL );
    }

int mdlScan ( MDL mdl, void *sendbuf, void *recvbuf, int count,
		MDL_Datatype datatype, MDL_Op op ) {
    return MPI_Scan( sendbuf, recvbuf, count, datatype, op, mdl->commMDL );
    }

int mdlExscan ( MDL mdl, void *sendbuf, void *recvbuf, int count,
		MDL_Datatype datatype, MDL_Op op ) {
    return MPI_Exscan( sendbuf, recvbuf, count, datatype, op, mdl->commMDL );
    }

int mdlReduce ( MDL mdl, void *sendbuf, void *recvbuf, int count,
		MDL_Datatype datatype, MDL_Op op, int root ) {
    return MPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, mdl->commMDL );
    }

int mdlAllreduce ( MDL mdl, void *sendbuf, void *recvbuf, int count,
		MDL_Datatype datatype, MDL_Op op ) {
    return MPI_Allreduce( sendbuf, recvbuf, count, datatype, op, mdl->commMDL );
    }

int mdlAlltoall( MDL mdl, void *sendbuf, int scount, MDL_Datatype stype,
		 void *recvbuf, int rcount, MDL_Datatype rtype) {
    return MPI_Alltoall(sendbuf,scount,stype,
			recvbuf,rcount,rtype,mdl->commMDL);
    }

int mdlAlltoallv( MDL mdl, void *sendbuf, int *sendcnts, int *sdispls, MDL_Datatype sendtype,
    void *recvbuf, int *recvcnts, int *rdispls, MDL_Datatype recvtype) {
    return MPI_Alltoallv( sendbuf, sendcnts, sdispls, sendtype, 
        recvbuf, recvcnts, rdispls, recvtype, mdl->commMDL );
    }

int mdlAlltoallw( MDL mdl, void *sendbuf, int *sendcnts, int *sdispls, MDL_Datatype *stypes,
    void *recvbuf, int *recvcnts, int *rdispls, MDL_Datatype *rtypes) {
    return MPI_Alltoallw( sendbuf, sendcnts, sdispls, stypes,
        recvbuf, recvcnts, rdispls, rtypes, mdl->commMDL );
    }

int mdlAllGather( MDL mdl, void *sendbuf, int scount, MDL_Datatype stype,
    void *recvbuf, int rcount, MDL_Datatype recvtype) {
    return MPI_Allgather(sendbuf, scount, stype, recvbuf, rcount, recvtype, mdl->commMDL);
    } 

int mdlAllGatherv( MDL mdl, void *sendbuf, int scount, MDL_Datatype stype,
    void *recvbuf, int *recvcnts, int *rdisps, MDL_Datatype recvtype) {
    return MPI_Allgatherv(sendbuf, scount, stype, recvbuf, recvcnts, rdisps, recvtype, mdl->commMDL);
    } 

int mdlReduceScatter( MDL mdl, void* sendbuf, void* recvbuf, int *recvcounts,
    MDL_Datatype datatype, MDL_Op op) {
    return MPI_Reduce_scatter(sendbuf, recvbuf, recvcounts, datatype, op, mdl->commMDL );
    }

int mdlTypeContiguous(MDL mdl,int count, MDL_Datatype old_type, MDL_Datatype *newtype) {
    return MPI_Type_contiguous(count,old_type,newtype);
    }

int mdlTypeIndexed(MDL mdl, int count,
    int array_of_blocklengths[], int array_of_displacements[],
    MDL_Datatype oldtype, MDL_Datatype *newtype) {
    return MPI_Type_indexed(count,
	array_of_blocklengths,array_of_displacements,
	oldtype,newtype);
    }

int mdlTypeCommit(MDL mdl, MDL_Datatype *datatype) {
    return MPI_Type_commit(datatype);
    }

int mdlTypeFree (MDL mdl, MDL_Datatype *datatype ) {
    return MPI_Type_free(datatype);
    }

/*
** This function will transfer a block of data using a pack function.
** The corresponding node must call mdlRecv.
 */

#define SEND_BUFFER_SIZE (1*1024*1024)

void mdlSend(MDL mdl,int id,mdlPack pack, void *ctx) {
    size_t nBuff;
    char *vOut;

    vOut = malloc(SEND_BUFFER_SIZE);
    mdlassert(mdl,vOut!=NULL);

    do {
	nBuff = (*pack)(ctx,&id,SEND_BUFFER_SIZE,vOut);
	MPI_Ssend(vOut,nBuff,MPI_BYTE,id,MDL_TAG_SEND,mdl->commMDL);
	}
    while ( nBuff != 0 );

    free(vOut);
    }

void mdlRecv(MDL mdl,int id,mdlPack unpack, void *ctx) {
    void *vIn;
    size_t nUnpack;
    int nBytes;
    MPI_Status status;
    int inid;

    if ( id < 0 ) id = MPI_ANY_SOURCE;

    vIn = malloc(SEND_BUFFER_SIZE);
    mdlassert(mdl,vIn!=NULL);

    do {
	MPI_Recv(vIn,SEND_BUFFER_SIZE,MPI_BYTE,id,MDL_TAG_SEND,
		 mdl->commMDL,&status);
	MPI_Get_count(&status, MPI_BYTE, &nBytes);
	inid = status.MPI_SOURCE;
	nUnpack = (*unpack)(ctx,&inid,nBytes,vIn);
	}
    while (nUnpack>0 && nBytes>0);

    free(vIn);
    }

#ifdef NEW_SWAP
/* This is the inplace version */
int mdlSwap(MDL mdl,int id,size_t nBufBytes,void *vBuf,size_t nOutBytes,
	    size_t *pnSndBytes,size_t *pnRcvBytes) {
    MPI_Status status;
    size_t nInBytes, nSend;
    char *pszBuf = vBuf;

    struct swapInit {
	size_t nOutBytes;
	size_t nBufBytes;
	} swi,swo;

    swo.nOutBytes = nOutBytes;
    swo.nBufBytes = nBufBytes;

    /* First exchange counts */
    MPI_Sendrecv(&swo, sizeof(swo), MPI_BYTE, id, MDL_TAG_SWAPINIT,
	&swi, sizeof(swi), MPI_BYTE, id, MDL_TAG_SWAPINIT,
	mdl->commMDL, &status);
    if (swi.nBufBytes < nOutBytes || swi.nOutBytes > nBufBytes) {
	if (swi.nBufBytes < nOutBytes) nOutBytes = swi.nBufBytes;
	nInBytes = (swi.nOutBytes < nBufBytes) ? swi.nOutBytes : nBufBytes;
	printf("= ******************** %lu %lu %lu %lu : %lu %lu\n",
	    swo.nBufBytes, swo.nOutBytes, swi.nBufBytes, swi.nOutBytes,
	    nInBytes, nOutBytes);
	}
    else {
	if (swi.nBufBytes < nOutBytes) nOutBytes = swi.nBufBytes;
	nInBytes = (swi.nOutBytes < nBufBytes) ? swi.nOutBytes : nBufBytes;
	}

    /* Swap the common portion */
    if ( nOutBytes>0 && nInBytes>0 ) {
	nSend = (nOutBytes < nInBytes ? nOutBytes : nInBytes);
	MPI_Sendrecv_replace(pszBuf, nSend, MPI_BYTE, id, MDL_TAG_SWAP,
	    id, MDL_TAG_SWAP, mdl->commMDL, &status);
	pszBuf += nSend;
	}
    else nSend = 0;

    /* Now Send the excess */
    if ( nOutBytes > nInBytes ) {
	nSend = nOutBytes - nInBytes;
	MPI_Send(pszBuf, nSend, MPI_BYTE, id, MDL_TAG_SWAP, mdl->commMDL);
	}
    else if ( nInBytes > nOutBytes ) {
	nSend = nInBytes - nOutBytes;
	MPI_Recv(pszBuf, nSend, MPI_BYTE, id, MDL_TAG_SWAP, mdl->commMDL, &status);
	}

    *pnSndBytes = nOutBytes;
    *pnRcvBytes = nInBytes;
    return 1;
    }
#else
/*
 ** This is a tricky function. It initiates a bilateral transfer between
 ** two threads. Both threads MUST be expecting this transfer. The transfer
 ** occurs between idSelf <---> 'id' or 'id' <---> idSelf as seen from the
 ** opposing thread. It is designed as a high performance non-local memory
 ** swapping primitive and implementation will vary in non-trivial ways
 ** between differing architectures and parallel paradigms (eg. message
 ** passing and shared address space). A buffer is specified by 'pszBuf'
 ** which is 'nBufBytes' in size. Of this buffer the LAST 'nOutBytes' are
 ** transfered to the opponent, in turn, the opponent thread transfers his
 ** nBufBytes to this thread's buffer starting at 'pszBuf'.
 ** If the transfer completes with no problems the function returns 1.
 ** If the function returns 0 then one of the players has not received all
 ** of the others memory, however he will have successfully transfered all
 ** of his memory.
 */
int mdlSwap(MDL mdl,int id,size_t nBufBytes,void *vBuf,size_t nOutBytes,
	    size_t *pnSndBytes,size_t *pnRcvBytes) {
    size_t nInBytes,nOutBufBytes;
    size_t i;
    int nInMax,nOutMax,nBytes;
    int iTag, pid;
    char *pszBuf = vBuf;
    char *pszIn,*pszOut;
    struct swapInit {
	size_t nOutBytes;
	size_t nBufBytes;
	} swi,swo;
    MPI_Status status;
    MPI_Request request;

    *pnRcvBytes = 0;
    *pnSndBytes = 0;
    /*
     **	Send number of rejects to target thread amount of free space
     */
    swi.nOutBytes = nOutBytes;
    swi.nBufBytes = nBufBytes;
    MPI_Isend(&swi,sizeof(swi),MPI_BYTE,id,MDL_TAG_SWAPINIT,
	      mdl->commMDL, &request);
    /*
     ** Receive the number of target thread rejects and target free space
     */
    iTag = MDL_TAG_SWAPINIT;
    pid = id;
    MPI_Recv(&swo,sizeof(swo),MPI_BYTE,pid,iTag,mdl->commMDL, &status);
    MPI_Get_count(&status, MPI_BYTE, &nBytes);
    assert(nBytes == sizeof(swo));
    MPI_Wait(&request, &status);
    nInBytes = swo.nOutBytes;
    nOutBufBytes = swo.nBufBytes;
    /*
     ** Start bilateral transfers. Note: One processor is GUARANTEED to
     ** complete all its transfers.
     */
    assert(nBufBytes >= nOutBytes);
    pszOut = &pszBuf[nBufBytes-nOutBytes];
    pszIn = pszBuf;
    while (nOutBytes && nInBytes) {
	/*
	 ** nOutMax is the maximum number of bytes allowed to be sent
	 ** nInMax is the number of bytes which will be received.
	 */
	nOutMax = size_t_to_int((nOutBytes < MDL_TRANS_SIZE)?nOutBytes:MDL_TRANS_SIZE);
	nOutMax = size_t_to_int((nOutMax < nOutBufBytes)?nOutMax:nOutBufBytes);
	nInMax = size_t_to_int((nInBytes < MDL_TRANS_SIZE)?nInBytes:MDL_TRANS_SIZE);
	nInMax = size_t_to_int((nInMax < nBufBytes)?nInMax:nBufBytes);
	/*
	 ** Copy to a temp buffer to be safe.
	 */
	for (i=0;i<nOutMax;++i) mdl->pszTrans[i] = pszOut[i];
	MPI_Isend(mdl->pszTrans,nOutMax,MPI_BYTE,id,MDL_TAG_SWAP,
		  mdl->commMDL, &request);
	iTag = MDL_TAG_SWAP;
	pid = id;
	MPI_Recv(pszIn,nInMax,MPI_BYTE,pid,iTag,mdl->commMDL,
		 &status);
	MPI_Get_count(&status, MPI_BYTE, &nBytes);
	assert(nBytes == nInMax);
	MPI_Wait(&request, &status);
	/*
	 ** Adjust pointers and counts for next itteration.
	 */
	pszOut = &pszOut[nOutMax];
	nOutBytes -= nOutMax;
	nOutBufBytes -= nOutMax;
	*pnSndBytes += nOutMax;
	pszIn = &pszIn[nInMax];
	nInBytes -= nInMax;
	nBufBytes -= nInMax;
	*pnRcvBytes += nInMax;
	}
    /*
     ** At this stage we perform only unilateral transfers, and here we
     ** could exceed the opponent's storage capacity.
     ** Note: use of Ssend is mandatory here, also because of this we
     ** don't need to use the intermediate buffer mdl->pszTrans.
     */
    while (nOutBytes && nOutBufBytes) {
	nOutMax = size_t_to_int((nOutBytes < MDL_TRANS_SIZE)?nOutBytes:MDL_TRANS_SIZE);
	nOutMax = size_t_to_int((nOutMax < nOutBufBytes)?nOutMax:nOutBufBytes);
	MPI_Ssend(pszOut,nOutMax,MPI_BYTE,id,MDL_TAG_SWAP,
		  mdl->commMDL);
	pszOut = &pszOut[nOutMax];
	nOutBytes -= nOutMax;
	nOutBufBytes -= nOutMax;
	*pnSndBytes += nOutMax;
	}
    while (nInBytes && nBufBytes) {
	nInMax = size_t_to_int((nInBytes < MDL_TRANS_SIZE)?nInBytes:MDL_TRANS_SIZE);
	nInMax = size_t_to_int((nInMax < nBufBytes)?nInMax:nBufBytes);
	iTag = MDL_TAG_SWAP;
	MPI_Recv(pszIn,nInMax,MPI_BYTE,id,iTag,mdl->commMDL,
		 &status);
	MPI_Get_count(&status, MPI_BYTE, &nBytes);
	assert(nBytes == nInMax);
	pszIn = &pszIn[nInMax];
	nInBytes -= nInMax;
	nBufBytes -= nInMax;
	*pnRcvBytes += nInMax;
	}
    if (nOutBytes) return(0);
    else if (nInBytes) return(0);
    else return(1);
    }
#endif

void mdlCommitServices(MDL mdl) {
    int nMaxBytes;
    nMaxBytes = (mdl->base.nMaxInBytes > mdl->base.nMaxOutBytes) ? mdl->base.nMaxInBytes : mdl->base.nMaxOutBytes;
    if (nMaxBytes > mdl->nMaxSrvBytes) {
        mdl->pszIn = realloc(mdl->pszIn, nMaxBytes + sizeof(SRVHEAD) + sizeof(serviceElement));
        assert(mdl->pszIn != NULL);
	mdl->pszOut = realloc(mdl->pszOut, nMaxBytes + sizeof(SRVHEAD) + sizeof(serviceElement));
        assert(mdl->pszOut != NULL);
	mdl->pszBuf = realloc(mdl->pszBuf, nMaxBytes + sizeof(SRVHEAD) + sizeof(serviceElement));
        assert(mdl->pszBuf != NULL);
        mdl->nMaxSrvBytes = nMaxBytes;
        }
    }

void mdlAddService(MDL mdl,int sid,void *p1,
		   void (*fcnService)(void *,void *,int,void *,int *),
		   int nInBytes,int nOutBytes) {
    mdlBaseAddService(&mdl->base, sid, p1, fcnService, nInBytes, nOutBytes);
    }


void mdlReqService(MDL mdl,int id,int sid,void *vin,int nInBytes) {
    char *pszIn = vin;
    int iFirstThread = mdlProcToThread(mdl, mdl->base.iProc);
    int iCore = id - iFirstThread;

    /* Local request: add to the thread queue */
    if (iCore >= 0 && iCore < mdl->base.nCores) {
	assert(iCore != mdl->base.iCore); /* Cannot request to ourself */
	serviceElement *qhdr = (serviceElement*)mdl->pmdl[iCore]->pszIn;
	SRVHEAD *ph = (SRVHEAD *)(qhdr + 1);
	char *pszOut = (char *)(ph + 1);
	ph->idFrom = mdl->base.idSelf;
	ph->coreTo = iCore;
	ph->sid = sid;
	if (!pszIn) ph->nInBytes = 0;
	else ph->nInBytes = nInBytes;
	ph->nOutBytes = 0;
	if (nInBytes > 0) {
	    assert(pszIn != NULL);
	    memcpy(pszOut, pszIn, nInBytes);
	    }

	OPA_Queue_header_init(&qhdr->hdr);
	OPA_Queue_enqueue(&mdl->pmdl[iCore]->inQueue, qhdr, serviceElement, hdr);
	}

    /* Remote request -- queue to MPI thread*/
    else if (mdl->base.iCore > 0) {
	assert(mdl->base.iCore == 0); /* Forbidden for now */
	}

    /* Remote request -- we are the MPI thread */
    else {
	serviceElement *qhdr = (serviceElement*)mdl->pszBuf;
	SRVHEAD *ph = (SRVHEAD *)(qhdr + 1);
	char *pszOut = (char *)(ph + 1);
	int iProc = mdlThreadToProc(mdl, id);
	ph->idFrom = mdl->base.idSelf;
	ph->coreTo = id - mdlProcToThread(mdl,iProc);
	ph->sid = sid;
	if (!pszIn) ph->nInBytes = 0;
	else ph->nInBytes = nInBytes;
	ph->nOutBytes = 0;
	if (nInBytes > 0) {
	    assert(pszIn != NULL);
	    memcpy(pszOut, pszIn, nInBytes);
	    }
	MPI_Send(ph, nInBytes + (int)sizeof(SRVHEAD), MPI_BYTE, iProc, MDL_TAG_REQ, mdl->commMDL);
	}
    }


void mdlGetReply(MDL mdl,int id,void *vout,int *pnOutBytes) {
    char *pszOut = vout;
    SRVHEAD *ph = (SRVHEAD *)mdl->pszBuf;
    char *pszIn = &mdl->pszBuf[sizeof(SRVHEAD)];
    int i,iTag,nBytes;
    MPI_Status status;
    int iFirstThread = mdlProcToThread(mdl, mdl->base.iProc);
    int iCore = id - iFirstThread;
    /* Local request: add to the thread queue */
    if (iCore >= 0 && iCore < mdl->base.nCores) {
	assert(iCore != mdl->base.iCore); /* Cannot request to ourself */
	while (OPA_Queue_is_empty(&mdl->inQueue)) {
#ifdef _MSC_VER
	    SwitchToThread();
#else
	    sched_yield();
#endif
	    }
	serviceElement *qhdr;
	OPA_Queue_dequeue(&mdl->inQueue, qhdr, serviceElement, hdr);
	ph = (SRVHEAD *)(qhdr + 1);
	pszIn = (char *)(ph + 1);
	}
    else {
	int iProc = mdlThreadToProc(mdl, id);
	iTag = MDL_TAG_RPL;
	MPI_Recv(mdl->pszBuf, mdl->nMaxSrvBytes + (int)sizeof(SRVHEAD), MPI_BYTE,
	    iProc, iTag, mdl->commMDL, &status);
	MPI_Get_count(&status, MPI_BYTE, &nBytes);
	assert(nBytes == ph->nOutBytes + sizeof(SRVHEAD));
	assert(ph->coreTo == 0);
	}
    if (ph->nOutBytes > 0 && pszOut != NULL)
	memcpy(pszOut, pszIn, ph->nOutBytes);
    if (pnOutBytes) *pnOutBytes = ph->nOutBytes;
    }

void mdlStop(MDL mdl) {
    int id;

    /* Stop the worker processes */
    for ( id=1; id<mdl->base.nThreads; ++id ) {
	/*if (msr->param.bVDetails)*/
	printf("Stopping worker thread %d\n",id);
	mdlReqService(mdl,id,SRV_STOP,NULL,0);
	mdlGetReply(mdl,id,NULL,NULL);
	}
    printf( "MDL terminated\n" );
    }

void mdlHandler(MDL mdl) {
    serviceElement *qhi = (serviceElement *)(mdl->pszIn);
    serviceElement *qho = (serviceElement *)(mdl->pszOut);
    SRVHEAD *phi = (SRVHEAD *)(qhi + 1);
    SRVHEAD *pho = (SRVHEAD *)(qho + 1);
    char *pszIn = (char *)(phi + 1);
    char *pszOut = (char *)(pho + 1);
    int sid,iTag,id,nOutBytes,nBytes;
    MPI_Status status;
    MPI_Comm   comm;

    do {
	/* We need to check our queue */
	if (mdl->base.iCore > 0) {
	    while (OPA_Queue_is_empty(&mdl->inQueue)) {
#ifdef _MSC_VER
		SwitchToThread();
#else
		sched_yield();
#endif
		}
	    serviceElement *qhdr;
	    OPA_Queue_dequeue(&mdl->inQueue, qhdr, serviceElement, hdr);
	    phi = (SRVHEAD *)(qhdr + 1);
	    char *pszIn = (char *)(phi + 1);
	    id = phi->idFrom;
	    }
	else {
	    /* Save this communicator... reply ALWAYS goes here */
	    comm = mdl->commMDL;
	    iTag = MDL_TAG_REQ;
	    id = MPI_ANY_SOURCE;
	    MPI_Recv(phi, mdl->nMaxSrvBytes + sizeof(SRVHEAD),
		MPI_BYTE, id, iTag, comm, &status);
	    /*
	    ** Quite a few sanity checks follow.
	    */
	    id = status.MPI_SOURCE;
	    MPI_Get_count(&status, MPI_BYTE, &nBytes);
	    assert(nBytes == phi->nInBytes + sizeof(SRVHEAD));
	    assert(id == mdlThreadToProc(mdl, phi->idFrom));
	    assert(phi->coreTo == 0);
	    }
	sid = phi->sid;
	assert(sid < mdl->base.nMaxServices);
	if (phi->nInBytes > mdl->base.psrv[sid].nInBytes) {
	    printf("ERROR: pid=%d, sid=%d, nInBytes=%d, sid.nInBytes=%d\n",
		mdlSelf(mdl), sid, phi->nInBytes, mdl->base.psrv[sid].nInBytes);
	    }
	assert(phi->nInBytes <= mdl->base.psrv[sid].nInBytes);
	nOutBytes = 0;
	assert(mdl->base.psrv[sid].fcnService != NULL);
	(*mdl->base.psrv[sid].fcnService)(mdl->base.psrv[sid].p1, pszIn, phi->nInBytes,
	    pszOut, &nOutBytes);
	assert(nOutBytes <= mdl->base.psrv[sid].nOutBytes);
	pho->idFrom = mdl->base.idSelf;
	pho->sid = sid;
	pho->coreTo = 0;
	pho->nInBytes = phi->nInBytes;
	pho->nOutBytes = nOutBytes;
	if (mdl->base.iCore > 0) {
	    int iFirstThread = mdl->pmdl[0]->base.idSelf;
	    int iCore = id - iFirstThread;

	    /* Remote reply: to MPI thread */
	    if (iCore < 0 || iCore >= mdl->base.nCores) iCore = 0;

	    OPA_Queue_header_init(&qho->hdr);
	    OPA_Queue_enqueue(&mdl->pmdl[iCore]->inQueue, qho, serviceElement, hdr);
	    }
	else {
	    MPI_Send(pho, nOutBytes + sizeof(SRVHEAD),
		MPI_BYTE, id, MDL_TAG_RPL, comm);
	    }
	} while (sid != SRV_STOP);
    }

#define MDL_TAG_CACHECOM	10
#define MDL_MID_CACHEIN		1
#define MDL_MID_CACHEREQ	2
#define MDL_MID_CACHERPL	3
#define MDL_MID_CACHEOUT	4
#define MDL_MID_CACHEFLSH	5
#define MDL_MID_CACHEDONE	6

int mdlCacheReceive(MDL mdl,char *pLine) {
    CACHE *c;
    CAHEAD *ph = (CAHEAD *)mdl->pszRcv;
    char *pszRcv = &mdl->pszRcv[sizeof(CAHEAD)];
    CAHEAD *phRpl;
    char *pszRpl;
    char *t;
    int id, iTag;
    int s,n,i;
    MPI_Status status;
    int ret;
    int iLineSize;

    ret = MPI_Wait(&mdl->ReqRcv, &status);
    assert(ret == MPI_SUCCESS);

    c = &mdl->cache[ph->cid];
    assert(c->iType != MDL_NOCACHE);

    switch (ph->mid) {
    case MDL_MID_CACHEIN:
	++c->nCheckIn;
	ret = 0;
	break;
    case MDL_MID_CACHEOUT:
	++c->nCheckOut;
	ret = 0;
	break;
    case MDL_MID_CACHEREQ:
	/*
	 ** This is the tricky part! Here is where the real deadlock
	 ** difficulties surface. Making sure to have one buffer per
	 ** thread solves those problems here.
	 */
	pszRpl = &mdl->ppszRpl[ph->id][sizeof(CAHEAD)];
	phRpl = (CAHEAD *)mdl->ppszRpl[ph->id];
	phRpl->cid = ph->cid;
	phRpl->mid = MDL_MID_CACHERPL;
	phRpl->id = mdl->base.idSelf;

	assert(ph->iLine>=0);
	s = ph->iLine*MDL_CACHELINE_ELTS;
	n = s + MDL_CACHELINE_ELTS;
	if ( n > c->nData ) n = c->nData;
	iLineSize = (n-s) * c->iDataSize;
	for(i=s; i<n; i++ ) {
	    t = (*c->getElt)(c->pData,i,c->iDataSize);
	    memcpy(pszRpl+(i-s)*c->iDataSize,t,c->iDataSize);
	    }
	if (mdl->pmidRpl[ph->id] != -1) {
	    MPI_Wait(&mdl->pReqRpl[ph->id], &status);
	    }
	mdl->pmidRpl[ph->id] = 0;
	MPI_Isend(phRpl,(int)sizeof(CAHEAD)+iLineSize,MPI_BYTE,
		  ph->id, MDL_TAG_CACHECOM, mdl->commMDL,
		  &mdl->pReqRpl[ph->id]);
	ret = 0;
	break;
    case MDL_MID_CACHEFLSH:
	assert(c->iType == MDL_COCACHE);
	s = ph->iLine*MDL_CACHELINE_ELTS;
	n = s + MDL_CACHELINE_ELTS;
	if (n > c->nData) n = c->nData;
	for(i=s;i<n;i++) {
		(*c->combine)(c->ctx,(*c->getElt)(c->pData,i,c->iDataSize),
			      &pszRcv[(i-s)*c->iDataSize]);
	    }
	ret = 0;
	break;
    case MDL_MID_CACHERPL:
	/*
	 ** For now assume no prefetching!
	 ** This means that this WILL be the reply to this Aquire
	 ** request.
	 */
	assert(pLine != NULL);
	iLineSize = c->iLineSize;
	for (i=0;i<iLineSize;++i) pLine[i] = pszRcv[i];
	if (c->iType == MDL_COCACHE && c->init) {
	    /*
	     ** Call the initializer function for all elements in
	     ** the cache line.
	     */
	    for (i=0;i<c->iLineSize;i+=c->iDataSize) {
		    (*c->init)(c->ctx,&pLine[i]);
		}
	    }
	ret = 1;
	break;
    case MDL_MID_CACHEDONE:
	/*
	 * No more caches, shouldn't get here.
	 */
	assert(0);
	break;
    default:
	assert(0);
	}

    /*
     * Fire up next receive
     */
    id = MPI_ANY_SOURCE;
    iTag = MDL_TAG_CACHECOM;
    MPI_Irecv(mdl->pszRcv,mdl->iCaBufSize, MPI_BYTE, id,
	      iTag, mdl->commMDL, &mdl->ReqRcv);

    return ret;
    }


void AdjustDataSize(MDL mdl) {
    int i,iMaxDataSize;

    /*
     ** Change buffer size?
     */
    iMaxDataSize = 0;
    for (i=0;i<mdl->nMaxCacheIds;++i) {
	if (mdl->cache[i].iType == MDL_NOCACHE) continue;
	if (mdl->cache[i].iDataSize > iMaxDataSize) {
	    iMaxDataSize = mdl->cache[i].iDataSize;
	    }
	}
    if (iMaxDataSize != mdl->iMaxDataSize) {
	/*
	 ** Create new buffer with realloc?
	 ** Be very careful when reallocing buffers in other libraries
	 ** (not PVM) to be sure that the buffers are not in use!
	 ** A pending non-blocking receive on a buffer which is realloced
	 ** here will cause problems, make sure to take this into account!
	 ** This is certainly true in using the MPL library.
	 */
	MPI_Status status;
	CAHEAD caOut;

	/* cancel outstanding receive by sending a message to
	   myself */

	caOut.cid = 0;
	caOut.mid = MDL_MID_CACHEDONE;
	caOut.id = mdl->base.idSelf;
	MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, mdl->base.idSelf,
		 MDL_TAG_CACHECOM, mdl->commMDL);
	MPI_Wait(&mdl->ReqRcv, &status);

	mdl->iMaxDataSize = iMaxDataSize;
	mdl->iCaBufSize = (int)sizeof(CAHEAD) +
			  iMaxDataSize*(1 << MDL_CACHELINE_BITS);
	mdl->pszRcv = realloc(mdl->pszRcv,mdl->iCaBufSize);
	assert(mdl->pszRcv != NULL);
	for (i=0;i<mdl->base.nThreads;++i) {
	    mdl->ppszRpl[i] = realloc(mdl->ppszRpl[i],mdl->iCaBufSize);
	    assert(mdl->ppszRpl[i] != NULL);
	    }
	mdl->pszFlsh = realloc(mdl->pszFlsh,mdl->iCaBufSize);
	assert(mdl->pszFlsh != NULL);

	/*
	 * Fire up receive again.
	 */
	MPI_Irecv(mdl->pszRcv,mdl->iCaBufSize, MPI_BYTE,
		  MPI_ANY_SOURCE, MDL_TAG_CACHECOM,
		  mdl->commMDL, &mdl->ReqRcv);
	}
    }

/*
 ** Special MDL memory allocation functions for allocating memory
 ** which must be visible to other processors thru the MDL cache
 ** functions.
 ** mdlMalloc() is defined to return a pointer to AT LEAST iSize bytes
 ** of memory. This pointer will be passed to either mdlROcache or
 ** mdlCOcache as the pData parameter.
 ** For PVM and most machines these functions are trivial, but on the
 ** T3D and perhaps some future machines these functions are required.
 */
void *mdlMalloc(MDL mdl,size_t iSize) {
    void *ptr;
    int rc = MPI_Alloc_mem(iSize,MPI_INFO_NULL,&ptr);
    if (rc) return NULL;
    return ptr;
    /*return(malloc(iSize));*/
    }

void mdlFree(MDL mdl,void *p) {
    MPI_Free_mem(p);
    /*free(p);*/
    }

/*
** This is the default element fetch routine.  It impliments the old behaviour
** of a single large array.  New data structures need to be more clever.
*/
static void *getArrayElement(void *vData,int i,int iDataSize) {
    char *pData = vData;
    return pData + (size_t)i*(size_t)iDataSize;
    }

void mdlSetCacheSize(MDL mdl,int cacheSize) {
    mdl->cacheSize = cacheSize;
    }

/*
 ** Common initialization for all types of caches.
 */
CACHE *CacheInitialize(MDL mdl,int cid,
		       void * (*getElt)(void *pData,int i,int iDataSize),
		       void *pData,int iDataSize,int nData) {
    CACHE *c;
    int i,nMaxCacheIds;
    int first;

    /*
     ** Allocate more cache spaces if required!
     */
    assert(cid >= 0);
    /*
     * first cache?
     */
    first = 1;
    for (i = 0; i < mdl->nMaxCacheIds; ++i) {
	if (mdl->cache[i].iType != MDL_NOCACHE) {
	    first = 0;
	    break;
	    }
	}
    if (first) {
	/*
	 * Fire up first receive
	 */
	MPI_Irecv(mdl->pszRcv,mdl->iCaBufSize, MPI_BYTE, MPI_ANY_SOURCE,
		  MDL_TAG_CACHECOM, mdl->commMDL, &mdl->ReqRcv);
	mdlTimeReset(mdl);
	}

    if (cid >= mdl->nMaxCacheIds) {
	/*
	 ** reallocate cache spaces, adding space for 2 new cache spaces
	 ** including the one just defined.
	 */
	nMaxCacheIds = cid + 3;
	mdl->cache = realloc(mdl->cache,nMaxCacheIds*sizeof(CACHE));
	assert(mdl->cache != NULL);
	/*
	 ** Initialize the new cache slots.
	 */
	for (i=mdl->nMaxCacheIds;i<nMaxCacheIds;++i) {
	    mdl->cache[i].iType = MDL_NOCACHE;
	    }
	mdl->nMaxCacheIds = nMaxCacheIds;
	}
    c = &mdl->cache[cid];
    assert(c->iType == MDL_NOCACHE);
    c->getElt = getElt==NULL ? getArrayElement : getElt;
    c->pData = pData;
    c->iDataSize = iDataSize;
    c->nData = nData;
    c->iLineSize = MDL_CACHELINE_ELTS*c->iDataSize;

    c->iKeyShift = 0;
    while ((1 << c->iKeyShift) < mdl->base.nThreads) ++c->iKeyShift;
    c->iIdMask = (1 << c->iKeyShift) - 1;

    if (c->iKeyShift < MDL_CACHELINE_BITS) {
	/*
	 * Key will be (index & MDL_INDEX_MASK) | id.
	 */
	c->iInvKeyShift = MDL_CACHELINE_BITS;
	c->iKeyShift = 0;
	}
    else {
	/*
	 * Key will be (index & MDL_INDEX_MASK) << KeyShift | id.
	 */
	c->iInvKeyShift = c->iKeyShift;
	c->iKeyShift -= MDL_CACHELINE_BITS;
	}

    /*
     ** Determine the number of cache lines to be allocated.
     */
    assert( mdl->cacheSize >= (1<<MDL_CACHELINE_BITS)*10*c->iDataSize);
    c->nLines = (mdl->cacheSize/c->iDataSize) >> MDL_CACHELINE_BITS;
    c->nTrans = 1;
    while (c->nTrans < c->nLines) c->nTrans *= 2;
    c->nTrans *= 2;
    c->iTransMask = c->nTrans-1;
    /*
     **	Set up the translation table.
     */
    c->pTrans = malloc(c->nTrans*sizeof(int));
    assert(c->pTrans != NULL);
    for (i=0;i<c->nTrans;++i) c->pTrans[i] = 0;
    /*
     ** Set up the tags. Note pTag[0] is a Sentinel!
     */
    c->pTag = malloc(c->nLines*sizeof(CTAG));
    assert(c->pTag != NULL);
    for (i=0;i<c->nLines;++i) {
	c->pTag[i].iKey = MDL_INVALID_KEY;
	c->pTag[i].nLock = 0;
	c->pTag[i].iLink = 0;
	}
    c->pTag[0].nLock = 1;     /* always locked */
    c->iLastVictim = 0;       /* This makes sure we have the first iVictim = 1 */
    c->nAccess = 0;
    c->nMiss = 0;				/* !!!, not NB */
    c->nColl = 0;				/* !!!, not NB */
    /*
     ** Allocate cache data lines.
     */
    c->pLine = malloc(mdl->cacheSize);
    assert(c->pLine != NULL);
    c->nCheckOut = 0;

    /*
     ** Set up the request message as much as possible!
     */
    c->caReq.cid = cid;
    c->caReq.mid = MDL_MID_CACHEREQ;
    c->caReq.id = mdl->base.idSelf;
    return(c);
    }

/*
 ** Initialize a Read-Only caching space.
 */
void mdlROcache(MDL mdl,int cid,
		void * (*getElt)(void *pData,int i,int iDataSize),
		void *pData,int iDataSize,int nData) {
    CACHE *c;
    int id;
    CAHEAD caIn;
    char achDiag[256];

    c = CacheInitialize(mdl,cid,getElt,pData,iDataSize,nData);
    c->iType = MDL_ROCACHE;
    /*
     ** For an ROcache these two functions are not needed.
     */
    c->init = NULL;
    c->combine = NULL;
    sprintf(achDiag, "%d: before CI, cache %d\n", mdl->base.idSelf, cid);
    mdlDiag(mdl, achDiag);
    /*
     ** THIS IS A SYNCHRONIZE!!!
     */
    caIn.cid = cid;
    caIn.mid = MDL_MID_CACHEIN;
    caIn.id = mdl->base.idSelf;
    if (mdl->base.idSelf == 0) {
	c->nCheckIn = 1;
	while (c->nCheckIn < mdl->base.nThreads) {
	    mdlCacheReceive(mdl, NULL);
	    }
	}
    else {
	/*
	 ** Must use non-blocking sends here, we will never wait
	 ** for these sends to complete, but will know for sure
	 ** that they have completed.
	 */
	MPI_Send(&caIn,sizeof(CAHEAD),MPI_BYTE, 0,
		 MDL_TAG_CACHECOM, mdl->commMDL);
	}
    sprintf(achDiag, "%d: In CI, cache %d\n", mdl->base.idSelf, cid);
    mdlDiag(mdl, achDiag);
    if (mdl->base.idSelf == 0) {
	for (id = 1; id < mdl->base.nThreads; id++) {
	    MPI_Send(&caIn,sizeof(CAHEAD),MPI_BYTE, id,
		     MDL_TAG_CACHECOM, mdl->commMDL);
	    }
	}
    else {
	c->nCheckIn = 0;
	while (c->nCheckIn == 0) {
	    mdlCacheReceive(mdl,NULL);
	    }
	}
    sprintf(achDiag, "%d: After CI, cache %d\n", mdl->base.idSelf, cid);
    mdlDiag(mdl, achDiag);
    AdjustDataSize(mdl);
    MPI_Barrier(mdl->commMDL);
    }

/*
 ** Initialize a Combiner caching space.
 */
void mdlCOcache(MDL mdl,int cid,
		void * (*getElt)(void *pData,int i,int iDataSize),
		void *pData,int iDataSize,int nData,
		void *ctx,void (*init)(void *,void *),void (*combine)(void *,void *,void *)) {
    CACHE *c;
    int id;
    CAHEAD caIn;

    c = CacheInitialize(mdl,cid,getElt,pData,iDataSize,nData);
    c->iType = MDL_COCACHE;
    assert(init);
    c->init = init;
    assert(combine);
    c->combine = combine;
    c->ctx = ctx;
    /*
     ** THIS IS A SYNCHRONIZE!!!
     */
    caIn.cid = cid;
    caIn.mid = MDL_MID_CACHEIN;
    caIn.id = mdl->base.idSelf;
    if (mdl->base.idSelf == 0) {
	c->nCheckIn = 1;
	while (c->nCheckIn < mdl->base.nThreads) {
	    mdlCacheReceive(mdl, NULL);
	    }
	}
    else {
	/*
	 ** Must use non-blocking sends here, we will never wait
	 ** for these sends to complete, but will know for sure
	 ** that they have completed.
	 */
	MPI_Send(&caIn,sizeof(CAHEAD),MPI_BYTE, 0,
		 MDL_TAG_CACHECOM, mdl->commMDL);
	}
    if (mdl->base.idSelf == 0) {
	for (id = 1; id < mdl->base.nThreads; id++) {
	    MPI_Send(&caIn,sizeof(CAHEAD),MPI_BYTE, id,
		     MDL_TAG_CACHECOM, mdl->commMDL);
	    }
	}
    else {
	c->nCheckIn = 0;
	while (c->nCheckIn == 0) {
	    mdlCacheReceive(mdl,NULL);
	    }
	}
    AdjustDataSize(mdl);
    MPI_Barrier(mdl->commMDL);
    }

void mdlFlushCache(MDL mdl,int cid) {
    CACHE *c = &mdl->cache[cid];
    CAHEAD caOut;
    CAHEAD *caFlsh = (CAHEAD *)mdl->pszFlsh;
    char *pszFlsh = &mdl->pszFlsh[sizeof(CAHEAD)];
    mdlkey_t iKey;
    int i,id;
    char *t;
    int j;
    int last;
    MPI_Status status;
    MPI_Request reqFlsh;
    MPI_Request reqBoth[2];
    int index;

    mdlTimeAddComputing(mdl);
    if (c->iType == MDL_COCACHE) {
	/*
	 ** Must flush all valid data elements.
	 */
	caFlsh->cid = cid;
	caFlsh->mid = MDL_MID_CACHEFLSH;
	caFlsh->id = mdl->base.idSelf;
	for (i=1;i<c->nLines;++i) {
	    iKey = c->pTag[i].iKey;
	    if (iKey != MDL_INVALID_KEY) {
		/*
		 ** Flush element since it is valid!
		 */
		id = mdlkey_t_to_int(iKey & c->iIdMask);
		caFlsh->iLine = mdlkey_t_to_int(iKey >> c->iInvKeyShift);
		t = &c->pLine[i*c->iLineSize];
		for (j = 0; j < c->iLineSize; ++j)
		    pszFlsh[j] = t[j];
		/*
		 * Use Synchronous send so as not to
		 * overwhelm the receiver.
		 */
		MPI_Issend(caFlsh, (int)sizeof(CAHEAD)+c->iLineSize,
			   MPI_BYTE, id, MDL_TAG_CACHECOM,
			   mdl->commMDL, &reqFlsh);
		/*
		 * Wait for the Flush to complete, but
		 * also service any incoming cache requests.
		*/
		reqBoth[0] = mdl->ReqRcv;
		reqBoth[1] = reqFlsh;

		while (1) {
		    MPI_Waitany(2, reqBoth, &index, &status);
		    assert(!(index != 0 && reqBoth[0] ==
			     MPI_REQUEST_NULL));
		    mdl->ReqRcv = reqBoth[0];
		    if (index == 1) /* Flush has completed */
			break;
		    else if (index == 0) {
			mdlCacheReceive(mdl, NULL);
			reqBoth[0] = mdl->ReqRcv;
			}
		    else
			assert(0);
		    }
		}
		c->pTag[i].iKey = MDL_INVALID_KEY;
	    }
	}
    mdlTimeAddSynchronizing(mdl);
    }

void mdlFinishCache(MDL mdl,int cid) {
    CACHE *c = &mdl->cache[cid];
    CAHEAD caOut;
    CAHEAD *caFlsh = (CAHEAD *)mdl->pszFlsh;
    char *pszFlsh = &mdl->pszFlsh[sizeof(CAHEAD)];
    mdlkey_t iKey;
    int i,id;
    char *t;
    int j;
    int last;
    MPI_Status status;
    MPI_Request reqFlsh;
    MPI_Request reqBoth[2];
    int index;

    mdlTimeAddComputing(mdl);
    if (c->iType == MDL_COCACHE) {
	/*
	 * Extra checkout to let everybody finish before
	 * flushes start.
	* I think this makes for bad synchronizes --trq
	caOut.cid = cid;
	caOut.mid = MDL_MID_CACHEOUT;
	caOut.id = mdl->base.idSelf;
	for(id = 0; id < mdl->base.nThreads; id++) {
	    if(id == mdl->base.idSelf)
		continue;
	    MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, id,
		     MDL_TAG_CACHECOM, mdl->commMDL);
	    }
	++c->nCheckOut;
	while(c->nCheckOut < mdl->base.nThreads) {
	  mdlCacheReceive(mdl, NULL);
	    }
	c->nCheckOut = 0;
	 */
	/*
	 ** Must flush all valid data elements.
	 */
	caFlsh->cid = cid;
	caFlsh->mid = MDL_MID_CACHEFLSH;
	caFlsh->id = mdl->base.idSelf;
	for (i=1;i<c->nLines;++i) {
	    iKey = c->pTag[i].iKey;
	    if (iKey != MDL_INVALID_KEY) {
		/*
		 ** Flush element since it is valid!
		 */
		id = mdlkey_t_to_int(iKey & c->iIdMask);
		caFlsh->iLine = mdlkey_t_to_int(iKey >> c->iInvKeyShift);
		t = &c->pLine[i*c->iLineSize];
		for (j = 0; j < c->iLineSize; ++j)
		    pszFlsh[j] = t[j];
		/*
		 * Use Synchronous send so as not to
		 * overwhelm the receiver.
		 */
		MPI_Issend(caFlsh, (int)sizeof(CAHEAD)+c->iLineSize,
			   MPI_BYTE, id, MDL_TAG_CACHECOM,
			   mdl->commMDL, &reqFlsh);
		/*
		 * Wait for the Flush to complete, but
		 * also service any incoming cache requests.
		*/
		reqBoth[0] = mdl->ReqRcv;
		reqBoth[1] = reqFlsh;

		while (1) {
		    MPI_Waitany(2, reqBoth, &index, &status);
		    assert(!(index != 0 && reqBoth[0] ==
			     MPI_REQUEST_NULL));
		    mdl->ReqRcv = reqBoth[0];
		    if (index == 1) /* Flush has completed */
			break;
		    else if (index == 0) {
			mdlCacheReceive(mdl, NULL);
			reqBoth[0] = mdl->ReqRcv;
			}
		    else
			assert(0);
		    }
		}
	    }
	}
    /*
     ** THIS IS A SYNCHRONIZE!!!
     */
    caOut.cid = cid;
    caOut.mid = MDL_MID_CACHEOUT;
    caOut.id = mdl->base.idSelf;
    if (mdl->base.idSelf == 0) {
	++c->nCheckOut;
	while (c->nCheckOut < mdl->base.nThreads) {
	    mdlCacheReceive(mdl, NULL);
	    }
	}
    else {
	MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, 0,
		 MDL_TAG_CACHECOM, mdl->commMDL);
	}
    if (mdl->base.idSelf == 0) {
	for (id = 1; id < mdl->base.nThreads; id++) {
	    MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, id,
		     MDL_TAG_CACHECOM, mdl->commMDL);
	    }
	}
    else {
	c->nCheckOut = 0;
	while (c->nCheckOut == 0) {
	    mdlCacheReceive(mdl,NULL);
	    }
	}
    /*
     ** Free up storage and finish.
     */
    free(c->pTrans);
    free(c->pTag);
    free(c->pLine);
    c->iType = MDL_NOCACHE;

    AdjustDataSize(mdl);
    /*
     * last cache?
     */
    last = 1;
    for (i = 0; i < mdl->nMaxCacheIds; ++i) {
	if (mdl->cache[i].iType != MDL_NOCACHE) {
	    last = 0;
	    break;
	    }
	}
    /*
     * shut down CacheReceive.
     * Note: I'm sending a message to myself.
     */
    if (last) {
	MPI_Status status;

	caOut.cid = cid;
	caOut.mid = MDL_MID_CACHEDONE;
	caOut.id = mdl->base.idSelf;
	MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, mdl->base.idSelf,
		 MDL_TAG_CACHECOM, mdl->commMDL);
	MPI_Wait(&mdl->ReqRcv, &status);
	}
    MPI_Barrier(mdl->commMDL);
    mdlTimeAddSynchronizing(mdl);
    }


void mdlCacheBarrier(MDL mdl,int cid) {
    CACHE *c = &mdl->cache[cid];
    CAHEAD caOut;
    int id;
    mdlTimeAddComputing(mdl);

    /*
    ** THIS IS A SYNCHRONIZE!!!
    */
    caOut.cid = cid;
    caOut.mid = MDL_MID_CACHEOUT;
    caOut.id = mdl->base.idSelf;
    if (mdl->base.idSelf == 0) {
	++c->nCheckOut;
	while (c->nCheckOut < mdl->base.nThreads) {
	    mdlCacheReceive(mdl, NULL);
	    }
	}
    else {
	MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, 0,
		 MDL_TAG_CACHECOM, mdl->commMDL);
	}
    if (mdl->base.idSelf == 0) {
	for (id = 1; id < mdl->base.nThreads; id++) {
	    MPI_Send(&caOut,sizeof(CAHEAD),MPI_BYTE, id,
		     MDL_TAG_CACHECOM, mdl->commMDL);
	    }
	}
    else {
	c->nCheckOut = 0;
	while (c->nCheckOut == 0) {
	    mdlCacheReceive(mdl,NULL);
	    }
	}
    c->nCheckOut = 0;
    MPI_Barrier(mdl->commMDL);
    mdlTimeAddSynchronizing(mdl);
    }


void mdlCacheCheck(MDL mdl) {
    int flag;
    MPI_Status status;

    while (1) {
	MPI_Test(&mdl->ReqRcv, &flag, &status);
	if (flag == 0)
	    break;

	mdlCacheReceive(mdl,NULL);
	}
    }


void *mdlDoMiss(MDL mdl, int cid, int iIndex, int id, mdlkey_t iKey, int lock) {
    CACHE *c = &mdl->cache[cid];
    char *pLine;
    int iElt,i,iLine;
    mdlkey_t iKeyVic;
    int idVic;
    int iVictim,*pi;
    char ach[80];
    CAHEAD *caFlsh;
    char *pszFlsh;
    MPI_Status status;
    MPI_Request reqFlsh;

    mdlTimeAddComputing(mdl);

    /*
    ** Cache Miss.
    */
    iLine = iIndex >> MDL_CACHELINE_BITS;
    c->caReq.cid = cid;
    c->caReq.mid = MDL_MID_CACHEREQ;
    c->caReq.id = mdl->base.idSelf;
    c->caReq.iLine = iLine;
    MPI_Send(&c->caReq,sizeof(CAHEAD),MPI_BYTE,
	     id,MDL_TAG_CACHECOM, mdl->commMDL);
    ++c->nMiss;

    if (++c->iLastVictim == c->nLines) c->iLastVictim = 1;
    iVictim = c->iLastVictim;

    iElt = iIndex & MDL_CACHE_MASK;
    for (i=1;i<c->nLines;++i) {
	if (!c->pTag[iVictim].nLock) {
	    /*
	     ** Found victim.
	     */
	    iKeyVic = c->pTag[iVictim].iKey;
	    /*
	     ** 'pLine' will point to the actual data line in the cache.
	     */
	    pLine = &c->pLine[iVictim*c->iLineSize];
	    caFlsh = NULL;
	    if (iKeyVic != MDL_INVALID_KEY) {
		if (c->iType == MDL_COCACHE) {
		    /*
		    ** Flush element since it is valid!
		    */
		    idVic = mdlkey_t_to_int(iKeyVic&c->iIdMask);
		    caFlsh = (CAHEAD *)mdl->pszFlsh;
		    pszFlsh = &mdl->pszFlsh[sizeof(CAHEAD)];
		    caFlsh->cid = cid;
		    caFlsh->mid = MDL_MID_CACHEFLSH;
		    caFlsh->id = mdl->base.idSelf;
		    caFlsh->iLine = mdlkey_t_to_int(iKeyVic >> c->iInvKeyShift);
		    for (i = 0; i < c->iLineSize; ++i)
			pszFlsh[i] = pLine[i];
		    MPI_Isend(caFlsh, (int)sizeof(CAHEAD)+c->iLineSize,
			      MPI_BYTE, idVic,
			      MDL_TAG_CACHECOM, mdl->commMDL, &reqFlsh);
		    }
		/*
		 ** If valid iLine then "unlink" it from the cache.
		 */
		pi = &c->pTrans[iKeyVic & c->iTransMask];
		while (*pi != iVictim) pi = &c->pTag[*pi].iLink;
		*pi = c->pTag[iVictim].iLink;
		}
	    c->pTag[iVictim].iKey = iKey;
	    if (lock) c->pTag[iVictim].nLock = 1;
	    /*
	     **	Add the modified victim tag back into the cache.
	     ** Note: the new element is placed at the head of the chain.
	     */
	    pi = &c->pTrans[iKey & c->iTransMask];
	    c->pTag[iVictim].iLink = *pi;
	    *pi = iVictim;
	    goto Await;
	    }
	if (++iVictim == c->nLines) iVictim = 1;
	}
    /*
     ** Cache Failure!
     */
    sprintf(ach,"MDL CACHE FAILURE: cid == %d, no unlocked lines!\n",cid);
    mdlDiag(mdl,ach);
    exit(1);
Await:
    c->iLastVictim = iVictim;
    /*
     ** At this point 'pLine' is the recipient cache line for the
     ** data requested from processor 'id'.
     */
    while (1) {
	if (mdlCacheReceive(mdl,pLine)) {
	    if (caFlsh)
		MPI_Wait(&reqFlsh, &status);
	    mdlTimeAddWaiting(mdl);
	    return(&pLine[iElt*c->iDataSize]);
	    }
	}
    }


void mdlRelease(MDL mdl,int cid,void *p) {
    CACHE *c = &mdl->cache[cid];
    int iLine;

    iLine = ((char *)p - c->pLine) / c->iLineSize;
    /*
     ** Check if the pointer fell in a cache line, otherwise it
     ** must have been a local pointer.
     */
    if (iLine > 0 && iLine < c->nLines) {
	--c->pTag[iLine].nLock;
	assert(c->pTag[iLine].nLock >= 0);
	}
    }


double mdlNumAccess(MDL mdl,int cid) {
    CACHE *c = &mdl->cache[cid];

    return(c->nAccess);
    }


double mdlMissRatio(MDL mdl,int cid) {
    CACHE *c = &mdl->cache[cid];
    double dAccess = c->nAccess;

    if (dAccess > 0.0) return(c->nMiss/dAccess);
    else return(0.0);
    }


double mdlCollRatio(MDL mdl,int cid) {
    CACHE *c = &mdl->cache[cid];
    double dAccess = c->nAccess;

    if (dAccess > 0.0) return(c->nColl/dAccess);
    else return(0.0);
    }

/*
** GRID Geometry information.  The basic process is as follows:
** - Initialize: Create a MDLGRID giving the global geometry information (total grid size)
** - SetLocal:   Set the local grid geometry (which slabs are on this processor)
** - GridShare:  Share this information between processors
** - Malloc:     Allocate one or more grid instances
** - Free:       Free the memory for all grid instances
** - Finish:     Free the GRID geometry information.
*/
void mdlGridInitialize(MDL mdl,MDLGRID *pgrid,int n1,int n2,int n3,int a1) {
    MDLGRID grid;
    assert(n1>0&&n2>0&&n3>0);
    assert(n1<=a1);
    *pgrid = grid = malloc(sizeof(struct mdlGridContext)); assert(grid!=NULL);
    grid->n1 = n1;
    grid->n2 = n2;
    grid->n3 = n3;
    grid->a1 = a1;

    /* This will be shared later (see mdlGridShare) */
    grid->id = malloc(sizeof(*grid->id)*(grid->n3));    assert(grid->id!=NULL);
    grid->rs = mdlMalloc(mdl,sizeof(*grid->rs)*mdl->base.nThreads); assert(grid->rs!=NULL);
    grid->rn = mdlMalloc(mdl,sizeof(*grid->rn)*mdl->base.nThreads); assert(grid->rn!=NULL);

    /* The following need to be set to appropriate values still. */
    grid->s = grid->n = grid->nlocal = 0;
    }

void mdlGridFinish(MDL mdl, MDLGRID grid) {
    if (grid->rs) free(grid->rs);
    if (grid->rn) free(grid->rn);
    if (grid->id) free(grid->id);
    free(grid);
    }

void mdlGridSetLocal(MDL mdl,MDLGRID grid,int s, int n, int nlocal) {
    assert( s>=0 && s<grid->n3);
    assert( n>=0 && s+n<=grid->n3);
    grid->s = s;
    grid->n = n;
    grid->nlocal = nlocal;
    }

/*
** Share the local GRID information with other processors by,
**   - finding the starting slab and number of slabs on each processor
**   - building a mapping from slab to processor id.
*/
void mdlGridShare(MDL mdl,MDLGRID grid) {
    int i, id;

    MPI_Allgather(&grid->s,sizeof(*grid->rs),MPI_BYTE,
		  grid->rs,sizeof(*grid->rs),MPI_BYTE,
		  mdl->commMDL);
    MPI_Allgather(&grid->n,sizeof(*grid->rn),MPI_BYTE,
		  grid->rn,sizeof(*grid->rn),MPI_BYTE,
		  mdl->commMDL);
    /* Calculate on which processor each slab can be found. */
    for(id=0; id<mdl->base.nThreads; id++ ) {
	for( i=grid->rs[id]; i<grid->rs[id]+grid->rn[id]; i++ ) grid->id[i] = id;
	}
    }

/*
** Allocate the local elements.  The size of a single element is
** given and the local GRID information is consulted to determine
** how many to allocate.
*/
void *mdlGridMalloc(MDL mdl,MDLGRID grid,int nEntrySize) {
    return mdlMalloc(mdl,nEntrySize*grid->nlocal);
    }

void mdlGridFree( MDL mdl, MDLGRID grid, void *p ) {
    mdlFree(mdl,p);
    }

#ifdef MDL_FFTW
size_t mdlFFTInitialize(MDL mdl,MDLFFT *pfft,
			int n1,int n2,int n3,int bMeasure) {
    MDLFFT fft;
    int sz,nz,sy,ny,nlocal;

    *pfft = NULL;
    fft = malloc(sizeof(struct mdlFFTContext));
    assert(fft != NULL);

    /* Contruct the FFTW plans */
    fft->fplan = rfftw3d_mpi_create_plan(mdl->commMDL,
					 n3, n2, n1,
					 FFTW_REAL_TO_COMPLEX,
					 (bMeasure ? FFTW_MEASURE : FFTW_ESTIMATE) );
    fft->iplan = rfftw3d_mpi_create_plan(mdl->commMDL,
					 /* dim.'s of REAL data --> */ n3, n2, n1,
					 FFTW_COMPLEX_TO_REAL,
					 (bMeasure ? FFTW_MEASURE : FFTW_ESTIMATE));
    rfftwnd_mpi_local_sizes( fft->fplan, &nz, &sz, &ny, &sy,&nlocal);

    /*
    ** Dimensions of k-space and r-space grid.  Note transposed order.
    ** Note also that the "actual" dimension 1 side of the r-space array
    ** can be (and usually is) larger than "n1" because of the inplace FFT.
    */
    mdlGridInitialize(mdl,&fft->rgrid,n1,n2,n3,2*(n1/2+1));
    mdlGridInitialize(mdl,&fft->kgrid,n1/2+1,n3,n2,n1/2+1);

    mdlGridSetLocal(mdl,fft->rgrid,sz,nz,nlocal);
    mdlGridSetLocal(mdl,fft->kgrid,sy,ny,nlocal/2);
    mdlGridShare(mdl,fft->rgrid);
    mdlGridShare(mdl,fft->kgrid);

    *pfft = fft;
    return nlocal;
    }

void mdlFFTFinish( MDL mdl, MDLFFT fft ) {
    rfftwnd_mpi_destroy_plan(fft->fplan);
    rfftwnd_mpi_destroy_plan(fft->iplan);
    mdlGridFinish(mdl,fft->kgrid);
    mdlGridFinish(mdl,fft->rgrid);
    free(fft);
    }

fftw_real *mdlFFTMAlloc( MDL mdl, MDLFFT fft ) {
    return mdlGridMalloc(mdl,fft->rgrid,sizeof(fftw_real));
    }

void mdlFFTFree( MDL mdl, MDLFFT fft, void *p ) {
    mdlGridFree(mdl,fft->rgrid,p);
    }

void mdlFFT( MDL mdl, MDLFFT fft, fftw_real *data, int bInverse ) {
    rfftwnd_mpi_plan plan = bInverse ? fft->iplan : fft->fplan;
    rfftwnd_mpi(plan,1,data,0,FFTW_TRANSPOSED_ORDER);
    }
#endif

void mdlSetWorkQueueSize(MDL mdl,int wqSize,int cudaSize) {
    }

/* Just do the work immediately */
void mdlAddWork(MDL mdl, void *ctx, mdlWorkFunction initWork, mdlWorkFunction checkWork, mdlWorkFunction doWork, mdlWorkFunction doneWork) {
    while( doWork(ctx) != 0 ) {}
    doneWork(ctx);
    }
