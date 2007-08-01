#ifndef PKD_HINCLUDED
#define PKD_HINCLUDED

#include <stdint.h>
#include <sys/resource.h>
#include "mdl.h"
#ifndef HAVE_CONFIG_H
#include "floattype.h"
#endif
#include "parameters.h"

#include "moments.h"
#include "cosmo.h"
#ifdef USE_HDF5
#include "iohdf5.h"
#endif
#ifdef PLANETS
#include "ssio.h"
#endif

/*
** The following sort of definition should really be in a global
** configuration header file -- someday...
*/

#define CID_PARTICLE	0
#define CID_CELL	1
#define CID_GROUP	2
#define CID_RM		3
#define CID_BIN		4
/*
** Here we define some special reserved nodes. Node-0 is a sentinel or null node, node-1
** is here defined as the ROOT of the local tree (or top tree), node-2 is unused and 
** node-3 is the root node for the very active tree.
*/
#define VAROOT          3
#define ROOT		1
#define NRESERVED_NODES 4
/*
** These macros implement the index manipulation tricks we use for moving
** around in the top-tree. Note: these do NOT apply to the local-tree!
*/
#define LOWER(i)	(i<<1)
#define UPPER(i)	((i<<1)+1)
#define PARENT(i)	(i>>1)
#define SIBLING(i) 	((i&1)?i-1:i+1)
#define SETNEXT(i)\
{\
	while (i&1) i=i>>1;\
	++i;\
	}

/*
** This is useful for debugging the very-active force calculation.
*/
#define A_VERY_ACTIVE  1


#define MAX_TIMERS		10


typedef struct pLite {
    FLOAT r[3];
    int i;
    uint8_t uRung;
    } PLITE;

typedef struct pIO {
    int64_t iOrder;
    FLOAT r[3];
    FLOAT v[3];
    FLOAT fMass;
    FLOAT fSoft;
} PIO;

typedef struct particle {
    int iOrder;
    unsigned int iActive;  
    int iRung;
    int iBucket;
    FLOAT fMass;
    FLOAT fSoft;
#ifdef CHANGESOFT
    FLOAT fSoft0;
#endif
    FLOAT r[3];
    FLOAT v[3];
    FLOAT a[3];
    FLOAT ae[3];
#ifdef HERMITE
    FLOAT ad[3];
    FLOAT r0[3];
    FLOAT v0[3];
    FLOAT a0[3];
    FLOAT ad0[3];
    FLOAT rp[3];
    FLOAT vp[3];
    FLOAT app[3];
    FLOAT adpp[3];
    FLOAT dTime0; 
#endif  /* Hermite */
    FLOAT fWeight;

    FLOAT fPot;
    FLOAT fBall;
    FLOAT fDensity;

    FLOAT dt;			/* a time step suggestion */
    FLOAT dtGrav;		/* suggested 1/dt^2 from gravity */

    int pGroup;
    int pBin;
    FLOAT fBallv2;
#ifdef RELAXATION
    FLOAT fRelax;
#endif

#ifdef PLANETS 
/* (collision stuff) */
    int iOrgIdx;		/* for tracking of mergers, aggregates etc. */
    FLOAT w[3];			/* spin vector */
    int iColor;			/* handy color tag */
    int iColflag;	        /* handy collision tag 1 for c1, 2 for c2*/
    int iOrderCol;              /* iOrder of colliding oponent.*/
    FLOAT dtCol;
/* end (collision stuff) */
#ifdef SYMBA
    FLOAT rb[3]; /* position before drift */
    FLOAT vb[3]; /* velocity before drift */
    FLOAT drmin; /* minimum distance from neighbors normalized by Hill*/
    FLOAT drmin2; /* min. dis. during drift */
    int   iOrder_VA[5]; /* iOrder's of particles within 3 hill radius*/
    int   i_VA[5];    /* pointers of particles */
    int   n_VA;       /* number of particles */
  double  hill_VA[5]; /* mutual hill radius calculated in grav.c */  
  double a_VA[3];          /* accralation due to close encounters */
  /* int   iKickRung; */
#endif
#endif/* PLANETS */
    } PARTICLE;


