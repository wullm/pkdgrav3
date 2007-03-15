#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* for unlink() */
#include <stddef.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include <sys/param.h> /* for MAXHOSTNAMELEN, if available */
#include <rpc/types.h>
#include <rpc/xdr.h>

#include "master.h"
#include "tipsydefs.h"
#include "outtype.h"
#include "smoothfcn.h"

#define LOCKFILE ".lockfile"	/* for safety lock */
#define STOPFILE "STOP"			/* for user interrupt */

#define NEWTIME
#ifdef NEWTIME 
double msrTime() {
    struct timeval tv;
    struct timezone tz;

    tz.tz_minuteswest=0; 
    tz.tz_dsttime=0;
    gettimeofday(&tv,NULL);
    return (tv.tv_sec+(tv.tv_usec*1e-6));
    }
#else
double msrTime() {
    return (1.0*time(0));
    }
#endif

void _msrLeader(void) {
    puts("pkdgrav2 Joachim Stadel June 6, 2005");
    puts("USAGE: pkdgrav2 [SETTINGS | FLAGS] [SIM_FILE]");
    puts("SIM_FILE: Configuration file of a particular simulation, which");
    puts("          includes desired settings and relevant input and");
    puts("          output files. Settings specified in this file override");
    puts("          the default settings.");
    puts("SETTINGS");
    puts("or FLAGS: Command line settings or flags for a simulation which");
    puts("          will override any defaults and any settings or flags");
    puts("          specified in the SIM_FILE.");
    }


void _msrTrailer(void)
    {
    puts("(see man page for more information)");
    }


void _msrExit(MSR msr,int status)
    {
    MDL mdl=msr->mdl;

    msrFinish(msr);
    mdlFinish(mdl);
    exit(status);
    }


void
_msrMakePath(const char *dir,const char *base,char *path)
    {
    /*
    ** Prepends "dir" to "base" and returns the result in "path". It is the
    ** caller's responsibility to ensure enough memory has been allocated
    ** for "path".
    */

    if (!path) return;
    path[0] = 0;
    if (dir) {
	strcat(path,dir);
	strcat(path,"/");
	}
    if (!base) return;
    strcat(path,base);
    }


