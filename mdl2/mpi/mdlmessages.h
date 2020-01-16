#ifndef MDLMESSAGES_H
#define MDLMESSAGES_H
#include <stdint.h>
#include "mdl_config.h"
#include "mpi.h"
#include "mdlfft.h"
#include "opa_queue.h"
#include <vector>

enum class CacheMessageType : uint8_t {
    UNKNOWN = 0,
    REQUEST = 1,
    REPLY = 2,
    FLUSH = 3,
    };

#define MDL_FLUSH_DATA_SIZE	32000

struct ServiceHeader {
    int32_t idFrom;      /* Global thread ID */
    int16_t replyTag;    /* Which MPI tag to send the reply */
    int16_t sid;
    int32_t nInBytes;
    int32_t nOutBytes;
    };

struct CacheHeader {
    uint8_t cid;
    CacheMessageType mid;
    uint16_t nItems;
    int32_t idFrom;
    int32_t idTo;
    int32_t iLine;
    };

class FlushBuffer {
protected:
    uint32_t iRankTo;
    uint32_t nBuffer;
    CacheMessageType mid;
    std::vector<char> Buffer;
public:
    explicit FlushBuffer(uint32_t nSize=MDL_FLUSH_DATA_SIZE,CacheMessageType mid=CacheMessageType::FLUSH);
    char *getBuffer() {return &Buffer.front();}
    uint32_t getCount() {return nBuffer;}
    uint32_t getRankTo() {return iRankTo;}
    bool isEmpty() {return nBuffer==0;}
    void emptyBuffer() {nBuffer=0;}
    void setRankTo(uint32_t iRank) { iRankTo=iRank; }
    bool canBuffer(int nSize) { return nBuffer+nSize+sizeof(ServiceHeader) <= MDL_FLUSH_DATA_SIZE; }
    bool addBuffer(int nSize, char *pData=0);
    bool addBuffer(uint8_t cid, int32_t idFrom, int32_t idTo, int32_t iLine, int nSize=0, const char *pData=0);
    bool addBuffer(int nSize, const CacheHeader *pData);
    };

class mdlMessage {
    friend class mdlMessageQueue;
    OPA_Queue_element_hdr_t hdr;
    struct mdlMessageQueue *replyQueue;
public:
    mdlMessage();
    void sendBack();
    virtual void action(class mpiClass *mpi) {sendBack();}
    virtual void result(class mdlClass *mdl);
    };

struct mdlMessageQueue : public OPA_Queue_info_t {
    mdlMessageQueue();
    mdlMessage &wait();
    void enqueue(mdlMessage *M) { OPA_Queue_enqueue(this,  M, mdlMessage, hdr); }
    void enqueue(mdlMessage &M) { OPA_Queue_enqueue(this, &M, mdlMessage, hdr); }
    void enqueue(const mdlMessage &M,mdlMessageQueue &Q, bool bWait=false);
    void enqueueAndWait(const mdlMessage &M);
    mdlMessage &dequeue() { mdlMessage *M; OPA_Queue_dequeue(this, M, mdlMessage, hdr); return * M; }
    bool empty() {return OPA_Queue_is_empty(this);}
    };

// Used to hold a sequence of cache lines to send to the MPI thread for processing
class mdlMessageFlushFromCore : public mdlMessage, public FlushBuffer {
public:
    virtual void action(class mpiClass *mdl);
    };

// Used to hold a sequence of cache lines to send from the MPI thread to cores
class mdlMessageFlushToCore : public mdlMessage, public FlushBuffer {
public:
    virtual void result(class mdlClass *mdl);
    };

class mdlMessageSTOP : public mdlMessage {
public:
    virtual void action(class mpiClass *mdl);
    };

class mdlMessageCacheOpen : public mdlMessage {
public:
    virtual void action(class mpiClass *mdl);
    };

class mdlMessageCacheClose : public mdlMessage {
public:
    virtual void action(class mpiClass *mdl);
    };

class mdlMessageCacheFlushOut : public mdlMessage {
public:
    virtual void action(class mpiClass *mdl);
    };

class mdlMessageCacheFlushLocal : public mdlMessage {
public:
    virtual void action(class mpiClass *mdl);
    };

class mdlMessageGridShare : public mdlMessage {
    friend class mdlClass;
    friend class mpiClass;
protected:
    MDLGRID grid;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageGridShare(MDLGRID grid);
    };

class mdlMessageDFT_R2C : public mdlMessage {
    friend class mdlClass;
    friend class mpiClass;
protected:
    MDLFFT fft;
    FFTW3(real) *data;
    FFTW3(complex) *kdata;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageDFT_R2C(MDLFFT fft, FFTW3(real) *data, FFTW3(complex) *kdata);
    };

class mdlMessageDFT_C2R : public mdlMessage {
    friend class mdlClass;
    friend class mpiClass;
protected:
    MDLFFT fft;
    FFTW3(real) *data;
    FFTW3(complex) *kdata;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageDFT_C2R(MDLFFT fft, FFTW3(real) *data, FFTW3(complex) *kdata);
    };