/* Active Type Masks */

/* Active: -- eg. Calculate new acceleration, PdV, etc... for this particle */
#define TYPE_ACTIVE            (1<<0)

/* Types used for Fast Density only (so far) */
/* Sum Fast Density on this particle */
#define TYPE_DensACTIVE        (1<<4)
/* Neighbour of ACTIVE (incl. ACTIVE): */
#define TYPE_NbrOfACTIVE       (1<<5)
/* Density set to zero already */
#define TYPE_DensZeroed        (1<<7)

/* Particle Type Masks */

#define TYPE_GAS               (1<<8)
#define TYPE_DARK              (1<<9)
#define TYPE_STAR              (1<<10)
#define TYPE_BLACKHOLE         (1<<11)

/* Particle marked for deletion.  Will be deleted in next
   msrAddDelParticles(); */
#define TYPE_DELETED           (1<<12)

/* A particle whose coordinates are output very frequently */
#define TYPE_TRACKER		   (1<<14)

/* Combination Masks */
#define TYPE_ALL				(TYPE_GAS|TYPE_DARK|TYPE_STAR|TYPE_BLACKHOLE)

/* Type Macros */
int TYPEQueryACTIVE      ( PARTICLE *a );
int TYPEQueryGAS         ( PARTICLE *a );
int TYPETest  ( PARTICLE *a, unsigned int mask );
int TYPEFilter( PARTICLE *a, unsigned int filter, unsigned int mask );
int TYPESet   ( PARTICLE *a, unsigned int mask );
int TYPEReset ( PARTICLE *a, unsigned int mask );
/* This retains Particle Type and clears all flags: */
int TYPEClearACTIVE( PARTICLE *a ); 

/* Warning: This erases Particle Type */
int TYPEClear( PARTICLE *a ); 

#define TYPEQueryACTIVE(a)       ((a)->iActive & TYPE_ACTIVE)
#define TYPEQueryGAS(a)			 ((a)->iActive & TYPE_GAS)
#define TYPETest(a,b)            ((a)->iActive & (b))
#define TYPEFilter(a,b,c)        (((a)->iActive & (b))==(c))
#define TYPESet(a,b)             ((a)->iActive |= (b))
#define TYPEReset(a,b)           ((a)->iActive &= (~(b)))
#define TYPEClearACTIVE(a)       ((a)->iActive &= TYPE_ALL)
#define TYPEClear(a)             ((a)->iActive = 0)

#define BND_COMBINE(b,b1,b2)\
{\
	int BND_COMBINE_j;\
	for (BND_COMBINE_j=0;BND_COMBINE_j<3;++BND_COMBINE_j) {\
		FLOAT BND_COMBINE_t1,BND_COMBINE_t2,BND_COMBINE_max,BND_COMBINE_min;\
		BND_COMBINE_t1 = (b1).fCenter[BND_COMBINE_j] + (b1).fMax[BND_COMBINE_j];\
		BND_COMBINE_t2 = (b2).fCenter[BND_COMBINE_j] + (b2).fMax[BND_COMBINE_j];\
		BND_COMBINE_max = (BND_COMBINE_t1 > BND_COMBINE_t2)?BND_COMBINE_t1:BND_COMBINE_t2;\
		BND_COMBINE_t1 = (b1).fCenter[BND_COMBINE_j] - (b1).fMax[BND_COMBINE_j];\
		BND_COMBINE_t2 = (b2).fCenter[BND_COMBINE_j] - (b2).fMax[BND_COMBINE_j];\
		BND_COMBINE_min = (BND_COMBINE_t1 < BND_COMBINE_t2)?BND_COMBINE_t1:BND_COMBINE_t2;\
		(b).fCenter[BND_COMBINE_j] = 0.5*(BND_COMBINE_max + BND_COMBINE_min);\
		(b).fMax[BND_COMBINE_j] = 0.5*(BND_COMBINE_max - BND_COMBINE_min);\
		}\
	}