void msrInitialize(MSR *pmsr,MDL mdl,int argc,char **argv)
    {
    MSR msr;
    int j,ret;
    int id,nDigits;
    struct inSetAdd inAdd;
    struct inLevelize inLvl;
    struct inGetMap inGM;

    msr = (MSR)malloc(sizeof(struct msrContext));
    assert(msr != NULL);
    msr->mdl = mdl;
    msr->pst = NULL;
    msr->lcl.pkd = NULL;
    *pmsr = msr;
    csmInitialize(&msr->param.csm);
    /*
    ** Now setup for the input parameters.
    **
    ** NOTE: nThreads & bDiag are parsed here, but the actual values are
    ** read from the command line via mdlInitialize(). This means the
    ** values of nThreads & bDiag read by prmAddParam() are ignored!
    */
    prmInitialize(&msr->prm,_msrLeader,_msrTrailer);
    msr->param.nThreads = 1;
    prmAddParam(msr->prm,"nThreads",1,&msr->param.nThreads,sizeof(int),"sz",
		"<nThreads>");
    msr->param.bDiag = 0;
    prmAddParam(msr->prm,"bDiag",0,&msr->param.bDiag,sizeof(int),"d",
		"enable/disable per thread diagnostic output");
    msr->param.bOverwrite = 0;
    prmAddParam(msr->prm,"bOverwrite",0,&msr->param.bOverwrite,sizeof(int),
		"overwrite","enable/disable overwrite safety lock = +overwrite");
    msr->param.bVWarnings = 1;
    prmAddParam(msr->prm,"bVWarnings",0,&msr->param.bVWarnings,sizeof(int),
		"vwarnings","enable/disable warnings = +vwarnings");
    msr->param.bVStart = 1;
    prmAddParam(msr->prm,"bVStart",0,&msr->param.bVStart,sizeof(int),
		"vstart","enable/disable verbose start = +vstart");
    msr->param.bVStep = 1;
    prmAddParam(msr->prm,"bVStep",0,&msr->param.bVStep,sizeof(int),
		"vstep","enable/disable verbose step = +vstep");
    msr->param.bVRungStat = 1;
    prmAddParam(msr->prm,"bVRungStat",0,&msr->param.bVRungStat,sizeof(int),
		"vrungstat","enable/disable rung statistics = +vrungstat");
    msr->param.bVDetails = 0;
    prmAddParam(msr->prm,"bVDetails",0,&msr->param.bVDetails,sizeof(int),
		"vdetails","enable/disable verbose details = +vdetails");
    nDigits = 5;
    prmAddParam(msr->prm,"nDigits",1,&nDigits,sizeof(int),"nd",
		"<number of digits to use in output filenames> = 5");
    msr->param.bPeriodic = 0;
    prmAddParam(msr->prm,"bPeriodic",0,&msr->param.bPeriodic,sizeof(int),"p",
		"periodic/non-periodic = -p");
    msr->param.bParaRead = 1;
    prmAddParam(msr->prm,"bParaRead",0,&msr->param.bParaRead,sizeof(int),"par",
		"enable/disable parallel reading of files = +par");
    msr->param.bParaWrite = 1;
    prmAddParam(msr->prm,"bParaWrite",0,&msr->param.bParaWrite,sizeof(int),"paw",
		"enable/disable parallel writing of files = +paw");
    msr->param.bCannonical = 1;
    prmAddParam(msr->prm,"bCannonical",0,&msr->param.bCannonical,sizeof(int),"can",
		"enable/disable use of cannonical momentum = +can");
    msr->param.bDoDensity = 1;
    prmAddParam(msr->prm,"bDoDensity",0,&msr->param.bDoDensity,sizeof(int),
		"den","enable/disable density outputs = +den");
    msr->param.bDodtOutput = 0;
    prmAddParam(msr->prm,"bDodtOutput",0,&msr->param.bDodtOutput,sizeof(int),
		"dtout","enable/disable dt outputs = -dtout");
    msr->param.nBucket = 8;
    prmAddParam(msr->prm,"nBucket",1,&msr->param.nBucket,sizeof(int),"b",
		"<max number of particles in a bucket> = 8");
    msr->param.iStartStep = 0;
    prmAddParam(msr->prm,"iStartStep",1,&msr->param.iStartStep,
		sizeof(int),"nstart","<initial step numbering> = 0");
    msr->param.nSteps = 0;
    prmAddParam(msr->prm,"nSteps",1,&msr->param.nSteps,sizeof(int),"n",
		"<number of timesteps> = 0");
    msr->param.iOutInterval = 0;
    prmAddParam(msr->prm,"iOutInterval",1,&msr->param.iOutInterval,sizeof(int),
		"oi","<number of timesteps between snapshots> = 0");
    msr->param.iLogInterval = 10;
    prmAddParam(msr->prm,"iLogInterval",1,&msr->param.iLogInterval,sizeof(int),
		"ol","<number of timesteps between logfile outputs> = 10");
    msr->param.bEwald = 1;
    prmAddParam(msr->prm,"bEwald",0,&msr->param.bEwald,sizeof(int),"ewald",
		"enable/disable Ewald correction = +ewald");
    msr->param.iEwOrder = 4;
    prmAddParam(msr->prm,"iEwOrder",1,&msr->param.iEwOrder,sizeof(int),"ewo",
		"<Ewald multipole expansion order: 1, 2, 3 or 4> = 4");
    msr->param.nReplicas = 0;
    prmAddParam(msr->prm,"nReplicas",1,&msr->param.nReplicas,sizeof(int),"nrep",
		"<nReplicas> = 0 for -p, or 1 for +p");
    msr->param.dSoft = 0.0;
    prmAddParam(msr->prm,"dSoft",2,&msr->param.dSoft,sizeof(double),"e",
		"<gravitational softening length> = 0.0");
    msr->param.dSoftMax = 0.0;
    prmAddParam(msr->prm,"dSoftMax",2,&msr->param.dSoftMax,sizeof(double),"eMax",
		"<maximum comoving gravitational softening length (abs or multiplier)> = 0.0");
    msr->param.bPhysicalSoft = 0;
    prmAddParam(msr->prm,"bPhysicalSoft",0,&msr->param.bPhysicalSoft,sizeof(int),"PhysSoft",
		"<Physical gravitational softening length> -PhysSoft");
    msr->param.bSoftMaxMul = 1;
    prmAddParam(msr->prm,"bSoftMaxMul",0,&msr->param.bSoftMaxMul,sizeof(int),"SMM",
		"<Use maximum comoving gravitational softening length as a multiplier> +SMM");
    msr->param.bVariableSoft = 0;
    prmAddParam(msr->prm,"bVariableSoft",0,&msr->param.bVariableSoft,sizeof(int),"VarSoft",
		"<Variable gravitational softening length> -VarSoft");
    msr->param.nSoftNbr = 32;
    prmAddParam(msr->prm,"nSoftNbr",1,&msr->param.nSoftNbr,sizeof(int),"VarSoft",
		"<Neighbours for Variable gravitational softening length> 32");
    msr->param.bSoftByType = 1;
    prmAddParam(msr->prm,"bSoftByType",0,&msr->param.bSoftByType,sizeof(int),"SBT",
		"<Variable gravitational softening length by Type> +SBT");
    msr->param.bDoSoftOutput = 0;
    prmAddParam(msr->prm,"bDoSoftOutput",0,&msr->param.bDoSoftOutput,sizeof(int),
		"softout","enable/disable soft outputs = -softout");
    msr->param.bDoRungOutput = 0;
    prmAddParam(msr->prm,"bDoRungOutput",0,&msr->param.bDoRungOutput,sizeof(int),
		"rungout","enable/disable rung outputs = -rungout");
    msr->param.dDelta = 0.0;
    prmAddParam(msr->prm,"dDelta",2,&msr->param.dDelta,sizeof(double),"dt",
		"<time step>");
    msr->param.dEta = 0.1;
    prmAddParam(msr->prm,"dEta",2,&msr->param.dEta,sizeof(double),"eta",
		"<time step criterion> = 0.1");
    msr->param.bGravStep = 0;
    prmAddParam(msr->prm,"bGravStep",0,&msr->param.bGravStep,sizeof(int),
		"gs","<Gravity timestepping according to iTimeStep Criterion>");
    msr->param.bEpsAccStep = 1;
    prmAddParam(msr->prm,"bEpsAccStep",0,&msr->param.bEpsAccStep,sizeof(int),
		"ea", "<Sqrt(Epsilon on a) timestepping>");
    msr->param.bSqrtPhiStep = 0;
    prmAddParam(msr->prm,"bSqrtPhiStep",0,&msr->param.bSqrtPhiStep,sizeof(int),
		"sphi", "<Sqrt(Phi) on a timestepping>");
    msr->param.bDensityStep = 0;
    prmAddParam(msr->prm,"bDensityStep",0,&msr->param.bDensityStep,sizeof(int),
		"isrho", "<Sqrt(1/Rho) timestepping>");
    msr->param.iTimeStepCrit = 0;
    prmAddParam(msr->prm,"iTimeStepCrit",1,&msr->param.iTimeStepCrit,sizeof(int),
		"tsc", "<Time Stepping Criteria for GravStep>");
    msr->param.nPColl = 0;
    prmAddParam(msr->prm,"nPColl",1,&msr->param.nPColl,sizeof(int),
		"npcoll", "<Number of particles in collisional regime>");
    msr->param.nTruncateRung = 0;
    prmAddParam(msr->prm,"nTruncateRung",1,&msr->param.nTruncateRung,sizeof(int),"nTR",
		"<number of MaxRung particles to delete MaxRung> = 0");
    msr->param.iMaxRung = 1;
    prmAddParam(msr->prm,"iMaxRung",1,&msr->param.iMaxRung,sizeof(int),
		"mrung", "<maximum timestep rung>");
    msr->param.nRungVeryActive = 31;
    prmAddParam(msr->prm,"nRungVeryActive",1,&msr->param.nRungVeryActive,
		sizeof(int), "nvactrung", "<timestep rung to use n^2>");
    msr->param.dEwCut = 2.6;
    prmAddParam(msr->prm,"dEwCut",2,&msr->param.dEwCut,sizeof(double),"ew",
		"<dEwCut> = 2.6");
    msr->param.dEwhCut = 2.8;
    prmAddParam(msr->prm,"dEwhCut",2,&msr->param.dEwhCut,sizeof(double),"ewh",
		"<dEwhCut> = 2.8");
    msr->param.dTheta = 0.8;
    msr->param.dTheta2 = msr->param.dTheta;
    prmAddParam(msr->prm,"dTheta",2,&msr->param.dTheta,sizeof(double),"theta",
		"<Barnes opening criterion> = 0.8");
    prmAddParam(msr->prm,"dTheta2",2,&msr->param.dTheta2,sizeof(double),
		"theta2","<Barnes opening criterion for a >= daSwitchTheta> = 0.8");
    msr->param.daSwitchTheta = 1./3.;
    prmAddParam(msr->prm,"daSwitchTheta",2,&msr->param.daSwitchTheta,sizeof(double),"aSwitchTheta",
		"<a to switch theta at> = 1./3.");
    msr->param.dPeriod = 1.0;
    prmAddParam(msr->prm,"dPeriod",2,&msr->param.dPeriod,sizeof(double),"L",
		"<periodic box length> = 1.0");
    msr->param.dxPeriod = 1.0;
    prmAddParam(msr->prm,"dxPeriod",2,&msr->param.dxPeriod,sizeof(double),"Lx",
		"<periodic box length in x-dimension> = 1.0");
    msr->param.dyPeriod = 1.0;
    prmAddParam(msr->prm,"dyPeriod",2,&msr->param.dyPeriod,sizeof(double),"Ly",
		"<periodic box length in y-dimension> = 1.0");
    msr->param.dzPeriod = 1.0;
    prmAddParam(msr->prm,"dzPeriod",2,&msr->param.dzPeriod,sizeof(double),"Lz",
		"<periodic box length in z-dimension> = 1.0");
    msr->param.achInFile[0] = 0;
    prmAddParam(msr->prm,"achInFile",3,msr->param.achInFile,256,"I",
		"<input file name> (file in TIPSY binary format)");
    strcpy(msr->param.achOutName,"pkdgrav");
    prmAddParam(msr->prm,"achOutName",3,msr->param.achOutName,256,"o",
		"<output name for snapshots and logfile> = \"pkdgrav\"");
    msr->param.csm->bComove = 0;
    prmAddParam(msr->prm,"bComove",0,&msr->param.csm->bComove,sizeof(int),
		"cm", "enable/disable comoving coordinates = -cm");
    msr->param.csm->dHubble0 = 0.0;
    prmAddParam(msr->prm,"dHubble0",2,&msr->param.csm->dHubble0, 
		sizeof(double),"Hub", "<dHubble0> = 0.0");
    msr->param.csm->dOmega0 = 1.0;
    prmAddParam(msr->prm,"dOmega0",2,&msr->param.csm->dOmega0,
		sizeof(double),"Om", "<dOmega0> = 1.0");
    msr->param.csm->dLambda = 0.0;
    prmAddParam(msr->prm,"dLambda",2,&msr->param.csm->dLambda,
		sizeof(double),"Lambda", "<dLambda> = 0.0");
    msr->param.csm->dOmegaRad = 0.0;
    prmAddParam(msr->prm,"dOmegaRad",2,&msr->param.csm->dOmegaRad,
		sizeof(double),"Omrad", "<dOmegaRad> = 0.0");
    msr->param.csm->dOmegab = 0.0;
    prmAddParam(msr->prm,"dOmegab",2,&msr->param.csm->dOmegab,
		sizeof(double),"Omb", "<dOmegab> = 0.0");
    strcpy(msr->param.achDataSubPath,".");
    prmAddParam(msr->prm,"achDataSubPath",3,msr->param.achDataSubPath,256,
		NULL,NULL);
    msr->param.dExtraStore = 0.1;
    prmAddParam(msr->prm,"dExtraStore",2,&msr->param.dExtraStore,
		sizeof(double),NULL,NULL);
    msr->param.nSmooth = 64;
    prmAddParam(msr->prm,"nSmooth",1,&msr->param.nSmooth,sizeof(int),"s",
		"<number of particles to smooth over> = 64");
    msr->param.bStandard = 0;
    prmAddParam(msr->prm,"bStandard",0,&msr->param.bStandard,sizeof(int),"std",
		"output in standard TIPSY binary format = -std");
    msr->param.bDoublePos = 0;
    prmAddParam(msr->prm,"bDoublePos",0,&msr->param.bDoublePos,sizeof(int),"dp",
		"input/output double precision positions (standard format only) = -dp");
    msr->param.dRedTo = 0.0;	
    prmAddParam(msr->prm,"dRedTo",2,&msr->param.dRedTo,sizeof(double),"zto",
		"specifies final redshift for the simulation");
    msr->param.nGrowMass = 0;
    prmAddParam(msr->prm,"nGrowMass",1,&msr->param.nGrowMass,sizeof(int),
		"gmn","<number of particles to increase mass> = 0");
    msr->param.dGrowDeltaM = 0.0;
    prmAddParam(msr->prm,"dGrowDeltaM",2,&msr->param.dGrowDeltaM,
		sizeof(double),"gmdm","<Total growth in mass/particle> = 0.0");
    msr->param.dGrowStartT = 0.0;
    prmAddParam(msr->prm,"dGrowStartT",2,&msr->param.dGrowStartT,
		sizeof(double),"gmst","<Start time for growing mass> = 0.0");
    msr->param.dGrowEndT = 1.0;
    prmAddParam(msr->prm,"dGrowEndT",2,&msr->param.dGrowEndT,
		sizeof(double),"gmet","<End time for growing mass> = 1.0");
    msr->param.dFracNoTreeSqueeze = 0.005;
    prmAddParam(msr->prm,"dFracNoTreeSqueeze",2,&msr->param.dFracNoTreeSqueeze,
		sizeof(double),"fnts",
		"<Fraction of Active Particles for no Tree Squeeze> = 0.005");
    msr->param.dFracNoDomainDecomp = 0.002;
    prmAddParam(msr->prm,"dFracNoDomainDecomp",2,&msr->param.dFracNoDomainDecomp,
		sizeof(double),"fndd",
		"<Fraction of Active Particles for no new DD> = 0.002");
    msr->param.dFracNoDomainDimChoice = 0.1;
    prmAddParam(msr->prm,"dFracNoDomainDimChoice",2,&msr->param.dFracNoDomainDimChoice,
		sizeof(double),"fnddc",
		"<Fraction of Active Particles for no new DD dimension choice> = 0.1");
    msr->param.bDoGravity = 1;
    prmAddParam(msr->prm,"bDoGravity",0,&msr->param.bDoGravity,sizeof(int),"g",
		"enable/disable interparticle gravity = +g");
    msr->param.bRungDD = 0;
    prmAddParam(msr->prm,"bRungDomainDecomp",0,&msr->param.bRungDD,sizeof(int),
		"RungDD","<Rung Domain Decomp> = 0");
    msr->param.dRungDDWeight = 1.0;
    prmAddParam(msr->prm,"dRungDDWeight",2,&msr->param.dRungDDWeight,sizeof(int),
		"RungDDWeight","<Rung Domain Decomp Weight> = 1.0");
    msr->param.dCentMass = 1.0;
    prmAddParam(msr->prm,"dCentMass",2,&msr->param.dCentMass,sizeof(double),
		"fgm","specifies the central mass for Keplerian orbits");
    msr->param.iWallRunTime = 0;
    prmAddParam(msr->prm,"iWallRunTime",1,&msr->param.iWallRunTime,
		sizeof(int),"wall",
		"<Maximum Wallclock time (in minutes) to run> = 0 = infinite");
    msr->param.bAntiGrav = 0;
    prmAddParam(msr->prm,"bAntiGrav",0,&msr->param.bAntiGrav,sizeof(int),"antigrav",
		"reverse gravity making it repulsive = -antigrav");
    msr->param.nFindGroups = 0;
    prmAddParam(msr->prm,"nFindGroups",1,&msr->param.nFindGroups,sizeof(int),
		"nFindGroups","<number of iterative FOFs> = 0");
    msr->param.nMinMembers = 16;
    prmAddParam(msr->prm,"nMinMembers",1,&msr->param.nMinMembers,sizeof(int),
		"nMinMembers","<minimum number of group members> = 16");
    msr->param.dTau = 0.164;
    prmAddParam(msr->prm,"dTau",2,&msr->param.dTau,sizeof(double),"dTau",
		"<linking lenght for first FOF in units of mean particle separation> = 0.164");
    msr->param.bTauAbs = 0;
    prmAddParam(msr->prm,"bTauAbs",0,&msr->param.bTauAbs,sizeof(int),"bTauAbs",
		"<if 1 use simulation units for dTau, not mean particle separation> = 0");
    msr->param.nBins = 0;
    prmAddParam(msr->prm,"nBins",1,&msr->param.nBins,sizeof(int),"nBins",
		"<number of bin in profiles, no profiles if 0 or negative> = 0");
    msr->param.bUsePotmin = 0;
    prmAddParam(msr->prm,"bUsePotmin",0,&msr->param.bUsePotmin,sizeof(int),"bUsePotmin",
		"<if 1 use minima of pot. instead of com for fist FOF level profiles> = 0");
    msr->param.nMinProfile = 2000;
    prmAddParam(msr->prm,"nMinProfile",1,&msr->param.nMinProfile,sizeof(int),"nMinProfile",
		"<minimum number of particles in a group to make a profile> = 2000");
    msr->param.fBinsRescale = 1.0;
    prmAddParam(msr->prm,"fBinsRescale",2,&msr->param.fBinsRescale,sizeof(double),"fBinsRescale",
		"<if bigger than one, nMinProfile gets smaller in recursive fofs> = 1.0");
    msr->param.fContrast = 10.0;
    prmAddParam(msr->prm,"fContrast",2,&msr->param.fContrast,sizeof(double),"fContrast",
		"<the density contrast used for recursive fofs> = 10.0");
    msr->param.Delta = 200.0;
    prmAddParam(msr->prm,"Delta",2,&msr->param.Delta,sizeof(double),"Delta",
		"<the density contrast whitin the virial radius over simulation unit density> = 200.0");
    msr->param.binFactor = 3.0;
    prmAddParam(msr->prm,"binFactor",2,&msr->param.binFactor,sizeof(double),"binFactor",
		"<ratio of largest spherical bin to fof determined group radius> = 3.0");
    msr->param.bLogBins = 0;
    prmAddParam(msr->prm,"bLogBins",0,&msr->param.bLogBins,
		sizeof(int),"bLogBins","use logaritmic bins instead of linear = -bLogBins");
#ifdef RELAXATION
    msr->param.bTraceRelaxation = 0;
    prmAddParam(msr->prm,"bTraceRelaxation",0,&msr->param.bTraceRelaxation,sizeof(int),
		"rtrace","<enable/disable relaxation tracing> = -rtrace");
#endif /* RELAXATION */
/*
** Set the box center to (0,0,0) for now!
*/
    for (j=0;j<3;++j) msr->fCenter[j] = 0.0;
    /*
    ** Define any "LOCAL" parameters (LCL)
    */
    msr->lcl.pszDataPath = getenv("PTOOLS_DATA_PATH");
    /*
    ** Process command line arguments.
    */
    ret = prmArgProc(msr->prm,argc,argv);
    if (!ret) {
	_msrExit(msr,1);
	}

    if (nDigits < 1 || nDigits > 9) {
	(void) fprintf(stderr,"Unreasonable number of filename digits.\n");
	_msrExit(msr,1);
	}

    (void) sprintf(msr->param.achDigitMask,"%%s.%%0%ii",nDigits);

    /*
    ** Make sure that we have some setting for nReplicas if bPeriodic is set.
    */
    if (msr->param.bPeriodic && !prmSpecified(msr->prm,"nReplicas")) {
	msr->param.nReplicas = 1;
	}
    /*
    ** Warn that we have a setting for nReplicas if bPeriodic NOT set.
    */
    if (!msr->param.bPeriodic && msr->param.nReplicas != 0) {
	printf("WARNING: nReplicas set to non-zero value for non-periodic!\n");
	}

    if (!msr->param.achInFile[0]) {
	puts("ERROR: no input file specified");
	_msrExit(msr,1);
	}

    if (msr->param.dTheta <= 0) {
	if (msr->param.dTheta == 0 && msr->param.bVWarnings)
	    fprintf(stderr,"WARNING: Zero opening angle may cause numerical problems\n");
	else if (msr->param.dTheta < 0) {
	    fprintf(stderr,"ERROR: Opening angle must be non-negative\n");
	    _msrExit(msr,1);
	    }
	}

    msr->nThreads = mdlThreads(mdl);
	
    /*
    ** Always set bCannonical = 1 if bComove == 0
    */
    if (!msr->param.csm->bComove) {
	if (!msr->param.bCannonical)
	    printf("WARNING: bCannonical reset to 1 for non-comoving (bComove == 0)\n");
	msr->param.bCannonical = 1;
	} 
    /* 
     * Softening 
     */
	
    if (msr->param.bPhysicalSoft || msr->param.bVariableSoft) {
	if (msr->param.bPhysicalSoft && !msrComove(msr)) {
	    printf("WARNING: bPhysicalSoft reset to 0 for non-comoving (bComove == 0)\n");
	    msr->param.bPhysicalSoft = 0;
	    }
#ifndef CHANGESOFT
	fprintf(stderr,"ERROR: You must compile with -DCHANGESOFT to use changing softening options\n");
	_msrExit(msr,1);
#endif
	if (msr->param.bVariableSoft && !prmSpecified(msr->prm,"bDoSoftOutput")) msr->param.bDoSoftOutput=1;
  
	if (msr->param.bPhysicalSoft && msr->param.bVariableSoft) {
	    fprintf(stderr,"ERROR: You may only choose one of Physical or Variable softening\n");
	    _msrExit(msr,1);
	    }
	}
    /*
    ** Determine the period of the box that we are using.
    ** Set the new d[xyz]Period parameters which are now used instead
    ** of a single dPeriod, but we still want to have compatibility
    ** with the old method of setting dPeriod.
    */
    if (prmSpecified(msr->prm,"dPeriod") && 
	!prmSpecified(msr->prm,"dxPeriod")) {
	msr->param.dxPeriod = msr->param.dPeriod;
	}
    if (prmSpecified(msr->prm,"dPeriod") && 
	!prmSpecified(msr->prm,"dyPeriod")) {
	msr->param.dyPeriod = msr->param.dPeriod;
	}
    if (prmSpecified(msr->prm,"dPeriod") && 
	!prmSpecified(msr->prm,"dzPeriod")) {
	msr->param.dzPeriod = msr->param.dPeriod;
	}
    /*
    ** Periodic boundary conditions can be disabled along any of the
    ** x,y,z axes by specifying a period of zero for the given axis.
    ** Internally, the period is set to infinity (Cf. pkdBucketWalk()
    ** and pkdDrift(); also the INTERSECT() macro in smooth.h).
    */
    if (msr->param.dPeriod  == 0) msr->param.dPeriod  = FLOAT_MAXVAL;
    if (msr->param.dxPeriod == 0) msr->param.dxPeriod = FLOAT_MAXVAL;
    if (msr->param.dyPeriod == 0) msr->param.dyPeriod = FLOAT_MAXVAL;
    if (msr->param.dzPeriod == 0) msr->param.dzPeriod = FLOAT_MAXVAL;
    /*
    ** Determine opening type.
    */

    msr->dCrit = msr->param.dTheta;
    if (!prmSpecified(msr->prm,"dTheta2")) 
	msr->param.dTheta2 = msr->param.dTheta;
    /*
    ** Initialize comove variables.
    */
    msr->nMaxOuts = 100;
    msr->pdOutTime = malloc(msr->nMaxOuts*sizeof(double));
    assert(msr->pdOutTime != NULL);
    msr->nOuts = 0;

    /*
    ** Check timestepping.
    */

    if (msr->param.iMaxRung < 1) {
	msr->param.iMaxRung = 1;
	if (msr->param.bVWarnings)
	    (void) fprintf(stderr,"WARNING: iMaxRung set to 1\n");
	}

    if (msr->param.bGravStep && !msr->param.bDoGravity) {
	puts("ERROR: need gravity to use gravity stepping...");
	_msrExit(msr,1);
	}
    if (msr->param.bEpsAccStep || msr->param.bSqrtPhiStep) {
	msr->param.bAccelStep = 1;
	}

    pstInitialize(&msr->pst,msr->mdl,&msr->lcl);

    pstAddServices(msr->pst,msr->mdl);
    /*
    ** Create the processor subset tree.
    */
    for (id=1;id<msr->nThreads;++id) {
	if (msr->param.bVDetails) printf("Adding %d to the pst\n",id);
	inAdd.id = id;
	pstSetAdd(msr->pst,&inAdd,sizeof(inAdd),NULL,NULL);
	}
    if (msr->param.bVDetails) printf("\n");
    /*
    ** Levelize the PST.
    */
    inLvl.iLvl = 0;
    pstLevelize(msr->pst,&inLvl,sizeof(inLvl),NULL,NULL);
    /*
    ** Create the processor mapping array for the one-node output
    ** routines.
    */
    msr->pMap = malloc(msr->nThreads*sizeof(int));
    assert(msr->pMap != NULL);
    inGM.nStart = 0;
    pstGetMap(msr->pst,&inGM,sizeof(inGM),msr->pMap,NULL);
    /*
    ** Initialize tree type to none.
    */
    msr->iCurrMaxRung = 0;
    /*
    ** Mark the Domain Decompositon as not done
    */
    msr->bDoneDomainDecomp = 0;
    msr->iLastRungDD = -1;
    msr->iLastRungSD = -1;
    msr->nRung = (int *) malloc( (msr->param.iMaxRung+1)*sizeof(int) );
    assert(msr->nRung != NULL);
    }