class mdlMessageFFT_Sizes : public mdlMessage {
    friend class mdlClass;
    friend class mpiClass;
protected: // Input fields
    int n1,n2,n3;
protected: // Output fields
    ptrdiff_t nz, sz, ny, sy, nLocal;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageFFT_Sizes(int n1, int n2, int n3);
    };

class mdlMessageFFT_Plans : public mdlMessageFFT_Sizes {
    friend class mdlClass;
    friend class mpiClass;
protected: // Input fields
    FFTW3(real) *data;
    FFTW3(complex) *kdata;
protected: // Output fields
    FFTW3(plan) fplan, iplan;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageFFT_Plans(int n1, int n2, int n3,FFTW3(real) *data=0,FFTW3(complex) *kdata=0);
    };

class mdlMessageMPI : public mdlMessage {
protected:
    friend class mdlClass;
    friend class mpiClass;
public:
    virtual void action(class mpiClass *mdl) = 0;
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    };

// Used to hold a sequence of cache lines to send from the MPI thread to cores
class mdlMessageFlushToRank : public mdlMessageMPI, public FlushBuffer {
protected:
    friend class mdlClass;
    friend class mpiClass;
public:
    virtual void action(class mpiClass *mdl);
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    };

// Send a small reply message with a single cache line
class mdlMessageCacheReply : public mdlMessageMPI, public FlushBuffer {
protected:
    friend class mdlClass;
    friend class mpiClass;
public:
    mdlMessageCacheReply(uint32_t nSize) : FlushBuffer(nSize,CacheMessageType::REPLY) {}
    virtual void action(class mpiClass *mdl);
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    };

class mdlMessageCacheReceive : public mdlMessageMPI, public FlushBuffer {
protected:
    friend class mdlClass;
    friend class mpiClass;
public:
    mdlMessageCacheReceive(uint32_t nSize) : FlushBuffer(nSize,CacheMessageType::UNKNOWN) {}
    virtual void action(class mpiClass *mdl);
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    };

class mdlMessageAlltoallv : public mdlMessageMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    int dataSize;
    void *sbuff, *rbuff;
    int *scount, *sdisps, *rcount, *rdisps;
public:
    virtual void action(class mpiClass *mdl);
    mdlMessageAlltoallv(int dataSize,void *sbuff,int *scount,int *sdisps,void *rbuff,int *rcount,int *rdisps);
    };

class mdlMessageBarrierMPI : public mdlMessageMPI {
public:
    virtual void action(class mpiClass *mdl);
    };

class mdlMessageBufferedMPI : public mdlMessageMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    void *buf;
    int count;
    int target;
    int tag;
    MPI_Datatype datatype;
public:
    virtual void action(class mpiClass *mdl) = 0;
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    explicit mdlMessageBufferedMPI(void *buf, int count, MPI_Datatype datatype, int target, int tag);
    int getCount() {return count;}
    };

class mdlMessageSend : public mdlMessageBufferedMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageSend(void *buf,int32_t count,MPI_Datatype datatype, int source, int tag);
    };

class mdlMessageReceive : public mdlMessageBufferedMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    int iCoreFrom;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageReceive(void *buf,int32_t count,MPI_Datatype datatype, int source, int tag,int iCoreFrom);
    };

class mdlMessageReceiveReply : public mdlMessageReceive {
protected:
    friend class mdlClass;
    friend class mpiClass;
    ServiceHeader header;
public:
    virtual void action(class mpiClass *mdl);
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    explicit mdlMessageReceiveReply(void *buf,int32_t count, int rID, int iCoreFrom);
    };

class mdlMessageReceiveRequest : public mdlMessageMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    ServiceHeader header;
    std::vector<char> Buffer;
public:
//    virtual void action(class mpiClass *mdl);
    explicit mdlMessageReceiveRequest(int32_t count=0);
    };

class mdlMessageSendRequest : public mdlMessageBufferedMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    ServiceHeader header;
public:
    virtual void action(class mpiClass *mdl);
    explicit mdlMessageSendRequest(int32_t idFrom,int16_t sid,int target,void *buf=0,int32_t count=0);
    };

class mdlMessageSendReply : public mdlMessageMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    ServiceHeader header;
    std::vector<char> Buffer;
    int iThreadTo;
public:
    virtual void action(class mpiClass *mdl);
//    explicit mdlMessageSendReply(int32_t idFrom,int16_t replyTag, int16_t sid,int target,void *buf=0,int32_t count=0);
    explicit mdlMessageSendReply(int32_t count=0);
    mdlMessageSendReply & makeReply(int32_t idFrom,int16_t replyTag,int16_t sid,int target,int32_t count);
    };

class mdlMessageCacheRequest : public mdlMessageBufferedMPI {
protected:
    friend class mdlClass;
    friend class mpiClass;
    CacheHeader header;
    void *pLine;
public:
    virtual void action(class mpiClass *mdl);
    virtual void finish(class mpiClass *mdl, const MPI_Status &status);
    explicit mdlMessageCacheRequest(uint8_t cid, int32_t idFrom);
    explicit mdlMessageCacheRequest(uint8_t cid, int32_t idFrom, uint16_t nItems, int32_t idTo, int32_t iLine, void *pLine);
    mdlMessageCacheRequest & makeCacheRequest(uint16_t nItems, int32_t idTo, int32_t iLine, void *pLine);
    };

#endif