typedef struct bndBound {
    FLOAT fCenter[3];
    FLOAT fMax[3];
    } BND;

#define MINDIST(bnd,pos,min2) {\
    double BND_dMin;\
    int BND_j;\
    (min2) = 0;					\
    for (BND_j=0;BND_j<3;++BND_j) {\
	BND_dMin = fabs((bnd).fCenter[BND_j] - (pos)[BND_j]) - (bnd).fMax[BND_j]; \
	if (BND_dMin > 0) (min2) += BND_dMin*BND_dMin;			\
	}\
    }

#define PARTITION(p,t,i,j,LOWER_CONDITION,UPPER_CONDITION) {\
    while (i <= j) {\
	if (LOWER_CONDITION) ++i;\
	else break;\
	}\
    while (i <= j) {\
	if (UPPER_CONDITION) --j;\
	else break;\
	}\
    if (i < j) {\
	t = p[i];\
	p[i] = p[j];\
	p[j] = t;\
	while (1) {\
	    ++i;\
	    while (LOWER_CONDITION) ++i;\
	    --j;\
	    while (UPPER_CONDITION) --j;\
	    if (i < j) {\
		t = p[i];\
		p[i] = p[j];\
		p[j] = t;\
		}\
	    else break;\
	    }\
	}\
    }


typedef struct kdTemp {
    BND bnd;
    int iLower;
    int iParent;
    int pLower;
    int pUpper;
    } KDT;


typedef struct kdNode {
    BND bnd;
    double dTimeStamp;
#ifdef LOCAL_EXPANSION
    FLOAT fOpen;
#else
    FLOAT fOpen2;
#endif
    FLOAT fSoft2;
    FLOAT r[3];
    FLOAT v[3];
    MOMR mom;
    int iLower;
    int iParent;
    int pLower;		/* also serves as thread id for the LTT */
    int pUpper;		/* pUpper < 0 indicates no particles in tree! */
    int iActive;
    uint8_t uMinRung;
    uint8_t uMaxRung;
    } KDN;

#define NMAX_OPENCALC	100

#define FOPEN_FACTOR	4.0/3.0

#ifdef LOCAL_EXPANSION
#define CALCOPEN(pkdn,diCrit2)\
{\
	FLOAT CALCOPEN_d2 = 0;\
	int CALCOPEN_j;\
	for (CALCOPEN_j=0;CALCOPEN_j<3;++CALCOPEN_j) {\
		FLOAT CALCOPEN_d = fabs((pkdn)->bnd.fCenter[CALCOPEN_j] - (pkdn)->r[CALCOPEN_j]) +\
			(pkdn)->bnd.fMax[CALCOPEN_j];\
		CALCOPEN_d2 += CALCOPEN_d*CALCOPEN_d;\
		}\
	(pkdn)->fOpen = sqrt(CALCOPEN_d2*(diCrit2));	\
	}
#else
#define CALCOPEN(pkdn,diCrit2)\
{\
	FLOAT CALCOPEN_d2 = 0;\
	int CALCOPEN_j;\
	for (CALCOPEN_j=0;CALCOPEN_j<3;++CALCOPEN_j) {\
		FLOAT CALCOPEN_d = fabs((pkdn)->bnd.fCenter[CALCOPEN_j] - (pkdn)->r[CALCOPEN_j]) +\
			(pkdn)->bnd.fMax[CALCOPEN_j];\
		CALCOPEN_d2 += CALCOPEN_d*CALCOPEN_d;\
		}\
	(pkdn)->fOpen2 = CALCOPEN_d2*(diCrit2);	\
	}
#endif