void msrLogParams(MSR msr,FILE *fp)
    {
    double z;
    int i;

#ifdef __DATE__
#ifdef __TIME__
    fprintf(fp,"# Compiled: %s %s\n",__DATE__,__TIME__);
#endif
#endif
    fprintf(fp,"# Preprocessor macros:");
#ifdef CHANGESOFT
    fprintf(fp," CHANGESOFT");
#endif
#ifdef DEBUG
    fprintf(fp," DEBUG");
#endif
#ifdef RELAXATION
    fprintf(fp,"RELAXATION");
#endif
#ifdef _REENTRANT
    fprintf(fp," _REENTRANT");
#endif
#ifdef MAXHOSTNAMELEN
	{
	char hostname[MAXHOSTNAMELEN];
	fprintf(fp,"\n# Master host: ");
	if (gethostname(hostname,MAXHOSTNAMELEN))
	    fprintf(fp,"unknown");
	else
	    fprintf(fp,"%s",hostname);
	}
#endif
	fprintf(fp,"\n# N: %d",msr->N);
	fprintf(fp," nThreads: %d",msr->param.nThreads);
	fprintf(fp," bDiag: %d",msr->param.bDiag);
	fprintf(fp," Verbosity flags: (%d,%d,%d,%d,%d)",msr->param.bVWarnings,
		msr->param.bVStart,msr->param.bVStep,msr->param.bVRungStat,
		msr->param.bVDetails);
	fprintf(fp,"\n# bPeriodic: %d",msr->param.bPeriodic);
	fprintf(fp," bComove: %d",msr->param.csm->bComove);
	fprintf(fp,"\n# bParaRead: %d",msr->param.bParaRead);
	fprintf(fp," bParaWrite: %d",msr->param.bParaWrite);
	fprintf(fp," bCannonical: %d",msr->param.bCannonical);
	fprintf(fp," bStandard: %d",msr->param.bStandard);
	fprintf(fp," nBucket: %d",msr->param.nBucket);
	fprintf(fp," iOutInterval: %d",msr->param.iOutInterval);
	fprintf(fp," iLogInterval: %d",msr->param.iLogInterval);
	fprintf(fp," iEwOrder: %d",msr->param.iEwOrder);
	fprintf(fp," nReplicas: %d",msr->param.nReplicas);
	fprintf(fp,"\n# dEwCut: %f",msr->param.dEwCut);
	fprintf(fp," dEwhCut: %f",msr->param.dEwhCut);
	fprintf(fp,"\n# iStartStep: %d",msr->param.iStartStep);
	fprintf(fp," nSteps: %d",msr->param.nSteps);
	fprintf(fp," nSmooth: %d",msr->param.nSmooth);
	fprintf(fp," dExtraStore: %f",msr->param.dExtraStore);
	if (prmSpecified(msr->prm,"dSoft"))
	    fprintf(fp," dSoft: %g",msr->param.dSoft);
	else
	    fprintf(fp," dSoft: input");
	fprintf(fp,"\n# bPhysicalSoft: %d",msr->param.bPhysicalSoft);
	fprintf(fp," bVariableSoft: %d",msr->param.bVariableSoft);
	fprintf(fp," nSoftNbr: %d",msr->param.nSoftNbr);
	fprintf(fp," bSoftByType: %d",msr->param.bSoftByType);
	fprintf(fp," bSoftMaxMul: %d",msr->param.bSoftMaxMul);
	fprintf(fp," dSoftMax: %g",msr->param.dSoftMax);
	fprintf(fp," bDoSoftOutput: %d",msr->param.bDoSoftOutput);
	fprintf(fp,"\n# dDelta: %g",msr->param.dDelta);
	fprintf(fp," dEta: %g",msr->param.dEta);
	fprintf(fp," iMaxRung: %d",msr->param.iMaxRung);
	fprintf(fp," nRungVeryActive: %d",msr->param.nRungVeryActive);
	fprintf(fp," bDoRungOutput: %d",msr->param.bDoRungOutput);
	fprintf(fp,"\n# bGravStep: %d",msr->param.bGravStep);
	fprintf(fp," bEpsAccStep: %d",msr->param.bEpsAccStep);
	fprintf(fp," bSqrtPhiStep: %d",msr->param.bSqrtPhiStep);
	fprintf(fp," bDensityStep: %d",msr->param.bDensityStep);
	fprintf(fp," nTruncateRung: %d",msr->param.nTruncateRung);
	fprintf(fp,"\n# iTimeStepCrit: %d",msr->param.iTimeStepCrit);
	fprintf(fp," nPColl: %d", msr->param.nPColl);
	fprintf(fp,"\n# bDoGravity: %d",msr->param.bDoGravity);
	fprintf(fp," dCentMass: %g",msr->param.dCentMass);
	fprintf(fp,"\n# dFracNoTreeSqueeze: %g",msr->param.dFracNoTreeSqueeze);
	fprintf(fp,"\n# dFracNoDomainDecomp: %g",msr->param.dFracNoDomainDecomp);
	fprintf(fp," dFracNoDomainDimChoice: %g",msr->param.dFracNoDomainDimChoice);
	fprintf(fp," bRungDD: %d",msr->param.bRungDD);
	fprintf(fp," dRungDDWeight: %g ",msr->param.dRungDDWeight);
	fprintf(fp,"\n# nTruncateRung: %d",msr->param.nTruncateRung);
	fprintf(fp,"\n# nGrowMass: %d",msr->param.nGrowMass);
	fprintf(fp," dGrowDeltaM: %g",msr->param.dGrowDeltaM);
	fprintf(fp," dGrowStartT: %g",msr->param.dGrowStartT);
	fprintf(fp," dGrowEndT: %g",msr->param.dGrowEndT);
	fprintf(fp,"\n# Group Find: nFindGroups: %d",msr->param.nFindGroups);
	fprintf(fp," dTau: %g",msr->param.dTau);
	fprintf(fp," bTauAbs: %d",msr->param.bTauAbs);
	fprintf(fp," nMinMembers: %d",msr->param.nMinMembers);	
	fprintf(fp," nBins: %d",msr->param.nBins);
	fprintf(fp," bUsePotmin: %d",msr->param.bUsePotmin);
	fprintf(fp," nMinProfile: %d",msr->param.nMinProfile);
	fprintf(fp," fBinsRescale: %g",msr->param.fBinsRescale);
	fprintf(fp," fContrast: %g",msr->param.fContrast);
	fprintf(fp," Delta: %g",msr->param.Delta);
	fprintf(fp," binFactor: %g",msr->param.binFactor);
	fprintf(fp," bLogBins: %d",msr->param.bLogBins);
#ifdef RELAXATION
	fprintf(fp,"\n# Relaxation estimate: bTraceRelaxation: %d",msr->param.bTraceRelaxation);	
#endif
	fprintf(fp," dTheta: %f",msr->param.dTheta);
	fprintf(fp,"\n# dPeriod: %g",msr->param.dPeriod);
	fprintf(fp," dxPeriod: %g",
		msr->param.dxPeriod >= FLOAT_MAXVAL ? 0 : msr->param.dxPeriod);
	fprintf(fp," dyPeriod: %g",
		msr->param.dyPeriod >= FLOAT_MAXVAL ? 0 : msr->param.dyPeriod);
	fprintf(fp," dzPeriod: %g",
		msr->param.dzPeriod >= FLOAT_MAXVAL ? 0 : msr->param.dzPeriod);
	fprintf(fp,"\n# dHubble0: %g",msr->param.csm->dHubble0);
	fprintf(fp," dOmega0: %g",msr->param.csm->dOmega0);
	fprintf(fp," dLambda: %g",msr->param.csm->dLambda);
	fprintf(fp," dOmegaRad: %g",msr->param.csm->dOmegaRad);
	fprintf(fp," dOmegab: %g",msr->param.csm->dOmegab);
	fprintf(fp,"\n# achInFile: %s",msr->param.achInFile);
	fprintf(fp,"\n# achOutName: %s",msr->param.achOutName); 
	fprintf(fp,"\n# achDataSubPath: %s",msr->param.achDataSubPath);
	if (msr->param.csm->bComove) {
	    fprintf(fp,"\n# RedOut:");
	    if (msr->nOuts == 0) fprintf(fp," none");
	    for (i=0;i<msr->nOuts;i++) {
		if (i%5 == 0) fprintf(fp,"\n#   ");
		z = 1.0/csmTime2Exp(msr->param.csm, msr->pdOutTime[i]) - 1.0;
		fprintf(fp," %f",z);
		}
	    fprintf(fp,"\n");
	    }
	else {
	    fprintf(fp,"\n# TimeOut:");
	    if (msr->nOuts == 0) fprintf(fp," none");
	    for (i=0;i<msr->nOuts;i++) {
		if (i%5 == 0) fprintf(fp,"\n#   ");
		fprintf(fp," %f",msr->pdOutTime[i]);
		}
	    fprintf(fp,"\n");
	    }
    }

int
msrGetLock(MSR msr)
    {
    /*
    ** Attempts to lock run directory to prevent overwriting. If an old lock
    ** is detected with the same achOutName, an abort is signaled. Otherwise
    ** a new lock is created. The bOverwrite parameter flag can be used to
    ** suppress lock checking.
    */

    FILE *fp = NULL;
    char achTmp[256],achFile[256];

    _msrMakePath(msr->param.achDataSubPath,LOCKFILE,achTmp);
    _msrMakePath(msr->lcl.pszDataPath,achTmp,achFile);
    if (!msr->param.bOverwrite && (fp = fopen(achFile,"r"))) {
	(void) fscanf(fp,"%s",achTmp);
	(void) fclose(fp);
	if (!strcmp(msr->param.achOutName,achTmp)) {
	    (void) printf("ABORT: %s detected.\nPlease ensure data is safe to "
			  "overwrite. Delete lockfile and try again.\n",achFile);
	    return 0;
	    }
	}
    if (!(fp = fopen(achFile,"w"))) {
	if (msr->param.bOverwrite && msr->param.bVWarnings) {
	    (void) printf("WARNING: Unable to create %s...ignored.\n",achFile);
	    return 1;
	    }
	else {
	    (void) printf("Unable to create %s\n",achFile);
	    return 0;
	    }
	}
    (void) fprintf(fp,msr->param.achOutName);
    (void) fclose(fp);
    return 1;
    }

int
msrCheckForStop(MSR msr)
    {
    /*
    ** Checks for existence of STOPFILE in run directory. If found, the file
    ** is removed and the return status is set to 1, otherwise 0.
    */

    static char achFile[256];
    static int first_call = 1;

    FILE *fp = NULL;

    if (first_call) {
	char achTmp[256];
	_msrMakePath(msr->param.achDataSubPath,STOPFILE,achTmp);
	_msrMakePath(msr->lcl.pszDataPath,achTmp,achFile);
	first_call = 0;
	}
    if ((fp = fopen(achFile,"r"))) {
	(void) printf("User interrupt detected.\n");
	(void) fclose(fp);
	(void) unlink(achFile);
	return 1;
	}
    return 0;
    }

void msrFinish(MSR msr)
    {
    int id;

    for (id=1;id<msr->nThreads;++id) {
	if (msr->param.bVDetails) printf("Stopping thread %d\n",id);		
	mdlReqService(msr->mdl,id,SRV_STOP,NULL,0);
	mdlGetReply(msr->mdl,id,NULL,NULL);
	}
    pstFinish(msr->pst);
    csmFinish(msr->param.csm);
    /*
    ** finish with parameter stuff, deallocate and exit.
    */
    prmFinish(msr->prm);
    free(msr->pMap);
    free(msr);
    }

