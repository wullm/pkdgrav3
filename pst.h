/*  This file is part of PKDGRAV3 (http://www.pkdgrav.org/).
 *  Copyright (c) 2001-2018 Joachim Stadel & Douglas Potter
 *
 *  PKDGRAV3 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  PKDGRAV3 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with PKDGRAV3.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PST_HINCLUDED
#define PST_HINCLUDED

#include "pkd.h"
#include "mdl.h"
#include "smooth/smoothfcn.h"
#include "gravity/moments.h"
#include "io/outtype.h"
#include "io/output.h"

#include "parameters.h"
#include "cosmo.h"
#include "ic/ic.h"

#define pstOffNode(pst) ((pst)->nLeaves > mdlCores((pst)->mdl))
#define pstOnNode(pst) ((pst)->nLeaves <= mdlCores((pst)->mdl))
#define pstAmNode(pst) ((pst)->nLeaves == mdlCores((pst)->mdl))
#define pstNotNode(pst) ((pst)->nLeaves != mdlCores((pst)->mdl))
#define pstAmCore(pst) ((pst)->nLeaves == 1)
#define pstNotCore(pst) ((pst)->nLeaves > 1)

typedef struct lclBlock {
    PKD	pkd;
    int iWtFrom;
    int iWtTo;
    int iPart;
    uint64_t iOrdSplit;
    double fSplit;
    double fWtLow;
    double fWtHigh;
    double fLow;
    double fHigh;
    int nSplit;
    uint64_t nWriteStart;
    uint64_t nGas;
    uint64_t nDark;
    uint64_t nStar;
    PKDOUT pkdout;
    } LCL;

typedef struct pstContext {
    struct pstContext *pstLower;
    MDL mdl;
    LCL *plcl;
    int idSelf;
    int idUpper;
    int nLeaves;
    int nLower;
    int nUpper;
    int iLvl;
    BND bnd;
    int iSplitDim;
    uint64_t iOrdSplit;
    double fSplit;
    double fSplitInactive;
    uint64_t nTotal;
    uint64_t nGas;
    uint64_t nDark;
    uint64_t nStar;
    } * PST;


#define PST_SERVICES		100
#define PST_FILENAME_SIZE	512
#define PST_MAX_FILES           16384

enum pst_service {
    PST_SRV_STOP=0, /* service 0 is always STOP and handled by MDL */
    PST_SETADD,
    PST_READFILE,
    PST_DOMAINDECOMP,
    PST_CALCBOUND,
    PST_COMBINEBOUND,
    PST_WEIGHT,
    PST_WEIGHTWRAP,
    PST_FREESTORE,
    PST_COLREJECTS,
    PST_SWAPREJECTS,
    PST_COLORDREJECTS,
    PST_DOMAINORDER,
    PST_LOCALORDER,
    PST_COMPRESSASCII,
    PST_SENDPARTICLES,
    PST_SENDARRAY,
    PST_WRITEASCII,
    PST_WRITE,
    PST_OUTPUT,
    PST_OUTPUT_SEND,
    PST_CHECKPOINT,
    PST_RESTORE,
    PST_BUILDTREE,
    PST_DISTRIBTOPTREE,
    PST_DUMPTREES,
    PST_TREEINITMARKED,
    PST_CALCROOT,
    PST_DISTRIBROOT,
    PST_ENFORCEPERIODIC,
    PST_SMOOTH,
#ifdef FAST_GAS
    PST_FASTGASPHASE1,
    PST_FASTGASPHASE2,
    PST_FASTGASCLEANUP,