#define CALCAXR(fMax,axr)\
{\
	if (fMax[0] < fMax[1]) {\
		if (fMax[1] < fMax[2]) {\
			if (fMax[0] > 0) axr = fMax[2]/fMax[0];\
			else axr = 1e6;\
			}\
		else if (fMax[0] < fMax[2]) {\
			if (fMax[0] > 0) axr = fMax[1]/fMax[0];\
			else axr = 1e6;\
			}\
		else if (fMax[2] > 0) axr = fMax[1]/fMax[2];\
		else axr = 1e6;\
		}\
	else if (fMax[0] < fMax[2]) {\
		if (fMax[1] > 0) axr = fMax[2]/fMax[1];\
		else axr = 1e6;\
		}\
	else if (fMax[1] < fMax[2]) {\
		if (fMax[1] > 0) axr = fMax[0]/fMax[1];\
		else axr = 1e6;\
		}\
	else if (fMax[2] > 0) axr = fMax[0]/fMax[2];\
	else axr = 1e6;\
	}

/*
** components required for evaluating a monopole interaction
** including the softening.
*/

typedef struct ilPart {
#ifndef USE_SIMD
    int iOrder;
#endif
    double m,x,y,z;
#ifndef USE_SIMD
    double vx,vy,vz;
#endif
#ifdef SOFTLINEAR
    double h;
#endif
#ifdef SOFTSQUARE
    double twoh2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
    double fourh2;
#endif  
    } ILP;

/*
** components required for time-step calculation (particle-bucket list)
*/

typedef struct ilPartBucket {
    double m,x,y,z;
#ifdef SOFTLINEAR
    double h;
#endif
#ifdef SOFTSQUARE
    double twoh2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
    double fourh2;
#endif     
    } ILPB;

typedef struct RhoEncArray {
    int index;
    double x;
    double y;
    double z;
    double dir;
    double rhoenc;
    } RHOENC;

typedef struct RhoLocalArray {
    double d2;
    double m;
    } RHOLOCAL;

typedef struct heapStruct {
    int index;
    double rhoenc;
    } HEAPSTRUCT;

typedef struct ewaldTable {
    double hx,hy,hz;
    double hCfac,hSfac;
    } EWT;
/*
** components required for groupfinder:  --J.D.--
*/
typedef struct remoteMember{
    int iPid;
    int iIndex;
    } FOFRM;
typedef struct groupData{
    int iLocalId;
    int iGlobalId;
    FLOAT fAvgDens;
    FLOAT fVelDisp;
    FLOAT fVelSigma2[3];
    FLOAT fMass;
    FLOAT fGasMass;
    FLOAT fStarMass;
    FLOAT fRadius;
    FLOAT fDeltaR2;
    FLOAT r[3];
    FLOAT potmin;
    FLOAT rpotmin[3];
    FLOAT rmax[3];
    FLOAT rmin[3];
    FLOAT v[3];
    FLOAT vcircMax;
    FLOAT rvcircMax;
    FLOAT rvir;
    FLOAT Mvir;
    FLOAT lambda;
    FLOAT rhoBG;
/*    FLOAT rTidal; */
/*    FLOAT mTidal; */
    int nLocal;
    int nTotal;
    int bMyGroup;
    int nLSubGroups;
    void *lSubGroups;
    int nSubGroups;
    void *subGroups;
    int nRemoteMembers;
    int iFirstRm;
    } FOFGD;

typedef struct protoGroup{
    int iId;
    int nMembers;
    int nRemoteMembers;
    int iFirstRm;
    } FOFPG;

typedef struct groupBin{
    int iId;
    int nMembers;
    FLOAT fRadius;
    FLOAT fDensity;
    FLOAT fMassInBin;
    FLOAT fMassEnclosed;
    FLOAT com[3];
    FLOAT v2[3]; 
    FLOAT L[3];
    FLOAT a;
    FLOAT b;
    FLOAT c;
    FLOAT phi;
    FLOAT theta;
    FLOAT psi;
    } FOFBIN;