void msrOneNodeReadTipsy(MSR msr, struct inReadTipsy *in)
    {
    int i,id;
    int *nParts;				/* number of particles for each processor */
    int nStart;
    PST pst0;
    LCL *plcl;
    char achInFile[PST_FILENAME_SIZE];
    int nid;
    int inswap;
    struct inSetParticleTypes intype;

    nParts = malloc(msr->nThreads*sizeof(*nParts));
    for (id=0;id<msr->nThreads;++id) {
	nParts[id] = -1;
	}

    pstOneNodeReadInit(msr->pst, in, sizeof(*in), nParts, &nid);
    assert(nid == msr->nThreads*sizeof(*nParts));
    for (id=0;id<msr->nThreads;++id) {
	assert(nParts[id] > 0);
	}

    pst0 = msr->pst;
    while(pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;
    /*
    ** Add the local Data Path to the provided filename.
    */
    _msrMakePath(plcl->pszDataPath,in->achInFile,achInFile);

    nStart = nParts[0];
    assert(msr->pMap[0] == 0);
    for (i=1;i<msr->nThreads;++i) {
	id = msr->pMap[i];
	/* 
	 * Read particles into the local storage.
	 */
	assert(plcl->pkd->nStore >= nParts[id]);
	pkdReadTipsy(plcl->pkd,achInFile,nStart,nParts[id],
		     in->bStandard,in->dvFac,in->dTuFac,in->bDoublePos);
	nStart += nParts[id];
	/* 
	 * Now shove them over to the remote processor.
	 */
	inswap = 0;
	mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	pkdSwapAll(plcl->pkd, id);
	mdlGetReply(pst0->mdl,id,NULL,NULL);
    	}
    assert(nStart == msr->N);
    /* 
     * Now read our own particles.
     */
    pkdReadTipsy(plcl->pkd,achInFile,0,nParts[0],in->bStandard,in->dvFac,
		 in->dTuFac,in->bDoublePos);
    pstSetParticleTypes(msr->pst,&intype,sizeof(intype),NULL,NULL);
    }

int xdrHeader(XDR *pxdrs,struct dump *ph)
    {
    int pad = 0;
	
    if (!xdr_double(pxdrs,&ph->time)) return 0;
    if (!xdr_int(pxdrs,&ph->nbodies)) return 0;
    if (!xdr_int(pxdrs,&ph->ndim)) return 0;
    if (!xdr_int(pxdrs,&ph->nsph)) return 0;
    if (!xdr_int(pxdrs,&ph->ndark)) return 0;
    if (!xdr_int(pxdrs,&ph->nstar)) return 0;
    if (!xdr_int(pxdrs,&pad)) return 0;
    return 1;
    }


double msrReadTipsy(MSR msr)
    {
    FILE *fp;
    struct dump h;
    struct inReadTipsy in;
    char achInFile[PST_FILENAME_SIZE];
    LCL *plcl = msr->pst->plcl;
    double dTime,aTo,tTo,z;
    struct inSetParticleTypes intype;
	
    if (msr->param.achInFile[0]) {
	/*
	** Add Data Subpath for local and non-local names.
	*/
	_msrMakePath(msr->param.achDataSubPath,msr->param.achInFile,in.achInFile);
	/*
	** Add local Data Path.
	*/
	_msrMakePath(plcl->pszDataPath,in.achInFile,achInFile);
	fp = fopen(achInFile,"r");

	if (!fp) {
	    printf("Could not open InFile:%s\n",achInFile);
	    _msrExit(msr,1);
	    }
	}
    else {
	printf("No input file specified\n");
	_msrExit(msr,1);
	return -1.0;
	}
    /*
    ** Assume tipsy format for now, and dark matter only.
    */
    if (msr->param.bStandard) {
	XDR xdrs;

	xdrstdio_create(&xdrs,fp,XDR_DECODE);
	xdrHeader(&xdrs,&h);
	xdr_destroy(&xdrs);
	}
    else {
	fread(&h,sizeof(struct dump),1,fp);
	}
    fclose(fp);
    msr->N = h.nbodies;
    msr->nDark = h.ndark;
    msr->nGas = h.nsph;
    msr->nStar = h.nstar;
    msr->nMaxOrder = msr->N - 1;
    msr->nMaxOrderGas = msr->nGas - 1;
    msr->nMaxOrderDark = msr->nGas + msr->nDark - 1;
    assert(msr->N == msr->nDark+msr->nGas+msr->nStar);
    if (msr->param.csm->bComove) {
	if(msr->param.csm->dHubble0 == 0.0) {
	    printf("No hubble constant specified\n");
	    _msrExit(msr,1);
	    }
	dTime = csmExp2Time(msr->param.csm,h.time);
	z = 1.0/h.time - 1.0;
	if (msr->param.bVStart)
	    printf("Input file, Time:%g Redshift:%g Expansion factor:%g iStartStep:%d\n",
		   dTime,z,h.time,msr->param.iStartStep);
	if (prmSpecified(msr->prm,"dRedTo")) {
	    if (!prmArgSpecified(msr->prm,"nSteps") &&
		prmArgSpecified(msr->prm,"dDelta")) {
		aTo = 1.0/(msr->param.dRedTo + 1.0);
		tTo = csmExp2Time(msr->param.csm,aTo);
		if (msr->param.bVStart)
		    printf("Simulation to Time:%g Redshift:%g Expansion factor:%g\n",
			   tTo,1.0/aTo-1.0,aTo);
		if (tTo < dTime) {
		    printf("Badly specified final redshift, check -zto parameter.\n");
		    _msrExit(msr,1);
		    }
		msr->param.nSteps = (int)ceil((tTo-dTime)/msr->param.dDelta);
		}
	    else if (!prmArgSpecified(msr->prm,"dDelta") &&
		     prmArgSpecified(msr->prm,"nSteps")) {
		aTo = 1.0/(msr->param.dRedTo + 1.0);
		tTo = csmExp2Time(msr->param.csm,aTo);
		if (msr->param.bVStart)
		    printf("Simulation to Time:%g Redshift:%g Expansion factor:%g\n",
			   tTo,1.0/aTo-1.0,aTo);
		if (tTo < dTime) {
		    printf("Badly specified final redshift, check -zto parameter.\n");	
		    _msrExit(msr,1);
		    }
		if(msr->param.nSteps != 0)
		    msr->param.dDelta =
			(tTo-dTime)/(msr->param.nSteps -
				     msr->param.iStartStep);
				
		else
		    msr->param.dDelta = 0.0;
		}
	    else if (!prmSpecified(msr->prm,"nSteps") &&
		     prmFileSpecified(msr->prm,"dDelta")) {
		aTo = 1.0/(msr->param.dRedTo + 1.0);
		tTo = csmExp2Time(msr->param.csm,aTo);
		if (msr->param.bVStart)
		    printf("Simulation to Time:%g Redshift:%g Expansion factor:%g\n",
			   tTo,1.0/aTo-1.0,aTo);
		if (tTo < dTime) {
		    printf("Badly specified final redshift, check -zto parameter.\n");
		    _msrExit(msr,1);
		    }
		msr->param.nSteps = (int)ceil((tTo-dTime)/msr->param.dDelta);
		}
	    else if (!prmSpecified(msr->prm,"dDelta") &&
		     prmFileSpecified(msr->prm,"nSteps")) {
		aTo = 1.0/(msr->param.dRedTo + 1.0);
		tTo = csmExp2Time(msr->param.csm,aTo);
		if (msr->param.bVStart)
		    printf("Simulation to Time:%g Redshift:%g Expansion factor:%g\n",
			   tTo,1.0/aTo-1.0,aTo);
		if (tTo < dTime) {
		    printf("Badly specified final redshift, check -zto parameter.\n");	
		    _msrExit(msr,1);
		    }
		if(msr->param.nSteps != 0)
		    msr->param.dDelta =	(tTo-dTime)/(msr->param.nSteps
						     - msr->param.iStartStep);
		else
		    msr->param.dDelta = 0.0;
		}
	    }
	else {
	    tTo = dTime + msr->param.nSteps*msr->param.dDelta;
	    aTo = csmTime2Exp(msr->param.csm,tTo);
	    if (msr->param.bVStart)
		printf("Simulation to Time:%g Redshift:%g Expansion factor:%g\n",
		       tTo,1.0/aTo-1.0,aTo);
	    }
	if (msr->param.bVStart)
	    printf("Reading file...\nN:%d nDark:%d nGas:%d nStar:%d\n",msr->N,
		   msr->nDark,msr->nGas,msr->nStar);
	if (msr->param.bCannonical) {
	    in.dvFac = h.time*h.time;
	    }
	else {
	    in.dvFac = 1.0;
	    }
	}
    else {
	dTime = h.time;
	if (msr->param.bVStart) printf("Input file, Time:%g iStartStep:%d\n",dTime,msr->param.iStartStep);
	tTo = dTime + (msr->param.nSteps - msr->param.iStartStep)*msr->param.dDelta;
	if (msr->param.bVStart) {
	    printf("Simulation to Time:%g\n",tTo);
	    printf("Reading file...\nN:%d nDark:%d nGas:%d nStar:%d Time:%g\n",
		   msr->N,msr->nDark,msr->nGas,msr->nStar,dTime);
	    }
	in.dvFac = 1.0;
	}
    in.nFileStart = 0;
    in.nFileEnd = msr->N - 1;
    in.nDark = msr->nDark;
    in.nGas = msr->nGas;
    in.nStar = msr->nStar;
    in.bStandard = msr->param.bStandard;
    in.bDoublePos = msr->param.bDoublePos;

    in.dTuFac = 1.0;
    /*
    ** Since pstReadTipsy causes the allocation of the local particle
    ** store, we need to tell it the percentage of extra storage it
    ** should allocate for load balancing differences in the number of
    ** particles.
    */
    in.fExtraStore = msr->param.dExtraStore;
    /*
    ** Provide the period.
    */
    in.fPeriod[0] = msr->param.dxPeriod;
    in.fPeriod[1] = msr->param.dyPeriod;
    in.fPeriod[2] = msr->param.dzPeriod;

    if(msr->param.bParaRead)
	pstReadTipsy(msr->pst,&in,sizeof(in),NULL,NULL);
    else
	msrOneNodeReadTipsy(msr, &in);
    pstSetParticleTypes(msr->pst, &intype, sizeof(intype), NULL, NULL);
    if (msr->param.bVDetails) puts("Input file has been successfully read.");
    /*
    ** Now read in the output points, passing the initial time.
    ** We do this only if nSteps is not equal to zero.
    */
    if (msrSteps(msr) > 0) msrReadOuts(msr,dTime);
    /*
    ** Set up the output counter.
    */
    for (msr->iOut=0;msr->iOut<msr->nOuts;++msr->iOut) {
	if (dTime < msr->pdOutTime[msr->iOut]) break;
	}
    return(dTime);
    }


/*
** This function makes some DANGEROUS assumptions!!!
** Main problem is that it calls pkd level routines, bypassing the
** pst level. It uses plcl pointer which is not desirable.
*/
void msrOneNodeWriteTipsy(MSR msr, struct inWriteTipsy *in)
    {
    int i,id;
    int nStart;
    PST pst0;
    LCL *plcl;
    char achOutFile[PST_FILENAME_SIZE];
    int inswap;

    pst0 = msr->pst;
    while(pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;
    /*
    ** Add the local Data Path to the provided filename.
    */
    _msrMakePath(plcl->pszDataPath,in->achOutFile,achOutFile);

    /* 
     * First write our own particles.
     */
    pkdWriteTipsy(plcl->pkd,achOutFile,plcl->nWriteStart,in->bStandard,
		  in->dvFac,in->duTFac,in->bDoublePos); 
    nStart = plcl->pkd->nLocal;
    assert(msr->pMap[0] == 0);
    for (i=1;i<msr->nThreads;++i) {
	id = msr->pMap[i];
	/* 
	 * Swap particles with the remote processor.
	 */
	inswap = 0;
	mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	pkdSwapAll(plcl->pkd, id);
	mdlGetReply(pst0->mdl,id,NULL,NULL);
	/* 
	 * Write the swapped particles.
	 */
	pkdWriteTipsy(plcl->pkd,achOutFile,nStart,
		      in->bStandard, in->dvFac, in->duTFac,in->bDoublePos); 
	nStart += plcl->pkd->nLocal;
	/* 
	 * Swap them back again.
	 */
	inswap = 0;
	mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	pkdSwapAll(plcl->pkd, id);
	mdlGetReply(pst0->mdl,id,NULL,NULL);
    	}
    assert(nStart == msr->N);
    }


void msrCalcWriteStart(MSR msr) 
    {
    struct outSetTotal out;
    struct inSetWriteStart in;

    pstSetTotal(msr->pst,NULL,0,&out,NULL);
    assert(out.nTotal == msr->N);
    in.nWriteStart = 0;
    pstSetWriteStart(msr->pst,&in,sizeof(in),NULL,NULL);
    }


void msrWriteTipsy(MSR msr,char *pszFileName,double dTime)
    {
    FILE *fp;
    struct dump h;
    struct inWriteTipsy in;
    char achOutFile[PST_FILENAME_SIZE];
    LCL *plcl = msr->pst->plcl;
    /*
    ** Calculate where to start writing.
    ** This sets plcl->nWriteStart.
    */
    msrCalcWriteStart(msr);
    /*
    ** Add Data Subpath for local and non-local names.
    */
    _msrMakePath(msr->param.achDataSubPath,pszFileName,in.achOutFile);
    /*
    ** Add local Data Path.
    */
    _msrMakePath(plcl->pszDataPath,in.achOutFile,achOutFile);
    fp = fopen(achOutFile,"w");
    if (!fp) {
	printf("Could not open OutFile:%s\n",achOutFile);
	_msrExit(msr,1);
	}
    in.bStandard = msr->param.bStandard;
    in.duTFac = 1.0;
    in.bDoublePos = msr->param.bDoublePos;
    /*
    ** Assume tipsy format for now.
    */
    h.nbodies = msr->N;
    h.ndark = msr->nDark;
    h.nsph = msr->nGas;
    h.nstar = msr->nStar;
    if (msr->param.csm->bComove) {
	h.time = csmTime2Exp(msr->param.csm,dTime);
	if (msr->param.bCannonical) {
	    in.dvFac = 1.0/(h.time*h.time);
	    }
	else {
	    in.dvFac = 1.0;
	    }
	}
    else {
	h.time = dTime;
	in.dvFac = 1.0;
	}
    h.ndim = 3;
    if (msr->param.bVDetails) {
	if (msr->param.csm->bComove) {
	    printf("Writing file...\nTime:%g Redshift:%g\n",
		   dTime,(1.0/h.time - 1.0));
	    }
	else {
	    printf("Writing file...\nTime:%g\n",dTime);
	    }
	}
    if (in.bStandard) {
	XDR xdrs;

	xdrstdio_create(&xdrs,fp,XDR_ENCODE);
	xdrHeader(&xdrs,&h);
	xdr_destroy(&xdrs);
	}
    else {
	fwrite(&h,sizeof(struct dump),1,fp);
	}
    fclose(fp);

    if(msr->param.bParaWrite)
	pstWriteTipsy(msr->pst,&in,sizeof(in),NULL,NULL);
    else
	msrOneNodeWriteTipsy(msr, &in);
    if (msr->param.bVDetails)
	puts("Output file has been successfully written.");
    }


void msrSetSoft(MSR msr,double dSoft)
    {
    struct inSetSoft in;
  
    if (msr->param.bVDetails) printf("Set Softening...\n");
    in.dSoft = dSoft;
    pstSetSoft(msr->pst,&in,sizeof(in),NULL,NULL);
    }


void msrDomainDecomp(MSR msr,int iRung,int bGreater,int bSplitVA) {
    struct inDomainDecomp in;
    struct outCalcBound outcb;
    int j;
    double sec,dsec;

    int iRungDD,iRungSD,nActive;


    in.bDoRootFind = 1;
    in.bDoSplitDimFind = 1;
    in.nBndWrap[0] = 0;
    in.nBndWrap[1] = 0;
    in.nBndWrap[2] = 0;

    /*
    ** If we are dealing with a nice periodic volume in all
    ** three dimensions then we can set the initial bounds
    ** instead of calculating them.
    */
    if (msr->param.bPeriodic && 
	msr->param.dxPeriod < FLOAT_MAXVAL &&
	msr->param.dyPeriod < FLOAT_MAXVAL && 
	msr->param.dzPeriod < FLOAT_MAXVAL) {
	for (j=0;j<3;++j) {
	    in.bnd.fCenter[j] = msr->fCenter[j];
	    }
	in.bnd.fMax[0] = 0.5*msr->param.dxPeriod;
	in.bnd.fMax[1] = 0.5*msr->param.dyPeriod;
	in.bnd.fMax[2] = 0.5*msr->param.dzPeriod;
	}
    else {
	pstCalcBound(msr->pst,NULL,0,&outcb,NULL);
	in.bnd = outcb.bnd;
	}
	
    nActive=0;
    if (bGreater) {
	iRungDD=msr->iCurrMaxRung+1; 
	while (iRungDD > iRung) {
	    iRungDD--;
	    nActive+=msr->nRung[iRungDD];
	    }
	while(iRungDD > 0 && nActive < msr->N*msr->param.dFracNoDomainDecomp) {
	    iRungDD--;
	    nActive+=msr->nRung[iRungDD];
	    }
	iRungSD = iRungDD;
	while(iRungSD > 0 && nActive < msr->N*msr->param.dFracNoDomainDimChoice) {
	    iRungSD--;
	    nActive+=msr->nRung[iRungSD];
	    }
	}
    else {
	iRungDD = iRung;
	while(iRungDD > 0 && msr->nRung[iRungDD] < msr->N*msr->param.dFracNoDomainDecomp) {
	    iRungDD--;
	    }
	iRungSD = iRungDD;
	while(iRungSD > 0 && msr->nRung[iRungSD] < msr->N*msr->param.dFracNoDomainDimChoice) {
	    iRungSD--;
	    }
	}

    if (msr->nActive < msr->N*msr->param.dFracNoDomainDecomp) {
	if (msr->bDoneDomainDecomp && msr->iLastRungDD >= iRungDD) {
	    if (msr->param.bVRungStat) printf("Skipping Root Finder (nActive = %d/%d, iRung %d/%d/%d)\n",msr->nActive,msr->N,iRung,iRungDD,msr->iLastRungDD);
	    in.bDoRootFind = 0;
	    in.bDoSplitDimFind = 0;
	    }
	}
    else iRungDD = iRung;

    if (in.bDoRootFind && msr->bDoneDomainDecomp && iRungDD > iRungSD && msr->iLastRungSD >= iRungSD) {
	if (msr->param.bVRungStat) printf("Skipping Split Dim Finding (nActive = %d/%d, iRung %d/%d/%d/%d)\n",msr->nActive,msr->N,iRung,iRungDD,iRungSD,msr->iLastRungSD);
	in.bDoSplitDimFind = 0;
	}

    if (iRungDD < iRung) 
	msrActiveRung(msr,iRungDD,bGreater); 

    in.nActive = msr->nActive;
    in.nTotal = msr->N;

    if (msr->param.bRungDD) {
	struct inRungDDWeight inRDD;
	inRDD.iMaxRung = msr->iCurrMaxRung;
	inRDD.dWeight = msr->param.dRungDDWeight;
	pstRungDDWeight(msr->pst,&inRDD,sizeof(struct inRungDDWeight),NULL,NULL);
	}

    if (msr->param.bVDetails) {
	printf("Domain Decomposition: nActive (Rung %d) %d\n",iRungDD,msr->nActive);
	printf("Domain Decomposition... \n");
	sec = msrTime();
	}

    in.bSplitVA = bSplitVA;
    if (bSplitVA) {
	printf("*** Splitting very active particles!\n");	
	}
    pstDomainDecomp(msr->pst,&in,sizeof(in),NULL,NULL);
    msr->bDoneDomainDecomp = 1; 

    if (msr->param.bVDetails) {
	dsec = msrTime() - sec;
	printf("Domain Decomposition complete, Wallclock: %f secs\n\n",dsec);
	}

    if (in.bDoSplitDimFind) {
	msr->iLastRungSD = iRungDD;
	}
    if (in.bDoRootFind) {
	msr->iLastRungDD = iRungDD;
	}

    if (iRungDD < iRung) {
	/* Restore Active data */
	msrActiveRung(msr,iRung,bGreater);
	}
    }

/*
** This the meat of the tree build, but will be called by differently named
** functions in order to implement special features without recoding...
*/
void _BuildTree(MSR msr,double dMass,int bExcludeVeryActive) {
    struct inBuildTree in;
    struct ioCalcRoot root;
    KDN *pkdn;
    int iDum,nCell;

    if (msr->param.bVDetails) printf("Building local trees...\n\n");

    in.nBucket = msr->param.nBucket;
    in.diCrit2 = 1/(msr->dCrit*msr->dCrit);
    nCell = 1<<(1+(int)ceil(log((double)msr->nThreads)/log(2.0)));
    pkdn = malloc(nCell*sizeof(KDN));
    assert(pkdn != NULL);
    in.iCell = ROOT;
    in.nCell = nCell;
    in.bTreeSqueeze = (msr->nActive > msr->N*msr->param.dFracNoTreeSqueeze);
    in.bExcludeVeryActive = bExcludeVeryActive;
    if (msr->param.bVDetails) {
	double sec,dsec;
	sec = msrTime();
	pstBuildTree(msr->pst,&in,sizeof(in),pkdn,&iDum);
	printf("Done pstBuildTree\n");
	dsec = msrTime() - sec;
	printf("Tree built, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstBuildTree(msr->pst,&in,sizeof(in),pkdn,&iDum);
	}
    pstDistribCells(msr->pst,pkdn,nCell*sizeof(KDN),NULL,NULL);
    free(pkdn);
    if (!bExcludeVeryActive) {
	/*
	** For simplicity we will skip calculating the Root for all particles 
	** with exclude very active since there are missing particles which 
	** could add to the mass and because it probably is not important to 
	** update the root so frequently.
	*/
	pstCalcRoot(msr->pst,NULL,0,&root,&iDum);
	pstDistribRoot(msr->pst,&root,sizeof(struct ioCalcRoot),NULL,NULL);
	}
    }

void msrBuildTree(MSR msr,double dMass) {
    _BuildTree(msr,dMass,0);
    }

void msrBuildTreeExcludeVeryActive(MSR msr,double dMass) {
    _BuildTree(msr,dMass,1);
    }


#ifdef GASOLINE
void msrCalcBoundBall(MSR msr,double fBallFactor)
    {
    struct inCalcBoundBall in;
    BND *pbnd;
    int iDum,nCell;

    in.fBallFactor = fBallFactor;
    nCell = 1<<(1+(int)ceil(log((double)msr->nThreads)/log(2.0)));
    pbnd = malloc(nCell*sizeof(BND));
    assert(pbnd != NULL);
    in.iCell = ROOT;
    in.nCell = nCell;
    pstCalcBoundBall(msr->pst,&in,sizeof(in),pbnd,&iDum);
    pstDistribBoundBall(msr->pst,pbnd,nCell*sizeof(BND),NULL,NULL);
    free(pbnd);
    }
#endif

void msrReorder(MSR msr)
    {
    struct inDomainOrder in;

    in.iMaxOrder = msrMaxOrder(msr);
    if (msr->param.bVDetails) {
	double sec,dsec;
	printf("Ordering...\n");
	sec = msrTime();
	pstDomainOrder(msr->pst,&in,sizeof(in),NULL,NULL);
	pstLocalOrder(msr->pst,NULL,0,NULL,NULL);
	dsec = msrTime() - sec;
	printf("Order established, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstDomainOrder(msr->pst,&in,sizeof(in),NULL,NULL);
	pstLocalOrder(msr->pst,NULL,0,NULL,NULL);
	}
    /*
    ** Mark domain decomp as not done.
    */
    msr->bDoneDomainDecomp = 0;
    }


void msrOutArray(MSR msr,char *pszFile,int iType)
    {
    struct inOutArray in;
    char achOutFile[PST_FILENAME_SIZE];
    LCL *plcl;
    PST pst0;
    FILE *fp;
    int id,i;
    int inswap;

    pst0 = msr->pst;
    while(pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;
    if (pszFile) {
	/*
	** Add Data Subpath for local and non-local names.
	*/
	_msrMakePath(msr->param.achDataSubPath,pszFile,in.achOutFile);
	/*
	** Add local Data Path.
	*/
	_msrMakePath(plcl->pszDataPath,in.achOutFile,achOutFile);
	fp = fopen(achOutFile,"w");

	if (!fp) {
	    printf("Could not open Array Output File:%s\n",achOutFile);
	    _msrExit(msr,1);
	    }
	}
    else {
	printf("No Array Output File specified\n");
	_msrExit(msr,1);
	return;
	}
    /*
    ** Write the Header information and close the file again.
    */
    fprintf(fp,"%d\n",msr->N);
    fclose(fp);
    /* 
     * First write our own particles.
     */
    assert(msr->pMap[0] == 0);
    pkdOutArray(plcl->pkd,achOutFile,iType); 
    for (i=1;i<msr->nThreads;++i) {
	id = msr->pMap[i];
	/* 
	 * Swap particles with the remote processor.
	 */
	inswap = 0;
	mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	pkdSwapAll(plcl->pkd, id);
	mdlGetReply(pst0->mdl,id,NULL,NULL);
	/* 
	 * Write the swapped particles.
	 */
	pkdOutArray(plcl->pkd,achOutFile,iType); 
	/* 
	 * Swap them back again.
	 */
	inswap = 0;
	mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	pkdSwapAll(plcl->pkd, id);
	mdlGetReply(pst0->mdl,id,NULL,NULL);
	}
    }


void msrOutVector(MSR msr,char *pszFile,int iType)
    {
    struct inOutVector in;
    char achOutFile[PST_FILENAME_SIZE];
    LCL *plcl;
    PST pst0;
    FILE *fp;
    int id,i;
    int inswap;
    int iDim;

    pst0 = msr->pst;
    while(pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;
    if (pszFile) {
	/*
	** Add Data Subpath for local and non-local names.
	*/
	_msrMakePath(msr->param.achDataSubPath,pszFile,in.achOutFile);
	/*
	** Add local Data Path.
	*/
	_msrMakePath(plcl->pszDataPath,in.achOutFile,achOutFile);
	fp = fopen(achOutFile,"w");

	if (!fp) {
	    printf("Could not open Vector Output File:%s\n",achOutFile);
	    _msrExit(msr,1);
	    }
	}
    else {
	printf("No Vector Output File specified\n");
	_msrExit(msr,1);
	return;
	}
    /*
    ** Write the Header information and close the file again.
    */
    fprintf(fp,"%d\n",msr->N);
    fclose(fp);

    /* 
     * First write our own particles.
     */
    assert(msr->pMap[0] == 0);
    for (iDim=0;iDim<3;++iDim) {
	pkdOutVector(plcl->pkd,achOutFile,iDim,iType); 
	for (i=1;i<msr->nThreads;++i) {
	    id = msr->pMap[i];
	    /* 
	     * Swap particles with the remote processor.
	     */
	    inswap = 0;
	    mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	    pkdSwapAll(plcl->pkd, id);
	    mdlGetReply(pst0->mdl,id,NULL,NULL);
	    /* 
	     * Write the swapped particles.
	     */
	    pkdOutVector(plcl->pkd,achOutFile,iDim,iType); 
	    /* 
	     * Swap them back again.
	     */
	    inswap = 0;
	    mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	    pkdSwapAll(plcl->pkd,id);
	    mdlGetReply(pst0->mdl,id,NULL,NULL);
	    }
	}
    }


void msrSmooth(MSR msr,double dTime,int iSmoothType,int bGasOnly,
	       int bSymmetric)
    {
    struct inSmooth in;

    /*
    ** Make sure that the type of tree is a density binary tree!
    */
    in.nSmooth = msr->param.nSmooth;
    in.bGasOnly = bGasOnly;
    in.bPeriodic = msr->param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    if (msrComove(msr)) {
	in.smf.H = csmTime2Hub(msr->param.csm,dTime);
	in.smf.a = csmTime2Exp(msr->param.csm,dTime);
	}
    else {
	in.smf.H = 0.0;
	in.smf.a = 1.0;
	}
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("Smoothing...\n");
	sec = msrTime();
	pstSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	dsec = msrTime() - sec;
	printf("Smooth Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	}
    }


void msrReSmooth(MSR msr,double dTime,int iSmoothType,int bGasOnly,
		 int bSymmetric)
    {
    struct inReSmooth in;

    /*
    ** Make sure that the type of tree is a density binary tree!
    */
    in.nSmooth = msr->param.nSmooth;
    in.bGasOnly = bGasOnly;
    in.bPeriodic = msr->param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    if (msrComove(msr)) {
	in.smf.H = csmTime2Hub(msr->param.csm,dTime);
	in.smf.a = csmTime2Exp(msr->param.csm,dTime);
	}
    else {
	in.smf.H = 0.0;
	in.smf.a = 1.0;
	}
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("ReSmoothing...\n");
	sec = msrTime();
	pstReSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	dsec = msrTime() - sec;
	printf("ReSmooth Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstReSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	}
    }

#ifdef GASOLINE
void msrReSmoothWalk(MSR msr,double dTime,int iSmoothType,int bGasOnly,
		     int bSymmetric)
    {
    struct inReSmooth in;

    /*
    ** Make sure that the type of tree is a density binary tree!
    */
    in.nSmooth = msr->param.nSmooth;
    in.bGasOnly = bGasOnly;
    in.bPeriodic = msr->param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    in.dfBall2OverSoft2 = (msr->param.bLowerSoundSpeed ? 0 :
			   4.0*msr->param.dhMinOverSoft*msr->param.dhMinOverSoft);
    if (msrComove(msr)) {
	in.smf.H = csmTime2Hub(msr->param.csm,dTime);
	in.smf.a = csmTime2Exp(msr->param.csm,dTime);
	}
    else {
	in.smf.H = 0.0;
	in.smf.a = 1.0;
	}
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("ReSmoothWalk...\n");
	sec = msrTime();
	pstReSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	dsec = msrTime() - sec;
	printf("ReSmoothWalk Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstReSmoothWalk(msr->pst,&in,sizeof(in),NULL,NULL);
	}
    }
#endif

void msrUpdateSoft(MSR msr,double dTime) {
#ifdef CHANGESOFT
    if (!(msr->param.bPhysicalSoft || msr->param.bVariableSoft)) return;
    if (msr->param.bPhysicalSoft) {
	struct inPhysicalSoft in;

	in.dFac = 1./csmTime2Exp(msr->param.csm,dTime);
	in.bSoftMaxMul = msr->param.bSoftMaxMul;
	in.dSoftMax = msr->param.dSoftMax;

	if (msr->param.bSoftMaxMul && in.dFac > in.dSoftMax) in.dFac = in.dSoftMax;

	pstPhysicalSoft(msr->pst,&in,sizeof(in),NULL,NULL);
	}
    else {
	struct inPostVariableSoft inPost;
	int bSymmetric = 0;
	int bGasOnly;

	pstPreVariableSoft(msr->pst,NULL,0,NULL,NULL);

	if (msr->param.bSoftByType) {
	    if (msr->nDark) {
		msrActiveType(msr,TYPE_DARK,TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
		msrBuildTree(msr,-1.0);
		bGasOnly = 0;
		assert(0); /* can't do this yet! */
		msrSmooth(msr,dTime,SMX_NULL,bGasOnly,bSymmetric);
		}
	    if (msr->nGas) {
		msrActiveType(msr,TYPE_GAS,TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
		msrBuildTree(msr,-1.0);
		bGasOnly = 1;
		msrSmooth(msr,dTime,SMX_NULL,bGasOnly,bSymmetric);
		}
	    if (msr->nStar) {
		msrActiveType(msr,TYPE_STAR,TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
		msrBuildTree(msr,-1.0);
		bGasOnly = 0;
		assert(0); /* can't do this yet! */
		msrSmooth(msr,dTime,SMX_NULL,bGasOnly,bSymmetric);
		}
	    }
	else {
	    msrActiveType(msr,TYPE_ALL,TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
	    msrBuildTree(msr,-1.0);
	    bGasOnly = 0;
	    msrSmooth(msr,dTime,SMX_NULL,bGasOnly,bSymmetric);
	    }

	inPost.dSoftMax = msr->param.dSoftMax;
	inPost.bSoftMaxMul = msr->param.bSoftMaxMul;
	pstPostVariableSoft(msr->pst,&inPost,sizeof(inPost),NULL,NULL);
	}
#endif
    }


void msrGravity(MSR msr,double dStep,int *piSec,double *pdWMax,double *pdIMax,
		double *pdEMax,int *pnActive)
    {
    struct inGravity in;
    struct outGravity out;
    int iDum;
    double sec,dsec;

    if (msr->param.bVStep) printf("Calculating Gravity, Step:%f\n",dStep);
    in.nReps = msr->param.nReplicas;
    in.bPeriodic = msr->param.bPeriodic;
    in.bEwald = msr->param.bEwald;
    in.iEwOrder = msr->param.iEwOrder;
    in.dEwCut = msr->param.dEwCut;
    in.dEwhCut = msr->param.dEwhCut;
    sec = msrTime();
    pstGravity(msr->pst,&in,sizeof(in),&out,&iDum);
    dsec = msrTime() - sec;

    *piSec = dsec;
    *pnActive = out.nActive;
    *pdWMax = out.dWMax;
    *pdIMax = out.dIMax;
    *pdEMax = out.dEMax;
    if (msr->param.bVStep) {
	double dPartAvg,dCellAvg;
	double dWAvg,dWMax,dWMin;
	double dIAvg,dIMax,dIMin;
	double dEAvg,dEMax,dEMin;
	double iP;

	/*
	** Output some info...
	*/
	if(dsec > 0.0) {
	    double dMFlops = out.dFlop/dsec*1e-6;
	    printf("Gravity Calculated, Wallclock: %f secs, MFlops:%.1f, Flop:%.3g\n",
		   dsec,dMFlops,out.dFlop);
	    }
	else {
	    printf("Gravity Calculated, Wallclock: %f secs, MFlops:unknown, Flop:%.3g\n",
		   dsec,out.dFlop);
	    }
	if (out.nActive > 0) {
	    dPartAvg = out.dPartSum/out.nActive;
	    dCellAvg = out.dCellSum/out.nActive;
	    }
	else {
	    dPartAvg = dCellAvg = 0;
	    if (msr->param.bVWarnings)
		printf("WARNING: no particles found!\n");

	    }
	iP = 1.0/msr->nThreads;
	dWAvg = out.dWSum*iP;
	dIAvg = out.dISum*iP;
	dEAvg = out.dESum*iP;
	dWMax = out.dWMax;
	dIMax = out.dIMax;
	dEMax = out.dEMax;
	dWMin = out.dWMin;
	dIMin = out.dIMin;
	dEMin = out.dEMin;
	printf("dPartAvg:%f dCellAvg:%f\n",
	       dPartAvg,dCellAvg);
	printf("Walk CPU     Avg:%10f Max:%10f Min:%10f\n",dWAvg,dWMax,dWMin);
	printf("Interact CPU Avg:%10f Max:%10f Min:%10f\n",dIAvg,dIMax,dIMin);
	if (msr->param.bEwald) printf("Ewald CPU    Avg:%10f Max:%10f Min:%10f\n",dEAvg,dEMax,dEMin);
	if (msr->nThreads > 1) {
	    printf("Particle Cache Statistics (average per processor):\n");
	    printf("    Accesses:    %10g\n",out.dpASum*iP);
	    printf("    Miss Ratio:  %10g\n",out.dpMSum*iP);
	    printf("    Min Ratio:   %10g\n",out.dpTSum*iP);
	    printf("    Coll Ratio:  %10g\n",out.dpCSum*iP);
	    printf("Cell Cache Statistics (average per processor):\n");
	    printf("    Accesses:    %10g\n",out.dcASum*iP);
	    printf("    Miss Ratio:  %10g\n",out.dcMSum*iP);
	    printf("    Min Ratio:   %10g\n",out.dcTSum*iP);
	    printf("    Coll Ratio:  %10g\n",out.dcCSum*iP);
	    printf("\n");
	    }
	}
    }


void msrCalcEandL(MSR msr,int bFirst,double dTime,double *E,double *T,
		  double *U,double *Eth,double L[])
    {
    struct outCalcEandL out;
    double a;
    int k;

    pstCalcEandL(msr->pst,NULL,0,&out,NULL);
    *T = out.T;
    *U = out.U;
    *Eth = out.Eth;
    for (k=0;k<3;k++) L[k] = out.L[k];
    /*
    ** Do the comoving coordinates stuff.
    ** Currently L is not adjusted for this. Should it be?
    */
    a = csmTime2Exp(msr->param.csm,dTime);
    if (!msr->param.bCannonical) *T *= pow(a,4.0);
    /*
     * Estimate integral (\dot a*U*dt) over the interval.
     * Note that this is equal to integral (W*da) and the latter
     * is more accurate when a is changing rapidly.
     */
    if (msr->param.csm->bComove && !bFirst) {
	msr->dEcosmo += 0.5*(a - csmTime2Exp(msr->param.csm, msr->dTimeOld))
	    *((*U) + msr->dUOld);
	}
    else {
	msr->dEcosmo = 0.0;
	}
    msr->dTimeOld = dTime;
    msr->dUOld = *U;
    *U *= a;
    *E = (*T) + (*U) - msr->dEcosmo + a*a*(*Eth);
    }


void msrDrift(MSR msr,double dTime,double dDelta)
    {
    struct inDrift in;
    int j;

    if (msr->param.bCannonical) {
	in.dDelta = csmComoveDriftFac(msr->param.csm,dTime,dDelta);
	}
    else {
	in.dDelta = dDelta;
	}
    for (j=0;j<3;++j) {
	in.fCenter[j] = msr->fCenter[j];
	}
    in.bPeriodic = msr->param.bPeriodic;
    in.fCentMass = msr->param.dCentMass;
    in.dTime = dTime;
    pstDrift(msr->pst,&in,sizeof(in),NULL,NULL);
    }

void msrDriftInactive(MSR msr,double dTime,double dDelta)
    {
    struct inDrift in;
    int j;

    if (msr->param.bCannonical) {
	in.dDelta = csmComoveDriftFac(msr->param.csm,dTime,dDelta);
	}
    else {
	in.dDelta = dDelta;
	}
    for (j=0;j<3;++j) {
	in.fCenter[j] = msr->fCenter[j];
	}
    in.dTime = dTime;
    in.bPeriodic = msr->param.bPeriodic;
    in.fCentMass = msr->param.dCentMass;
    pstDriftInactive(msr->pst,&in,sizeof(in),NULL,NULL);
    }

/*
 * For gasoline, updates predicted velocities to beginning of timestep.
 */
void msrKickKDKOpen(MSR msr,double dTime,double dDelta)
    {
    double H,a;
    struct inKick in;
    struct outKick out;
	
    if (msr->param.bCannonical) {
	in.dvFacOne = 1.0;		/* no hubble drag, man! */
	in.dvFacTwo = csmComoveKickFac(msr->param.csm,dTime,dDelta);
	}
    else {
	/*
	** Careful! For non-cannonical we want H and a at the 
	** HALF-STEP! This is a bit messy but has to be special
	** cased in some way.
	*/
	dTime += dDelta/2.0;
	a = csmTime2Exp(msr->param.csm,dTime);
	H = csmTime2Hub(msr->param.csm,dTime);
	in.dvFacOne = (1.0 - H*dDelta)/(1.0 + H*dDelta);
	in.dvFacTwo = dDelta/pow(a,3.0)/(1.0 + H*dDelta);
	}
    if (msr->param.bAntiGrav) {
	in.dvFacTwo = -in.dvFacTwo;
	in.dvPredFacTwo = -in.dvPredFacTwo;
	}
    pstKick(msr->pst,&in,sizeof(in),&out,NULL);
    if (msr->param.bVDetails)
	printf("KickOpen: Avg Wallclock %f, Max Wallclock %f\n",
	       out.SumTime/out.nSum,out.MaxTime);
    }

/*
 * For gasoline, updates predicted velocities to end of timestep.
 */
void msrKickKDKClose(MSR msr,double dTime,double dDelta)
    {
    double H,a;
    struct inKick in;
    struct outKick out;
	
    if (msr->param.bCannonical) {
	in.dvFacOne = 1.0; /* no hubble drag, man! */
	in.dvFacTwo = csmComoveKickFac(msr->param.csm,dTime,dDelta);
	}
    else {
	/*
	** Careful! For non-cannonical we want H and a at the 
	** HALF-STEP! This is a bit messy but has to be special
	** cased in some way.
	*/
	dTime += dDelta/2.0;
	a = csmTime2Exp(msr->param.csm,dTime);
	H = csmTime2Hub(msr->param.csm,dTime);
	in.dvFacOne = (1.0 - H*dDelta)/(1.0 + H*dDelta);
	in.dvFacTwo = dDelta/pow(a,3.0)/(1.0 + H*dDelta);
	}
    if (msr->param.bAntiGrav) {
	in.dvFacTwo = -in.dvFacTwo;
	in.dvPredFacTwo = -in.dvPredFacTwo;
	}
    pstKick(msr->pst,&in,sizeof(in),&out,NULL);
    if (msr->param.bVDetails)
	printf("KickClose: Avg Wallclock %f, Max Wallclock %f\n",
	       out.SumTime/out.nSum,out.MaxTime);
    }


int msrOutTime(MSR msr,double dTime)
    {	
    if (msr->iOut < msr->nOuts) {
	if (dTime >= msr->pdOutTime[msr->iOut]) {
	    ++msr->iOut;
	    return(1);
	    }
	else return(0);
	}
    else return(0);
    }


int cmpTime(const void *v1,const void *v2) 
    {
    double *d1 = (double *)v1;
    double *d2 = (double *)v2;

    if (*d1 < *d2) return(-1);
    else if (*d1 == *d2) return(0);
    else return(1);
    }

void msrReadOuts(MSR msr,double dTime)
    {
    char achFile[PST_FILENAME_SIZE];
    char ach[PST_FILENAME_SIZE];
    LCL *plcl = &msr->lcl;
    FILE *fp;
    int i,ret;
    double z,a,n;
    char achIn[80];
	
    /*
    ** Add Data Subpath for local and non-local names.
    */
    achFile[0] = 0;
    sprintf(achFile,"%s/%s.red",msr->param.achDataSubPath,
	    msr->param.achOutName);
    /*
    ** Add local Data Path.
    */
    if (plcl->pszDataPath) {
	strcpy(ach,achFile);
	sprintf(achFile,"%s/%s",plcl->pszDataPath,ach);
	}
    fp = fopen(achFile,"r");
    if (!fp) {
	msr->nOuts = 0;
	return;
	}
    i = 0;
    while (1) {
	if (!fgets(achIn,80,fp)) goto NoMoreOuts;
	switch (achIn[0]) {
	case 'z':
	    ret = sscanf(&achIn[1],"%lf",&z);
	    if (ret != 1) goto NoMoreOuts;
	    a = 1.0/(z+1.0);
	    msr->pdOutTime[i] = csmExp2Time(msr->param.csm,a);
	    break;
	case 'a':
	    ret = sscanf(&achIn[1],"%lf",&a);
	    if (ret != 1) goto NoMoreOuts;
	    msr->pdOutTime[i] = csmExp2Time(msr->param.csm,a);
	    break;
	case 't':
	    ret = sscanf(&achIn[1],"%lf",&msr->pdOutTime[i]);
	    if (ret != 1) goto NoMoreOuts;
	    break;
	case 'n':
	    ret = sscanf(&achIn[1],"%lf",&n);
	    if (ret != 1) goto NoMoreOuts;
	    msr->pdOutTime[i] = dTime + (n-0.5)*msrDelta(msr);
	    break;
	default:
	    ret = sscanf(achIn,"%lf",&z);
	    if (ret != 1) goto NoMoreOuts;
	    a = 1.0/(z+1.0);
	    msr->pdOutTime[i] = csmExp2Time(msr->param.csm,a);
	    }
	++i;
	if(i > msr->nMaxOuts) {
	    msr->nMaxOuts *= 2;
	    msr->pdOutTime = realloc(msr->pdOutTime,
				     msr->nMaxOuts*sizeof(double));
	    assert(msr->pdOutTime != NULL);
	    }
	}
    NoMoreOuts:
    msr->nOuts = i;
    /*
    ** Now sort the array of output times into ascending order.
    */
    qsort(msr->pdOutTime,msr->nOuts,sizeof(double),cmpTime);
    fclose(fp);
    }


int msrSteps(MSR msr)
    {
    return(msr->param.nSteps);
    }


char *msrOutName(MSR msr)
    {
    return(msr->param.achOutName);
    }


double msrDelta(MSR msr)
    {
    return(msr->param.dDelta);
    }


int msrLogInterval(MSR msr)
    {
    return(msr->param.iLogInterval);
    }


int msrOutInterval(MSR msr)
    {
    return(msr->param.iOutInterval);
    }


int msrComove(MSR msr)
    {
    return(msr->param.csm->bComove);
    }


double msrSoft(MSR msr)
    {
    return(msr->param.dSoft);
    }


void msrSwitchTheta(MSR msr,double dTime)
    {
    double a;

    a = csmTime2Exp(msr->param.csm,dTime);
    if (a >= msr->param.daSwitchTheta) msr->dCrit = msr->param.dTheta2; 
    }


void
msrInitStep(MSR msr) {
    struct inSetRung insr;
    struct inInitStep in;

    /*
    ** Here we can pass down all parameters of the simulation
    ** before any timestepping takes place. This should happen 
    ** just after the file has been read and the PKD structure
    ** initialized for each processor.
    */
    in.param = msr->param;
    pstInitStep(msr->pst, &in, sizeof(in), NULL, NULL);
    
    /*
    ** Initialize particles to lowest rung. (what for?)
    */
    insr.iRung = msr->param.iMaxRung - 1;
    pstSetRung(msr->pst, &insr, sizeof(insr), NULL, NULL);
    msr->iCurrMaxRung = insr.iRung;
    }


void
msrSetRung(MSR msr, int iRung)
    {
    struct inSetRung in;

    in.iRung = iRung;
    pstSetRung(msr->pst, &in, sizeof(in), NULL, NULL);
    msr->iCurrMaxRung = in.iRung;
    }


int msrMaxRung(MSR msr)
    {
    return msr->param.iMaxRung;
    }


int msrCurrMaxRung(MSR msr)
    {
    return msr->iCurrMaxRung;
    }


double msrEta(MSR msr)
    {
    return msr->param.dEta;
    }

/*
 * bGreater = 1 => activate all particles at this rung and greater.
 */
void msrActiveRung(MSR msr, int iRung, int bGreater)
    {
    struct inActiveRung in;

    in.iRung = iRung;
    in.bGreater = bGreater;
    pstActiveRung(msr->pst, &in, sizeof(in), &(msr->nActive), NULL);
    }

void msrActiveTypeOrder(MSR msr, unsigned int iTestMask )
    {
    struct inActiveTypeOrder in;
    int nActive;

    in.iTestMask = iTestMask;
    pstActiveTypeOrder(msr->pst,&in,sizeof(in),&nActive,NULL);

    if (iTestMask & TYPE_ACTIVE)       msr->nActive       = nActive;
    if (iTestMask & TYPE_SMOOTHACTIVE) msr->nSmoothActive = nActive;
    }

void msrActiveOrder(MSR msr)
    {
    pstActiveOrder(msr->pst,NULL,0,&(msr->nActive),NULL);
    }

void msrActiveExactType(MSR msr, unsigned int iFilterMask, unsigned int iTestMask, unsigned int iSetMask) 
    {
    struct inActiveType in;
    int nActive;

    in.iFilterMask = iFilterMask;
    in.iTestMask = iTestMask;
    in.iSetMask = iSetMask;

    pstActiveExactType(msr->pst,&in,sizeof(in),&nActive,NULL);

    if (iSetMask & TYPE_ACTIVE      ) msr->nActive       = nActive;
    if (iSetMask & TYPE_SMOOTHACTIVE) msr->nSmoothActive = nActive;
    }

void msrActiveType(MSR msr, unsigned int iTestMask, unsigned int iSetMask) 
    {
    struct inActiveType in;
    int nActive;

    in.iTestMask = iTestMask;
    in.iSetMask = iSetMask;

    pstActiveType(msr->pst,&in,sizeof(in),&nActive,NULL);

    if (iSetMask & TYPE_ACTIVE      ) msr->nActive       = nActive;
    if (iSetMask & TYPE_SMOOTHACTIVE) msr->nSmoothActive = nActive;
    }

void msrSetType(MSR msr, unsigned int iTestMask, unsigned int iSetMask) 
    {
    struct inActiveType in;
    int nActive;

    in.iTestMask = iTestMask;
    in.iSetMask = iSetMask;

    pstSetType(msr->pst,&in,sizeof(in),&nActive,NULL);
    }

void msrResetType(MSR msr, unsigned int iTestMask, unsigned int iSetMask) 
    {
    struct inActiveType in;
    int nActive;

    in.iTestMask = iTestMask;
    in.iSetMask = iSetMask;

    pstResetType(msr->pst,&in,sizeof(in),&nActive,NULL);

    if (msr->param.bVDetails) printf("nResetType: %d\n",nActive);
    }

int msrCountType(MSR msr, unsigned int iFilterMask, unsigned int iTestMask) 
    {
    struct inActiveType in;
    int nActive;

    in.iFilterMask = iFilterMask;
    in.iTestMask = iTestMask;

    pstCountType(msr->pst,&in,sizeof(in),&nActive,NULL);

    return nActive;
    }

void msrActiveMaskRung(MSR msr, unsigned int iSetMask, int iRung, int bGreater) 
    {
    struct inActiveType in;
    int nActive;

    in.iTestMask = (~0);
    in.iSetMask = iSetMask;
    in.iRung = iRung;
    in.bGreater = bGreater;

    pstActiveMaskRung(msr->pst,&in,sizeof(in),&nActive,NULL);

    if (iSetMask & TYPE_ACTIVE      ) msr->nActive       = nActive;
    if (iSetMask & TYPE_SMOOTHACTIVE) msr->nSmoothActive = nActive;
    }

void msrActiveTypeRung(MSR msr, unsigned int iTestMask, unsigned int iSetMask, int iRung, int bGreater) 
    {
    struct inActiveType in;
    int nActive;

    in.iTestMask = iTestMask;
    in.iSetMask = iSetMask;
    in.iRung = iRung;
    in.bGreater = bGreater;

    pstActiveTypeRung(msr->pst,&in,sizeof(in),&nActive,NULL);

    if (iSetMask & TYPE_ACTIVE      ) msr->nActive       = nActive;
    if (iSetMask & TYPE_SMOOTHACTIVE) msr->nSmoothActive = nActive;
    }

int msrCurrRung(MSR msr, int iRung)
    {
    struct inCurrRung in;
    struct outCurrRung out;

    in.iRung = iRung;
    pstCurrRung(msr->pst, &in, sizeof(in), &out, NULL);
    return out.iCurrent;
    }

void
msrGravStep(MSR msr,double dTime)
    {
    struct inGravStep in;
    double expand;

    in.dEta = msrEta(msr);
    expand = csmTime2Exp(msr->param.csm,dTime);
    in.dRhoFac = 1.0/(expand*expand*expand);
    pstGravStep(msr->pst,&in,sizeof(in),NULL,NULL);
    }

void
msrAccelStep(MSR msr,double dTime)
    {
    struct inAccelStep in;
    double a;

    in.dEta = msrEta(msr);
    a = csmTime2Exp(msr->param.csm,dTime);
    if (msr->param.bCannonical) {
	in.dVelFac = 1.0/(a*a);
	}
    else {
	in.dVelFac = 1.0;
	}
    in.dAccFac = 1.0/(a*a*a);
    in.bDoGravity = msrDoGravity(msr);
    in.bEpsAcc = msr->param.bEpsAccStep;
    in.bSqrtPhi = msr->param.bSqrtPhiStep;
    pstAccelStep(msr->pst,&in,sizeof(in),NULL,NULL);
    }

void
msrDensityStep(MSR msr,double dTime)
    {
    struct inDensityStep in;
    double expand;
    int bGasOnly,bSymmetric;

    if (msr->param.bVDetails) printf("Calculating Rung Densities...\n");
    bGasOnly = 0;
    bSymmetric = 0;
    msrSmooth(msr,dTime,SMX_DENSITY,bGasOnly,bSymmetric);
    in.dEta = msrEta(msr);
    expand = csmTime2Exp(msr->param.csm,dTime);
    in.dRhoFac = 1.0/(expand*expand*expand);
    pstDensityStep(msr->pst,&in,sizeof(in),NULL,NULL);
    }

void
msrInitDt(MSR msr)
    {
    struct inInitDt in;
    
    in.dDelta = msrDelta(msr);
    pstInitDt(msr->pst,&in,sizeof(in),NULL,NULL);
    }

void msrDtToRung(MSR msr, int iRung, double dDelta, int bAll)
    {
    struct inDtToRung in;
    struct outDtToRung out;
    
    in.iRung = iRung;
    in.dDelta = dDelta;
    in.iMaxRung = msrMaxRung(msr);
    in.bAll = bAll;

    pstDtToRung(msr->pst, &in, sizeof(in), &out, NULL);

    if (out.nMaxRung <= msr->param.nTruncateRung && out.iMaxRung > iRung) {
	if (msr->param.bVDetails) printf("n_CurrMaxRung = %d  (iCurrMaxRung = %d):  Promoting particles to iCurrMaxrung = %d\n",
					 out.nMaxRung,out.iMaxRung,out.iMaxRung-1);

	in.iMaxRung = out.iMaxRung; /* Note this is the forbidden rung so no -1 here */
	pstDtToRung(msr->pst, &in, sizeof(in), &out, NULL);
	}
    msr->iCurrMaxRung = out.iMaxRung;
    }


void msrRungStats(MSR msr)
    {
    if (msr->param.bVRungStat) {
	struct inRungStats in;
	struct outRungStats out;
	int i;

	printf("Rung distribution:\n");
	for (i=0;i<msr->param.iMaxRung;++i) {
	    in.iRung = i;
	    pstRungStats(msr->pst,&in,sizeof(in),&out,NULL);
	    msr->nRung[i] = out.nParticles;
	    printf("   rung:%d %d\n",i,out.nParticles);
	    }
	printf("\n");
	}
    }


void msrTopStepKDK(MSR msr,
		   double dStep,	/* Current step */
		   double dTime,	/* Current time */
		   double dDelta,	/* Time step */
		   int iRung,		/* Rung level */
		   int iKickRung,	/* Gravity on all rungs from iRung
					   to iKickRung */
		   int iAdjust,		/* Do an adjust? */
		   double *pdActiveSum,
		   double *pdWMax,
		   double *pdIMax,
		   double *pdEMax,
		   int *piSec)
    {
    double dMass = -1.0;
    int nActive;
    int bSplitVA;

    if(iAdjust && (iRung < msrMaxRung(msr)-1)) {
	if (msr->param.bVDetails) printf("Adjust, iRung: %d\n",iRung);
	msrActiveRung(msr, iRung, 1);
	msrActiveType(msr,TYPE_ALL,TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
	msrInitDt(msr);
	if (msr->param.bGravStep) {
	    msrGravStep(msr,dTime);
	    }
	if (msr->param.bAccelStep) {
	    msrAccelStep(msr,dTime);
	    }
	if (msr->param.bDensityStep) {
	    bSplitVA = 0;
	    msrDomainDecomp(msr,iRung,1,bSplitVA);
	    msrActiveRung(msr,iRung,1);
	    msrBuildTree(msr,dMass);
	    msrDensityStep(msr,dTime);
	    }
	msrDtToRung(msr,iRung,dDelta,1);
	if (iRung == 0) {
	    /*
	      msrReorder(msr);
	      msrOutArray(msr,"test.dt",OUT_DT_ARRAY);
	      msrActiveOrder(msr);
	    */
	    msrRungStats(msr);
	    }
	}
    if (msr->param.bVDetails) printf("msrKickOpen at iRung: %d 0.5*dDelta: %g\n",
				     iRung, 0.5*dDelta);
    msrActiveRung(msr,iRung,0);
    msrActiveType(msr,TYPE_ALL,TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE );
    msrKickKDKOpen(msr,dTime,0.5*dDelta);
    if ((msrCurrMaxRung(msr) > iRung) && (msr->param.nRungVeryActive > iRung)) {
	/*
	** Recurse.
	*/
	msrTopStepKDK(msr,dStep,dTime,0.5*dDelta,iRung+1,iRung+1,0,
		      pdActiveSum,pdWMax,pdIMax,pdEMax,piSec);
	dTime += 0.5*dDelta;
	dStep += 1.0/(2 << iRung);
	msrActiveRung(msr,iRung,0);
	msrTopStepKDK(msr,dStep,dTime,0.5*dDelta,iRung+1,iKickRung,1,
		      pdActiveSum,pdWMax,pdIMax,pdEMax,piSec);
	}
    else if(msrCurrMaxRung(msr) == iRung) {
	/* This Drifts everybody */
	if (msr->param.bVDetails) printf("Drift, iRung: %d -- we should never get here for VA set small enough\n", iRung);
	msrDrift(msr,dTime,dDelta);
	dTime += dDelta;
	dStep += 1.0/(1 << iRung);

	msrActiveMaskRung(msr,TYPE_ACTIVE,iKickRung,1);
	bSplitVA = 0;
	msrDomainDecomp(msr,iKickRung,1,bSplitVA);
	msrInitAccel(msr);

	if(msrDoGravity(msr)) {
	    msrActiveRung(msr,iKickRung,1);
	    msrUpdateSoft(msr,dTime);
	    msrActiveType(msr,TYPE_ALL,TYPE_TREEACTIVE);
	    if (msr->param.bVDetails)
		printf("Gravity, iRung: %d to %d\n", iRung, iKickRung);
	    msrBuildTree(msr,dMass);
	    msrGravity(msr,dStep,piSec,pdWMax,pdIMax,pdEMax,&nActive);
	    *pdActiveSum += (double)nActive/msr->N;
	    }
	}
    else {
	int nMaxRungVeryActive;
	double dDeltaTmp;
	int i;
	
	/*
	 * We have more rungs to go, but we've hit the very active limit.
	 */
	/*
	 * Determine VeryActive particles
	 */
	msrActiveMaskRung(msr, TYPE_VERYACTIVE, iRung+1, 1);
	/*
	 * Activate VeryActives
	 */
	msrActiveType(msr, TYPE_VERYACTIVE, TYPE_ACTIVE);
	/*
	 * Drift the non-VeryActive particles forward 1/2 timestep
	 */
	if (msr->param.bVDetails)
	    printf("InActiveDrift at iRung: %d, 0.5*dDelta: %g\n", iRung, 0.5*dDelta);
	msrDriftInactive(msr, dTime, 0.5*dDelta);
	/*
	 * Build a tree out of them for use by the VeryActives
	 */
	if(msrDoGravity(msr)) {
	    msrUpdateSoft(msr,dTime + 0.5*dDelta);
	    /*
	    ** Domain decomposition for parallel exclude very active is going to be 
	    ** placed here shortly.
	    */
	    bSplitVA = 1;
	    msrDomainDecomp(msr,iKickRung,1,bSplitVA);

	    if (msr->param.bVDetails)
		printf("Building exclude very active tree: iRung: %d\n", iRung);
	    msrBuildTreeExcludeVeryActive(msr,dMass);
	    }
	/*
	 * Perform timestepping on individual processors.
	 */
	if (msr->param.bVDetails)
	    printf("VeryActive at iRung: %d\n", iRung);
	msrStepVeryActiveKDK(msr, dStep, dTime, dDelta, iRung, &nMaxRungVeryActive);
	dTime += dDelta;
	dStep += 1.0/(1 << iRung);
	/*
	 * Move Inactives to the end of the step.
	 */
	if (msr->param.bVDetails)
	    printf("InActiveDrift at iRung: %d, 0.5*dDelta: %g\n", iRung, 0.5*dDelta);
	msrActiveType(msr, TYPE_VERYACTIVE, TYPE_ACTIVE);
	/* 
	** The inactives are half time step behind the actives. 
	** Move them a half time step ahead to synchronize everything again.
	*/
	msrDriftInactive(msr, dTime - 0.5*dDelta, 0.5*dDelta);

	/*
	 * Regular Tree gravity
	 */
	msrActiveMaskRung(msr,TYPE_ACTIVE,iKickRung,1);
	bSplitVA = 0;
	msrDomainDecomp(msr,iKickRung,1,bSplitVA);
	msrInitAccel(msr);

	if(msrDoGravity(msr)) {
	    msrActiveRung(msr,iKickRung,1);
	    msrUpdateSoft(msr,dTime);
	    msrActiveType(msr,TYPE_ALL,TYPE_TREEACTIVE);
	    if (msr->param.bVDetails)
		printf("Gravity, iRung: %d to %d\n", nMaxRungVeryActive, iKickRung);
	    msrBuildTree(msr,dMass);
	    msrGravity(msr,dStep,piSec,pdWMax,pdIMax,pdEMax,&nActive);
	    *pdActiveSum += (double)nActive/msr->N;
	    }
	dDeltaTmp = dDelta;
	for(i = nMaxRungVeryActive; i > iRung; i--)
	    dDeltaTmp *= 0.5;
	
	for(i = nMaxRungVeryActive; i > iRung; i--) { /* close off all
							 the VeryActive Kicks
						      */
	    if (msr->param.bVDetails) printf("VeryActive msrKickClose at iRung: %d, 0.5*dDelta: %g\n",
					     i, 0.5*dDeltaTmp);
	    msrActiveRung(msr,i,0);
	    msrKickKDKClose(msr,dTime,0.5*dDeltaTmp);
	    dDeltaTmp *= 2.0;
	    }
	}
    
    if (msr->param.bVDetails) printf("KickClose, iRung: %d, 0.5*dDelta: %g\n",
				     iRung, 0.5*dDelta);
    msrActiveRung(msr,iRung,0);
    msrKickKDKClose(msr,dTime,0.5*dDelta);
    }

void
msrStepVeryActiveKDK(MSR msr, double dStep, double dTime, double dDelta,
		     int iRung, int *pnMaxRung)
    {
    struct inStepVeryActive in;
    struct outStepVeryActive out;
    
    in.dStep = dStep;
    in.dTime = dTime;
    in.dDelta = dDelta;
    in.iRung = iRung;
    in.nMaxRung = *pnMaxRung;
    in.diCrit2 = 1/(msr->dCrit*msr->dCrit);   /* could set a stricter opening criterion here */
    in.param = msr->param;
    in.csm = *msr->param.csm;
    
    /*
     * Start Particle Cache on all nodes (could be done as part of
     * tree build)
     */
    pstROParticleCache(msr->pst, NULL, 0, NULL, NULL);
    
    pstStepVeryActiveKDK(msr->pst, &in, sizeof(in), &out, NULL);
    /*
     * Finish Particle Cache on all nodes
     */
    pstParticleCacheFinish(msr->pst, NULL, 0, NULL, NULL);
    *pnMaxRung = out.nMaxRung;
    }

int
msrMaxOrder(MSR msr)
    {
    return msr->nMaxOrder;
    }

void
msrAddDelParticles(MSR msr)
    {
    struct outColNParts *pColNParts;
    int *pNewOrder;
    struct inSetNParts in;
    struct inSetParticleTypes intype;
    int iOut;
    int i;
    
    if (msr->param.bVDetails) printf("Changing Particle number\n");
    pColNParts = malloc(msr->nThreads*sizeof(*pColNParts));
    pstColNParts(msr->pst, NULL, 0, pColNParts, &iOut);
    /*
     * Assign starting numbers for new particles in each processor.
     */
    pNewOrder = malloc(msr->nThreads*sizeof(*pNewOrder));
    for(i=0;i<msr->nThreads;i++) {
	/*
	 * Detect any changes in particle number, and force a tree
	 * build.
	 */
	if (pColNParts[i].nNew != 0 || pColNParts[i].nDeltaGas != 0 ||
	    pColNParts[i].nDeltaDark != 0 || pColNParts[i].nDeltaStar != 0) {
	    printf("Particle assignments have changed!\n");
	    printf("need to rebuild tree, code in msrAddDelParticles()\n");
	    printf("needs to be updated. Bailing out for now...\n");
	    exit(-1);
	    }
	pNewOrder[i] = msr->nMaxOrder + 1;
	msr->nMaxOrder += pColNParts[i].nNew;
	msr->nGas += pColNParts[i].nDeltaGas;
	msr->nDark += pColNParts[i].nDeltaDark;
	msr->nStar += pColNParts[i].nDeltaStar;
	}
    msr->N = msr->nGas + msr->nDark + msr->nStar;

    msr->nMaxOrderDark = msr->nMaxOrder;

    pstNewOrder(msr->pst,pNewOrder,sizeof(*pNewOrder)*msr->nThreads,NULL,NULL);

    if (msr->param.bVDetails)
	printf("New numbers of particles: %d gas %d dark %d star\n",
	       msr->nGas, msr->nDark, msr->nStar);

    in.nGas = msr->nGas;
    in.nDark = msr->nDark;
    in.nStar = msr->nStar;
    in.nMaxOrderGas = msr->nMaxOrderGas;
    in.nMaxOrderDark = msr->nMaxOrderDark;
    pstSetNParts(msr->pst,&in,sizeof(in),NULL,NULL);
    pstSetParticleTypes(msr->pst,&intype,sizeof(intype),NULL,NULL);

    free(pNewOrder);
    free(pColNParts);
    }

int msrDoDensity(MSR msr)
    {
    return(msr->param.bDoDensity);
    }

int msrDoGravity(MSR msr)
    {
    return(msr->param.bDoGravity);
    }

void msrInitAccel(MSR msr)
    {
    pstInitAccel(msr->pst,NULL,0,NULL,NULL);
    }

void msrInitTimeSteps(MSR msr,double dTime,double dDelta) 
    {
    double dMass = -1.0;

    msrActiveType(msr,TYPE_ALL,TYPE_ACTIVE|TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
    msrInitDt(msr);
    if (msr->param.bGravStep) {
	msrGravStep(msr,dTime);
	}
    if (msr->param.bAccelStep) {
	msrAccelStep(msr,dTime);
	}
    if (msr->param.bDensityStep) {
	msrDomainDecomp(msr,0,1,0);
	msrActiveType(msr,TYPE_ALL,TYPE_ACTIVE|TYPE_TREEACTIVE|TYPE_SMOOTHACTIVE);
	msrBuildTree(msr,dMass);
	msrDensityStep(msr,dTime);
	}
    msrDtToRung(msr,0,dDelta,1);
    msrRungStats(msr);
    }


void msrFof(MSR msr,int nFOFsDone,int iSmoothType,int bSymmetric)
    {
    struct inFof in;
    in.nFOFsDone = nFOFsDone;
    in.nSmooth = msr->param.nSmooth;
    in.bPeriodic = msr->param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    in.smf.dTau2 = msr->param.dTau * msr->param.dTau;
    if(msr->param.bTauAbs==0) in.smf.dTau2 *= pow(msr->param.csm->dOmega0,-0.6666);
    in.smf.bTauAbs = msr->param.bTauAbs;
    in.smf.nMinMembers = msr->param.nMinMembers;
    in.smf.fContrast = msr->param.fContrast;
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("Doing FOF...\n");
	sec = msrTime();
	pstFof(msr->pst,&in,sizeof(in),NULL,NULL);
	dsec = msrTime() - sec;
	printf("FOF Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstFof(msr->pst,&in,sizeof(in),NULL,NULL);
	}
    }

void msrGroupMerge(MSR msr)
    {
    struct inGroupMerge in;
    int nGroups;
    in.bPeriodic = msr->param.bPeriodic;
    in.smf.nMinMembers = msr->param.nMinMembers;
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("Doing GroupMerge...\n");
	sec = msrTime();
	pstGroupMerge(msr->pst,&in,sizeof(in),&nGroups,NULL);
	dsec = msrTime() - sec;
	printf("GroupMerge done, Wallclock: %f secs\n",dsec);
	}
    else {
	pstGroupMerge(msr->pst,&in,sizeof(in),&nGroups,NULL);
	}
    msr->nGroups = nGroups;
    printf("MASTER: TOTAL groups: %i \n" ,nGroups);
    }

void msrGroupProfiles(MSR msr,int nFOFsDone,int iSmoothType,int bSymmetric)
    {
    int nBins;
    struct inGroupProfiles in;
    in.bPeriodic = msr->param.bPeriodic;
    in.nTotalGroups = msr->nGroups;
    in.nSmooth = msr->param.nSmooth;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    in.nFOFsDone = nFOFsDone;
    in.smf.nMinMembers = msr->param.nMinMembers;
    in.smf.nBins = msr->param.nBins;
    in.smf.nMinProfile = msr->param.nMinProfile/pow(msr->param.fBinsRescale, nFOFsDone);	
    in.bLogBins = msr->param.bLogBins;
    in.smf.bUsePotmin = msr->param.bUsePotmin;
    in.smf.Delta = msr->param.Delta;
    in.smf.binFactor = msr->param.binFactor;
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("Doing GroupProfiles...\n");
	sec = msrTime();
	pstGroupProfiles(msr->pst,&in,sizeof(in),&nBins,NULL);
	dsec = msrTime() - sec;
	printf("GroupProfiles done, Wallclock: %f secs\n",dsec);
	}
    else {
	pstGroupProfiles(msr->pst,&in,sizeof(in),&nBins,NULL);
	}
    msr->nBins = nBins;
    printf("MASTER: TOTAL bins: %i TOTAL groups: %i \n" ,nBins,msr->nGroups);
    }

void msrOutGroups(MSR msr,char *pszFile,int iOutType, double dTime){

    struct inOutArray in;
    char achOutFile[PST_FILENAME_SIZE];
    LCL *plcl;
    PST pst0;
    FILE *fp;
    double dvFac,time;
   
    pst0 = msr->pst;
    while(pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;
    if (pszFile) {
	/*
	** Add Data Subpath for local and non-local names.
	*/
	_msrMakePath(msr->param.achDataSubPath,pszFile,in.achOutFile);
	/*
	** Add local Data Path.
	*/
	_msrMakePath(plcl->pszDataPath,in.achOutFile,achOutFile);

	fp = fopen(achOutFile,"w");
	if (!fp) {
	    printf("Could not open Group Output File:%s\n",achOutFile);
	    _msrExit(msr,1);
	    }
	}
    else {
	printf("No Group Output File specified\n");
	_msrExit(msr,1);
	return;
	}
    if (msrComove(msr)) {
	time = csmTime2Exp(msr->param.csm,dTime);
	if (msr->param.bCannonical) {
	    dvFac = 1.0/(time*time);
	    }
	else {
	    dvFac = 1.0;
	    }
	}
    else {
	time = dTime;
	dvFac = 1.0;
	}

    if(iOutType == OUT_GROUP_TIPSY_NAT || iOutType == OUT_GROUP_TIPSY_STD){
	/*
	** Write tipsy header.
	*/
	struct dump h;
	h.nbodies = msr->nGroups;
	h.ndark = 0;
	h.nsph = 0;
	h.nstar = msr->nGroups;
	h.time = time;
	h.ndim = 3;

	if (msrComove(msr)) {
	    printf("Writing file...\nTime:%g Redshift:%g\n",
		   dTime,(1.0/h.time - 1.0));
	    }
	else {
	    printf("Writing file...\nTime:%g\n",dTime);
	    }
		
	if (iOutType == OUT_GROUP_TIPSY_STD) {
	    XDR xdrs;
	    xdrstdio_create(&xdrs,fp,XDR_ENCODE);
	    xdrHeader(&xdrs,&h);
	    xdr_destroy(&xdrs);
	    }
	else {
	    fwrite(&h,sizeof(struct dump),1,fp);
	    }	
	}	
    fclose(fp);	
    /* 
     * Write the groups.
     */
    assert(msr->pMap[0] == 0);
    pkdOutGroup(plcl->pkd,achOutFile,iOutType,0,dvFac);
    }

void msrDeleteGroups(MSR msr){

    LCL *plcl;
    PST pst0;
   
    pst0 = msr->pst;
    while(pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;

    if(plcl->pkd->groupData)free(plcl->pkd->groupData); 
    if(plcl->pkd->groupBin)free(plcl->pkd->groupBin);
    plcl->pkd->nBins = 0;
    plcl->pkd->nGroups = 0;
    }
#ifdef RELAXATION
void msrInitRelaxation(MSR msr)
    {
    pstInitRelaxation(msr->pst,NULL,0,NULL,NULL);
    }	
void msrRelaxation(MSR msr,double dTime,double deltaT,int iSmoothType,int bSymmetric)
    {
    struct inSmooth in;
    in.nSmooth = msr->param.nSmooth;
    in.bPeriodic = msr->param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    in.dfBall2OverSoft2 = (msr->param.bLowerSoundSpeed ? 0 :
			   4.0*msr->param.dhMinOverSoft*msr->param.dhMinOverSoft);
    if (msrComove(msr)) {
	in.smf.H = csmTime2Hub(msr->param.csm,dTime);
	in.smf.a = csmTime2Exp(msr->param.csm,dTime);
	}
    else {
	in.smf.H = 0.0;
	in.smf.a = 1.0;
	}
    in.smf.dDeltaT = deltaT;
    if (msr->param.bVStep) {
	double sec,dsec;
	printf("Smoothing for relaxation...dDeltaT = %f \n",deltaT);
	sec = msrTime();
	pstSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	dsec = msrTime() - sec;
	printf("Relaxation Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstSmooth(msr->pst,&in,sizeof(in),NULL,NULL);
	}
    }
#endif /* RELAXATION */