#endif
    PST_GRAVITY,
    PST_GRAVEXTERNAL,
    PST_LIGHTCONE,
    PST_CALCEANDL,
    PST_CALCEANDLEXT,
    PST_DRIFT,
    PST_DRIFTINACTIVE,
    PST_CACHEBARRIER,
    PST_ROPARTICLECACHE,
    PST_PARTICLECACHEFINISH,
    PST_COPY0,
    PST_PREDICTOR,
    PST_CORRECTOR,
    PST_SUNCORRECTOR,
    PST_PREDICTORINACTIVE,
    PST_KICK,
    PST_KICKTREE,
    PST_SETSOFT,
    PST_PHYSICALSOFT,
    PST_SETTOTAL,
    PST_ONENODEREADINIT,
    PST_SWAPALL,
    PST_MASSCHECK,
    PST_ACTIVEORDER,
    PST_INITCOSMOLOGY,
    PST_ZERONEWRUNG,
    PST_ACTIVERUNG,
    PST_COUNTRUNGS,
    PST_GRAVSTEP,
    PST_ACCELSTEP,
    PST_SPHSTEP,
    PST_STARFORM,
    PST_DENSITYSTEP,
    PST_CORRECTENERGY,
    PST_MARKSMOOTH,
    PST_RESMOOTH,
    PST_INITACCEL,
    PST_UPDATERUNG,
    PST_INITDT,
    PST_ORDWEIGHT,
    PST_SETWRITESTART,
    PST_ADDWRITESTART,
    PST_COLNPARTS,
    PST_NEWORDER,
    PST_GETNPARTS,
    PST_SETNPARTS,
    PST_DENSCHECK,
    PST_NEW_FOF,
    PST_FOF_PHASES,
    PST_FOF_FINISH_UP,
    PST_FOFMERGE,
    PST_HOP_LINK,
    PST_HOP_JOIN,
    PST_HOP_FINISH_UP,
    PST_HOP_TREE_BUILD,
    PST_HOP_GRAVITY,
    PST_HOP_UNBIND,
    PST_GROUP_RELOCATE,
    PST_GROUP_STATS,
    PST_GROUP_STATS1,
    PST_SHRINK_PHASES,
    PST_GROUP_STATS2,
    PST_GROUPPROFILES,
    PST_INITRELAXATION,
    PST_INITIALIZEPSTORE,
    PST_GETFFTMAXSIZES,
    PST_GENERATEIC,
    PLT_GENERATEIC,
    PST_MOVEIC,
    PLT_MOVEIC,
    PST_HOSTNAME,
    PST_MEMSTATUS,
    PST_GETCLASSES,
    PST_SETCLASSES,
    PST_SWAPCLASSES,
    PST_COUNTSELECTED,
    PST_SELSPECIES,
    PST_SELMASS,
    PST_SELBYID,
    PST_SELPHASEDENSITY,
    PST_SELGROUP,
    PST_SELBOX,
    PST_SELSPHERE,
    PST_SELCYLINDER,
    PST_SELBLACKHOLES,

    PST_PROFILE,
    PST_CALCDISTANCE,
    PST_CALCCOM,
    PST_COUNTDISTANCE,
#ifdef MDL_FFTW
    PST_GRID_CREATE_FFT,
    PST_GRID_DELETE_FFT,
    PST_ADD_LINEAR_SIGNAL,
    PST_MEASURELINPK,
    PST_SETLINGRID,
    PST_LINEARKICK,
#endif
    PST_ASSIGN_MASS,
    PST_DENSITY_CONTRAST,
    PST_WINDOW_CORRECTION,
    PST_INTERLACE,
    PST_GRID_BIN_K,
    PST_BISPECTRUM_SELECT,
    PST_BISPECTRUM_CALCULATE,
    PST_TOTALMASS,
    PST_COUNTSPECIES,
    PST_SETWRITESPECIESCOUNTS,
    PST_LIGHTCONE_OPEN,
    PST_LIGHTCONE_CLOSE,
    PST_LIGHTCONEVEL,
    PST_GET_PARTICLES,
    };