typedef struct pkdContext {
    MDL mdl;
    int idSelf;
    int nThreads;
    int nStore;
    int nRejects;
    int nLocal;
    int nVeryActive;
    int nActive;
    int nDark;
    int nGas;
    int nStar;
    int nMaxOrderDark;
    int nMaxOrderGas;
    FLOAT fPeriod[3];
    int nMaxNodes;   /* for kdTemp */
    KDT *kdTemp;
    KDN *kdTop;
    int iTopRoot;
    int nNodes;
    int nNodesFull;     /* number of nodes in the full tree (including very active particles) */
    int nMaxDepth;	/* gives the maximum depth of the local tree */
    int nNonVANodes;    /* number of nodes *not* in Very Active Tree, or index to the start of the VA nodes (except VAROOT) */
    KDN *kdNodes;
    PARTICLE *pStore;
    int nMaxBucketActive;
    PARTICLE **piActive;
    PARTICLE **piInactive;
    PLITE *pLite;

    /*
    ** New activation methods
    */
    uint8_t uMinRungActive;
    uint8_t uMaxRungActive;
    uint8_t uRungVeryActive;    /* NOTE: The first very active particle is at iRungVeryActive + 1 */

    /*
    ** Ewald summation setup.
    */
    MOMC momRoot;		/* we hope to get rid of this */
    int nMaxEwhLoop;
    int nEwhLoop;
    EWT *ewt;
    /*
    ** Timers stuff.
    */
    struct timer {
	double sec;
	double stamp;
	double system_sec;
	double system_stamp;
	double wallclock_sec;
	double wallclock_stamp;
	int iActive;
	} ti[MAX_TIMERS];
    int nGroups;
    FOFGD *groupData;
    int nRm;
    int nMaxRm;
    FOFRM *remoteMember;
    int nBins;

    FOFBIN *groupBin;
    /*
    ** Oh heck, just put all the parameters in here!
    ** This is set in pkdInitStep.
    */
    struct parameters param;
#ifdef PLANETS
    double dDeltaEcoll;
    double dSunMass;
    int    iCollisionflag; /*call pkddocollisionveryactive if iCollisionflag=1*/ 
#endif   
    } * PKD;

/* New, rung based ACTIVE/INACTIVE routines */
static inline int pkdRungVeryActive(PKD pkd) { return pkd->uRungVeryActive; }
static inline int pkdIsVeryActive(PKD pkd, PARTICLE *p) {
    return p->iRung > pkd->uRungVeryActive;
}

static inline int pkdIsRungActive(PKD pkd, uint8_t uRung ) {
    return uRung >= pkd->uMinRungActive && uRung <= pkd->uMaxRungActive;
}

static inline int pkdIsActive(PKD pkd, PARTICLE *p ) {
    return pkdIsRungActive(pkd,p->iRung);
}

static inline int pkdIsCellActive(PKD pkd, KDN *c) {
    return pkd->uMinRungActive <= c->uMaxRung && pkd->uMaxRungActive >= c->uMinRung;
}
#define CELL_ACTIVE(c,a,b) ((a)<=(c)->uMaxRung && (b)>=(c)->uMinRung)



typedef struct CacheStatistics {
    double dpNumAccess;
    double dpMissRatio;
    double dpCollRatio;
    double dpMinRatio;
    double dcNumAccess;
    double dcMissRatio;
    double dcCollRatio;
    double dcMinRatio;
    } CASTAT;


/*
** From tree.c:
*/
void pkdVATreeBuild(PKD pkd,int nBucket,FLOAT diCrit2,int bSqueeze,double dTimeStamp);
void pkdTreeBuild(PKD pkd,int nBucket,FLOAT dCrit,KDN *pkdn,int bSqueeze,int bExcludeVeryActive,double dTimeStamp);
void pkdCombineCells(KDN *pkdn,KDN *p1,KDN *p2,int bCombineBound);
void pkdDistribCells(PKD,int,KDN *);
void pkdCalcRoot(PKD,MOMC *);
void pkdDistribRoot(PKD,MOMC *);

#ifdef GASOLINE
void pkdCalcBoundBall(PKD pkd,double fBallFactor,BND *);
void pkdDistribBoundBall(PKD,int,BND *);
#endif

#include "parameters.h"
/*
** From pkd.c:
*/
double pkdGetTimer(PKD,int);
double pkdGetSystemTimer(PKD,int);
double pkdGetWallClockTimer(PKD,int);
void pkdClearTimer(PKD,int);
void pkdStartTimer(PKD,int);
void pkdStopTimer(PKD,int);
void pkdInitialize(PKD *,MDL,int,FLOAT *,int,int,int,double);
void pkdFinish(PKD);
void pkdReadTipsy(PKD,char *,char *,int,int,int,double,double,int);
void pkdSetSoft(PKD pkd,double dSoft);
void pkdCalcBound(PKD,BND *);

#ifdef CHANGESOFT
void pkdPhysicalSoft(PKD pkd,double dSoftMax,double dFac,int bSoftMaxMul);
void pkdPreVariableSoft(PKD pkd);
void pkdPostVariableSoft(PKD pkd,double dSoftMax,int bSoftMaxMul);
#endif

void pkdBucketWeight(PKD pkd,int iBucket,FLOAT fWeight);
void pkdGasWeight(PKD);
void pkdRungDDWeight(PKD, int, double);
int pkdWeight(PKD,int,FLOAT,int,int,int,int *,int *,FLOAT *,FLOAT *);
void pkdCountVA(PKD,int,FLOAT,int *,int *);
int pkdLowerPart(PKD,int,FLOAT,int,int);
int pkdUpperPart(PKD,int,FLOAT,int,int);
int pkdWeightWrap(PKD,int,FLOAT,FLOAT,int,int,int,int,int *,int *);
int pkdLowerPartWrap(PKD,int,FLOAT,FLOAT,int,int,int);
int pkdUpperPartWrap(PKD,int,FLOAT,FLOAT,int,int,int);
int pkdLowerOrdPart(PKD,int,int,int);
int pkdUpperOrdPart(PKD,int,int,int);
int pkdActiveOrder(PKD);

int pkdColRejects(PKD,int);
int pkdColRejects_Old(PKD,int,FLOAT,FLOAT,int);

int pkdSwapRejects(PKD,int);
int pkdSwapSpace(PKD);
int pkdFreeStore(PKD);
int pkdLocal(PKD);
int pkdActive(PKD);
int pkdInactive(PKD);
int pkdNodes(PKD);
int pkdColOrdRejects(PKD,int,int);
void pkdLocalOrder(PKD);
void pkdWriteTipsy(PKD,char *,int,int,double,double,int);
#ifdef USE_HDF5
void pkdWriteHDF5(PKD pkd, IOHDF5 io,double dvFac,double duTFac);
#endif
void pkdGravAll(PKD,double,int,int,int,int,int,double,double,int *,
		double *,double *,CASTAT *,double *);
void pkdCalcE(PKD,double *,double *,double *);
void pkdCalcEandL(PKD,double *,double *,double *,double []);
void pkdDrift(PKD,double,double,FLOAT *,int,int,FLOAT);
void pkdDriftInactive(PKD pkd,double dTime,double dDelta,FLOAT fCenter[3],int bPeriodic,
		      int bFandG, FLOAT fCentMass);
void pkdStepVeryActiveKDK(PKD pkd, double dStep, double dTime, double dDelta,
			  int iRung, int iKickRung, int iRungVeryActive,int iAdjust,
			  double diCrit2,int *pnMaxRung,
			  double aSunInact[3], double adSunInact[3], double dSunMass);
#ifdef HERMITE
void
pkdStepVeryActiveHermite(PKD pkd, double dStep, double dTime, double dDelta,
		     int iRung, int iKickRung, int iRungVeryActive,int iAdjust, double diCrit2,
			 int *pnMaxRung, double aSunInact[], double adSunInact[], double dSunMass);