#ifdef __cplusplus
extern "C" {
#endif

void pstAddServices(PST,MDL);
void pstInitialize(PST *,MDL,LCL *);
void pstFinish(PST);

/* PST_INITIALIZEPSTORE */
struct inInitializePStore {
    uint64_t mMemoryModel;
    uint64_t nStore;
    uint64_t nSpecies[FIO_SPECIES_LAST];
    double fPeriod[3];
    uint64_t nMinEphemeral;
    uint64_t nMinTotalStore;
    int nEphemeralBytes;
    int nTreeBitsLo;
    int nTreeBitsHi;
    int iCacheSize;
    int iWorkQueueSize;
    int iCUDAQueueSize;
    int bLightCone;
    int bLightConeParticles;
    };
int pstInitializePStore(PST,void *,int,void *,int);


/* PST_READFILE */
struct inReadFile {
    uint64_t nNodeStart; /* First particle to read (of total) */
    uint64_t nNodeEnd;   /* Last particle to read (of total) */
    double dvFac;
    double dTuFac;
    double dOmega0;
    double dOmegab;
    int nProcessors;
    /*char achFilename[PST_FILENAME_SIZE];*/
    };
typedef char inReadFileFilename[PST_FILENAME_SIZE];
int pstReadFile(PST,void *,int,void *,int);

/* PST_COMPRESSASCII */
struct inCompressASCII {
    uint64_t nTotal;
    int iFile;
    int iType;
    int iDim;
    };
struct outCompressASCII {
    uint64_t nBytes;
    };
int pstCompressASCII(PST,void *,int,void *,int);

/* PST_WRITEASCII */
struct inWriteASCII {
    uint64_t nFileOffset;
    char achOutFile[PST_FILENAME_SIZE];
    };
int pstWriteASCII(PST,void *,int,void *,int);

/* PST_RESTORE */
struct inRestore {
    int nProcessors;
    char achInFile[PST_FILENAME_SIZE];
    };
int pstRestore(PST,void *,int,void *,int);

/* PST_WRITE */
struct inWrite {
    BND bnd;
    double dTime;
    double dEcosmo;
    double dTimeOld;
    double dUOld;
    double dvFac;
    double dTuFac;
    double dBoxSize;
    double Omega0;
    double OmegaLambda;
    double HubbleParam;
    uint64_t nSph;
    uint64_t nDark;
    uint64_t nStar;
    int bStandard;
    int iIndex;
    int nProcessors;
    int iLower, iUpper;
    int bHDF5;
    int mFlags;
    char achOutFile[PST_FILENAME_SIZE];
    };
int pstWrite(PST,void *,int,void *,int);

/* PST_SENDPARTICLES */
int pstSendParticles(PST,void *,int,void *,int);

/* PST_SENDARRAY */
struct inSendArray {
    double dvFac;
    int iTo;
    int field;
    int iUnitSize;
    int bMarked;
    };
int pstSendArray(PST,void *,int,void *,int);

/* PST_CHECKPOINT */
int pstCheckpoint(PST,void *,int,void *,int);

struct inOutput {
    int iProcessor;   /* Output number: 0 to nParaWrite */
    int nProcessor;   /* Number of processors left in parallel */
    int iPartner;     /* Who to send the data to */
    int nPartner;     /* How many partners there are */
    outType eOutputType;  /* What kind of output */
    int iGrid;        /* Which grid (for grid output) */
    char achOutFile[PST_FILENAME_SIZE];
    };

/* PST_OUTPUT */
int pstOutput(PST,void *,int,void *,int);

/* PST_OUTPUT_SEND */
int pstOutputSend(PST,void *,int,void *,int);

/* PST_BUILDTREE */
struct inBuildTree {
    int nBucket;      /* Bucket Size */
    int nGroup;       /* Group Size */
    uint32_t uRoot;   /* Which root node to use */
    uint32_t utRoot;  /* Template tree */
    double ddHonHLimit;
    };
int pstBuildTree(PST,void *,int,void *,int);

/* PST_TREEINITMARKED */
int pstTreeInitMarked(PST,void *,int,void *,int);

/* PST_HOP_LINK */
struct inHopLink {
    double dHopTau;
    int nSmooth;
    int bPeriodic;
    int bSymmetric;
    int iSmoothType;
    SMF smf;
    };
int pstHopLink(PST,void *,int,void *,int);

/* PST_HOP_JOIN */
struct outHopJoin {
    uint64_t nGroups;
    int bDone;
    };
int pstHopJoin(PST,void *,int,void *,int);

/* PST_HOP_FINISH_UP */
struct inHopFinishUp{
    double fPeriod[3];
    int bPeriodic;
    int nMinGroupSize;
    };
int pstHopFinishUp(PST,void *,int,void *,int);

/* PST_HOP_TREE_BUILD */
/* PST_BUILDTREE */
struct inHopTreeBuild {
    int nBucket;
    int nGroup;
    };
int pstHopTreeBuild(PST,void *,int,void *,int);

/* PST_HOP_GRAVITY */
struct inHopGravity {
    double dTime;
    double dEwCut;
    double dEwhCut;
    double dTheta;
    int bPeriodic;
    int nGroup;
    uint8_t uRungLo;
    uint8_t uRungHi;
    };
int pstHopGravity(PST,void *,int,void *,int);

/* PST_HOP_UNBIND */
struct inHopUnbind {
    double dTime;
    double fPeriod[3];
    int bPeriodic;
    int nMinGroupSize;
    int iIteration;
    };
struct outHopUnbind {
    uint64_t nEvaporated;
    uint64_t nGroups;
    };
int pstHopUnbind(PST,void *,int,void *,int);

/* PST_GROUP_RELOCATE */
int pstGroupRelocate(PST,void *,int,void *,int);

/* PST_GROUP_STATS */
struct inGroupStats {
    int bPeriodic;
    double dPeriod[3];
    double rEnvironment[2];
    };
int pstGroupStats(PST,void *,int,void *,int);

/* PST_GROUP_STATS1 */
struct inGroupStats1 {
    int bPeriodic;
    double dPeriod[3];
    double rEnvironment[2];
    };
void pstGroupStats1(PST,void *,int,void *,int *);

/* PST_SHRINK_PHASES */
struct outShrinkPhases {
    int bdone;
    };   
void pstShrinkPhases(PST,void *,int,void *,int *);

/* PST_GROUP_STATS2 */
struct inGroupStats2 {
    int bPeriodic;
    double dPeriod[3];
    double rEnvironment[2];
    };
void pstGroupStats2(PST,void *,int,void *,int *);

/* PST_SMOOTH */
struct inSmooth {
    int nSmooth;
    int bPeriodic;
    int bSymmetric;
    int iSmoothType;
    SMF smf;
    };
int pstSmooth(PST,void *,int,void *,int);

/* PST_RESMOOTH */
int pstReSmooth(PST,void *,int,void *,int);

#ifdef FAST_GAS
/* PST_FASTGASPHASE1 */
int pstFastGasPhase1(PST,void *,int,void *,int);

/* PST_FASTGASPHASE2 */
int pstFastGasPhase2(PST,void *,int,void *,int);

/* PST_FASTGASCLEANUP */
int pstFastGasCleanup(PST,void *,int,void *,int);
#endif

/* PST_GRAVITY */
struct inGravity {
    double dTime;
    double dEwCut;
    double dEwhCut;
    double dTheta;
    int nReps;
    int bPeriodic;
    int bEwald;
    int nGroup;
    int iRoot1;
    int iRoot2;
    struct pkdKickParameters kick;
    struct pkdLightconeParameters lc;
    struct pkdTimestepParameters ts;
    };


typedef struct StatsCollector {
    double dSum;
    double dSum2;
    double dMax;
    int idMax;
    int n;
    } STAT;

/*
** The outGravityReduct structure is at the beginning of the output message, 
** followed by number of threads times the outGravityPerProc structure.
*/
struct outGravityReduct {
    STAT sLocal;
    STAT sActive;
    STAT sPart;
    STAT sPartNumAccess;
    STAT sPartMissRatio;
    STAT sCell;
    STAT sCellNumAccess;
    STAT sCellMissRatio;
    STAT sFlop;
#ifdef INSTRUMENT
    STAT sComputing;
    STAT sWaiting;
    STAT sSynchronizing;
#endif
#ifdef __linux__
    STAT sRSS;
    STAT sFreeMemory;
#endif
    double dFlopSingleCPU;
    double dFlopDoubleCPU;
    double dFlopSingleGPU;
    double dFlopDoubleGPU;
    uint64_t nActive;
    uint64_t nRung[IRUNGMAX+1];
    };
int pstGravity(PST,void *,int,void *,int);

/* PST_CALCEANDL */
struct outCalcEandL {
    double T;
    double U;
    double Eth;
    double L[3];
    double F[3];
    double W;
    };
int pstCalcEandL(PST,void *,int,void *,int);

/* PST_DRIFT */
struct inDrift {
    double dTime;
    double dDelta;
    double dDeltaVPred;
    double dDeltaUPred;
    int iRoot;
    int bDoGas;
    };
int pstDrift(PST,void *,int,void *,int);

/* PST_ROPARTICLECACHE */

int pstROParticleCache(PST, void *, int, void *, int);

/* PST_PARTICLECACHEFINISH */

int pstParticleCacheFinish(PST, void *, int, void *, int);

/* PST_CACHEBARRIER */
int pstCacheBarrier(PST, void *, int, void *, int);

/* PST_KICK */
struct inKick {
    double dTime;
    double dDelta;
    double dDeltaVPred;
    double dDeltaU;
    double dDeltaUPred;
    int    bDoGas;
    uint8_t uRungLo;
    uint8_t uRungHi;
    };
int pstKick(PST,void *,int,void *,int);

/* PST_KICKTREE */
struct inKickTree {
    double dTime;
    double dDelta;
    double dDeltaVPred;
    double dDeltaU;
    double dDeltaUPred;
    int iRoot;
    };
int pstKickTree(PST,void *,int,void *,int);

/* PST_PHYSICALSOFT */
struct inPhysicalSoft {
    double dSoftMax;
    double dFac;
    int bSoftMaxMul;
    };
int pstPhysicalSoft(PST,void *,int,void *,int);

/* PST_SETTOTAL */
struct outSetTotal {
    uint64_t nTotal;
    };
int pstSetTotal(PST,void *,int,void *,int);

/* PST_ONENODEREADINIT */
int pstOneNodeReadInit(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_ACTIVEORDER */
int pstActiveOrder(PST,void *,int,void *,int);

struct inDensCheck {
    int iRung;
    int bGreater;
    int iMeasure;
    };
struct outDensCheck {
    double dMaxDensError;
    double dAvgDensError;
    int nError;
    int nTotal;
    };

void pstDensCheck(PST,void *,int,void *,int *);

/* PST_ACCELSTEP */
struct inAccelStep {
    double dEta;
    double dVelFac;
    double dAccFac;
    double dDelta;
    int    iMaxRung;
    int    bDoGravity;
    int    bEpsAcc;
    double dhMinOverSoft;
    uint8_t uRungLo;
    uint8_t uRungHi;
    };
int pstAccelStep(PST,void *,int,void *,int);

/* PST_SPHSTEP */
struct inSphStep {
    double dDelta;
    double dEta;
    double dAccFac;
    double dEtaUDot;
    int iMaxRung;
    uint8_t uRungLo;
    uint8_t uRungHi;
    };
int pstSphStep(PST,void *,int,void *,int);

/* PST_STARFORM */
struct inStarForm
    {
    double dRateCoeff;
    double dTMax;
    double dDenMin;
    double dDelta;

    double dTime;
    double dInitStarMass;
    double dESNPerStarMass;
    double dtCoolingShutoff;

    double dtFeedbackDelay;
    double dMassLossPerStarMass;
    double dZMassPerStarMass;
    double dMinGasMass;
    double dTuFac;

    int bGasCooling;
    int bdivv;
    };

struct outStarForm 
    {
    int nFormed;
    int nDeleted;
    double dMassFormed;
    };

int pstStarForm(PST,void *,int,void *,int);

/* PST_DENSITYSTEP */
struct inDensityStep {
    double dDelta;
    double dEta;
    double dRhoFac;
    int iMaxRung;
    uint8_t uRungLo;
    uint8_t uRungHi;
    };
int pstDensityStep(PST,void *,int,void *,int);

/* PST_CORRECTENERGY */
struct inCorrectEnergy {
    double dTuFac;
    double z;
    double dTime;
    int    iDirection;
    };
int pstCorrectEnergy(PST, void *,int,void *,int);

/* PST_UPDATERUNG */
struct inUpdateRung {
    uint8_t uRungLo;  /* Minimum Rung to modify */
    uint8_t uRungHi;  /* Maximum Rung to modify */
    uint8_t uMinRung; /* Minimum it can be set to */
    uint8_t uMaxRung; /* Maximum it can be set to */
    };
struct outUpdateRung {
    uint64_t nRungCount[MAX_RUNG];
    };
int pstUpdateRung(PST,void *,int,void *,int);

/* PST_SETWRITESTART */
struct inSetWriteStart {
    uint64_t nWriteStart;
    };
int pstSetWriteStart(PST,void *,int,void *,int);

/* PST_ADDWRITESTART */
struct inAddWriteStart {
    uint64_t nWriteStart;
    };
int pstAddWriteStart(PST,void *,int,void *,int);

/* PST_COLNPARTS */
struct outColNParts {
    int nNew;
    int nDeltaGas;
    int nDeltaDark;
    int nDeltaStar;
    };
int pstColNParts(PST, void *, int, void *, int);

/* PST_NEWORDER */
int pstNewOrder(PST, void *, int, void *, int);

/* PST_GETNPARTS */
/* see pkd.h
 struct outGetNParts { 
	int n;
    int nGas;
    int nDark;
    int nStar;
    };
*/
int pstGetNParts(PST, void *, int, void *, int);

/* PST_SETNPARTS */
struct inSetNParts {
    uint64_t nGas;
    uint64_t nDark;
    uint64_t nStar;
    };
int pstSetNParts(PST, void *, int, void *, int);

/* PST_MARKSMOOTH */
struct inMarkSmooth {
    int nSmooth;
    int bGasOnly;
    int bPeriodic;
    int bSymmetric;
    int iMarkType;
    SMF smf;
    };
void pstMarkSmooth(PST,void *,int,void *,int *);


/* PST_NEW_FOF */
struct inNewFof {
    double dTau2;
    int nMinMembers;
    int bPeriodic;
    int nReplicas;
    int nBucket;
    };
int pstNewFof(PST,void *,int,void *,int);

/* PST_FOF_PHASES */
struct outFofPhases {
    int bMadeProgress;
    };
int pstFofPhases(PST,void *,int,void *,int);

/* PST_FOF_FINISH_UP */
struct inFofFinishUp{
    int nMinGroupSize;
    };
int pstFofFinishUp(PST,void *,int,void *,int);

int pstInitRelaxation(PST,void *,int,void *,int);

#ifdef MDL_FFTW
/* PST_GETFFTMAXSIZES */
struct inGetFFTMaxSizes {
    int nx,ny,nz;
    };
struct outGetFFTMaxSizes {
    uint64_t nMaxLocal;
    int nMaxZ;
    int nMaxY;
    };
int pstGetFFTMaxSizes(PST,void *,int,void *,int);

/* PST_GENERATEIC */
#define MAX_TF 4096
struct inGenerateIC {
    uint64_t nPerNode;
    double dBoxSize;
    double dBoxMass;
    double dExpansion;
    int iSeed;
    int bFixed;
    float fPhase;
    int nGrid;
    int b2LPT;
    int bComove;
    int nTf;
    double k[MAX_TF];
    double tf[MAX_TF];
    };
struct outGenerateIC {
    double dExpansion;
    uint64_t N;
    double noiseMean;
    double noiseCSQ;
    };
int pstGenerateIC(PST,void *,int,void *,int);

struct inGenerateICthread {
    struct inGenerateIC *ic;
    MDLFFT fft;
    };
int pltGenerateIC(PST,void *,int,void *,int);

/* PLT_MOVEIC */
struct inMoveIC {
    overlayedParticle *pBase;
    uint64_t iStart;
    uint64_t nMove;
    float fMass;
    float fSoft;
    int nGrid;
    };
int pltMoveIC(PST,void *,int,void *,int);
int pstMoveIC(PST,void *,int,void *,int);
#endif

/* PST_MEMSTATUS */
struct outMemStatus {
#ifdef __linux__
    uint64_t rss;
    uint64_t freeMemory;
#endif
    uint64_t nBytesTree;
    uint64_t nBytesCl;
    uint64_t nBytesIlp;
    uint64_t nBytesIlc;
    };
int pstMemStatus(PST,void *,int,void *,int);

/* PST_GETCLASSES - Output PARTCLASS[] */
int pstGetClasses(PST,void *,int,void *,int);

/* PST_SETCLASSES - Input PARTCLASS[] */
int pstSetClasses(PST,void *,int,void *,int);

/* PST_SWAPCLASSES - Input PARTCLASS[] - Output PARTCLASS[] */
int pstSwapClasses(PST,void *,int,void *,int);

/* PST_PROFILE */
#define PST_MAX_PROFILE_BINS 1000000
struct inProfile {
    double dCenter[3];
    double com[3];
    double vcm[3];
    double L[3];
    uint32_t nBins;
    uint8_t uRungLo;
    uint8_t uRungHi;
    double dRadii[PST_MAX_PROFILE_BINS];
    };
int pstProfile(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_CALCDISTANCE */
struct inCalcDistance {
    double dCenter[3];
    double dRadius;
    int bPeriodic;
    };
int pstCalcDistance(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_CALCCOM */
struct inCalcCOM {
    double dCenter[3];
    double dRadius;
    int bPeriodic;
    };
struct outCalcCOM {
    double com[3];
    double vcm[3];
    double L[3];
    double M;
    uint64_t N;
    };
int pstCalcCOM(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_COUNTDISTANCE */
struct inCountDistance {
    double dRadius2Inner;
    double dRadius2Outer;
    };
struct outCountDistance {
    uint64_t nCount;
    };
int pstCountDistance(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_GRID_CREATE_FFT */
struct inGridCreateFFT {
    int nGrid;
    };
int pstGridCreateFFT(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_GRID_DELETE_FFT */
int pstGridDeleteFFT(PST pst,void *vin,int nIn,void *vout,int nOut);

#ifdef MDL_FFTW
/* PST_ADD_LINEAR_SIGNAL */
struct inAddLinearSignal {
    int iGrid;
    int iSeed;
    int bFixed;
    float fPhase;
    double Lbox;
    double a;
    };
int pstAddLinearSignal(PST pst,void *vin,int nIn,void *vout,int nOut);
#define PST_MAX_K_BINS 2500
/* PST_GRID_BIN_K */
struct inGridBinK {
    int nBins;
    int iGrid;
    };
struct outGridBinK {
    double fK[PST_MAX_K_BINS];
    double fPower[PST_MAX_K_BINS];
    uint64_t nPower[PST_MAX_K_BINS];
    };
int pstGridBinK(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_ASSIGN_MASS */
struct inAssignMass {
    int iAssignment;
    int iGrid;
    float fDelta;
    };
int pstAssignMass(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_DENSITY_CONTRAST */
struct inDensityContrast {
    int iGrid;
    int k;
    double dTotalMass;
    };
int pstDensityContrast(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_WINDOW_CORRECTION */
struct inWindowCorrection {
    int iAssignment;
    int iGrid;
    };
int pstWindowCorrection(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_INTERLACE */
struct inInterlace {
    int iGridTarget;
    int iGridSource;
    };
int pstInterlace(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_BISPECTRUM_SELECT */
struct inBispectrumSelect {
    int iGridTarget;
    int iGridSource;
    double kmin, kmax;
    };
int pstBispectrumSelect(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_BISPECTRUM_CALCULATE */
struct inBispectrumCalculate {
    int iGrid1, iGrid2, iGrid3;
    };
int pstBispectrumCalculate(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_LINEARKICK */
struct inLinearKick {
    vel_t dtOpen, dtClose;
};
int pstLinearKick(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_SETLINGRID */
struct inSetLinGrid {
    double a0;
    double a;
    double a1;
    double dBSize;
    int nGrid;
    /* Noise generation */
    int iSeed;
    int bFixed;
    float fPhase;
    };
int pstSetLinGrid(PST pst,void *vin,int nIn,void *vout,int nOut);
/* PST_MEASURELINPK */
struct inMeasureLinPk {
    double dA;
    double dBoxSize;
    int iSeed;
    int bFixed; 
    float fPhase;
    int nGrid;
    int nBins;
    };
struct outMeasureLinPk {
    double fK[PST_MAX_K_BINS];
    double fPower[PST_MAX_K_BINS];
    uint64_t nPower[PST_MAX_K_BINS];
    };
int pstMeasureLinPk(PST pst,void *vin,int nIn,void *vout,int nOut);
#endif

/* PST_TOTALMASS */
struct outTotalMass {
    double dMass;
    };
int pstTotalMass(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_COUNTSPECIES */
int pstCountSpecies(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_SETWRITESPECIESCOUNTS */
struct inWriteSpeciesCounts {
    int iIndex;
    int nProcessors;
    int iLower, iUpper;
    int iTarget;
    };
int pstSetWriteSpeciesCounts(PST,void *,int,void *,int);

struct inLightConeOpen {
    int nSideHealpix;
    char achOutFile[PST_FILENAME_SIZE];
    };
int pstLightConeOpen(PST pst,void *vin,int nIn,void *vout,int nOut);
struct inLightConeClose {
    char achOutFile[PST_FILENAME_SIZE];
    };
int pstLightConeClose(PST pst,void *vin,int nIn,void *vout,int nOut);

struct inLightConeVel {
    double dBoxSize;
    };
int pstLightConeVel(PST pst,void *vin,int nIn,void *vout,int nOut);

/* PST_GET_PARICLES */
int pstGetParticles(PST pst,void *vin,int nIn,void *vout,int nOut);

#ifdef __cplusplus
}
#endif

#endif