void pkdCopy0(PKD pkd,double dTime);
void pkdPredictor(PKD pkd,double dTime); 
void pkdCorrector(PKD pkd,double dTime);
void pkdSunCorrector(PKD pkd,double dTime,double dSunMass); 
void pkdPredictorInactive(PKD pkd,double dTime);
void pkdAarsethStep(PKD pkd, double dEta);
void pkdFirstDt(PKD pkd);
#endif /* Hermite */
void pkdKickKDKOpen(PKD pkd,double dTime,double dDelta);
void pkdKickKDKClose(PKD pkd,double dTime,double dDelta);
void pkdKick(PKD pkd,double,double, double, double, double, double, int, double, double);
void pkdEwaldKick(PKD pkd, double dvFacOne, double dvFacTwo);
void pkdSwapAll(PKD pkd, int idSwap);
void pkdInitStep(PKD pkd,struct parameters *p,CSM csm);
void pkdSetRung(PKD pkd, int iRung);
void pkdBallMax(PKD pkd, int iRung, int bGreater, double ddHonHLimit);
void pkdActiveRung(PKD pkd, int iRung, int bGreater);
int pkdCurrRung(PKD pkd, int iRung);
void pkdGravStep(PKD pkd, double dEta, double dRhoFac);
void pkdAccelStep(PKD pkd, double dEta, double dVelFac, double
		  dAccFac, int bDoGravity, int bEpsAcc, int bSqrtPhi, double dhMinOverSoft);
void pkdDensityStep(PKD pkd, double dEta, double dRhoFac);
int pkdDtToRung(PKD pkd,int iRung, double dDelta, int iMaxRung, int bAll, int *nRungCount);
void pkdInitDt(PKD pkd, double dDelta);
int pkdOrdWeight(PKD,int,int,int,int,int *,int *);
void pkdDeleteParticle(PKD pkd, PARTICLE *p);
void pkdNewParticle(PKD pkd, PARTICLE *p);
int pkdResetTouchRung(PKD pkd, unsigned int iTestMask, unsigned int iSetMask);
void pkdSetRungVeryActive(PKD pkd, int iRung );
int pkdIsGas(PKD,PARTICLE *);
int pkdIsDark(PKD,PARTICLE *);
int pkdIsStar(PKD,PARTICLE *);
void pkdSetParticleTypes(PKD pkd);
void pkdColNParts(PKD pkd, int *pnNew, int *nDeltaGas, int *nDeltaDark,
		  int *nDeltaStar);
void pkdNewOrder(PKD pkd, int nStart);
void pkdSetNParts(PKD pkd, int nGas, int nDark, int nStar, int nMaxOrderGas,
		  int nMaxOrderDark, double dSunMass);
#ifdef RELAXATION
void pkdInitRelaxation(PKD pkd);
#endif

int pkdPackIO(PKD pkd,
	      PIO *io, int nMax,
	      int *iIndex,
	      int iMinOrder, int iMaxOrder );

#ifdef PLANETS
void pkdSunIndirect(PKD pkd,double aSun[],double adSun[],int iFlag);
void pkdGravSun(PKD pkd,double aSun[],double adSun[],double dSunMass);
void pkdReadSS(PKD pkd,char *pszFileName,int nStart,int nLocal);
void pkdWriteSS(PKD pkd,char *pszFileName,int nStart);

#ifdef SYMBA
#define symfac 1.925925925925926 /* 2.08/1.08 */
#define rsym2  1.442307692307692 /* 3.0/2.08 */

void
pkdStepVeryActiveSymba(PKD pkd, double dStep, double dTime, double dDelta,
		       int iRung, int iKickRung, int iRungVeryActive,
		       int iAdjust, double diCrit2,
		       int *pnMaxRung, double dSunMass, int);
int pkdSortVA(PKD pkd, int iRung);
void pkdGravVA(PKD pkd, int iRung);
int pkdCheckDrminVA(PKD pkd, int iRung, int multiflag, int nMaxRung);
void pkdKickVA(PKD pkd, double dt);
int pkdDrminToRung(PKD pkd, int iRung, int iMaxRung, int *nRungCount);
int pkdGetPointa(PKD pkd);
int pkdDrminToRungVA(PKD pkd, int iRung, int iMaxRung, int multiflag);
void pkdMomSun(PKD pkd,double momSun[]);
void pkdDriftSun(PKD pkd,double vSun[],double dt);
void pkdKeplerDrift(PKD pkd,double dt,double mu,int tag_VA);



#endif /* SYMBA */
#endif /* PLANETS*/

#endif
