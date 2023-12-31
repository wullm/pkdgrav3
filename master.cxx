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

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "pkd_config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h> /* for unlink() */
#endif
#ifdef _MSC_VER
#define mkdir _mkdir
#define unlink _unlink
#endif
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#else
#define PRIu64 "llu"
#endif
#include <limits.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <math.h>
#if defined(HAVE_WORDEXP) && defined(HAVE_WORDFREE)
#include <wordexp.h>
#elif defined(HAVE_GLOB) && defined(HAVE_GLOBFREE)
#include <glob.h>
#endif
#include <sys/stat.h>
#include <algorithm>
// #include <functional>
#include <fstream>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h> /* for MAXHOSTNAMELEN, if available */
#endif
#include "fmt/format.h"  // This will be part of c++20
#include "fmt/ostream.h"
using namespace fmt::literals; // Gives us ""_a and ""_format literals


#include "master.h"
#include "core/illinois.h"
#include "io/outtype.h"
#include "smooth/smoothfcn.h"
#include "io/fio.h"

#include "core/setadd.h"
#include "core/swapall.h"
#include "core/hostname.h"
#include "core/calcroot.h"
#include "core/select.h"

#include "domains/distribtoptree.h"
#include "domains/distribroot.h"
#include "domains/dumptrees.h"
#include "domains/olddd.h"

#include "gravity/setsoft.h"
#include "gravity/activerung.h"
#include "gravity/countrungs.h"
#include "gravity/zeronewrung.h"

#define LOCKFILE ".lockfile"	/* for safety lock */
#define STOPFILE "STOP"			/* for user interrupt */
#define CHECKFILE "CHECKPOINT"		/* for user interrupt */

void MSR::msrprintf(const char *Format, ... ) const {
    va_list ap;
    if (bVDetails) {
	va_start(ap,Format);
	vprintf(Format,ap);
	va_end(ap);
	}
    }

#ifdef _MSC_VER
double MSR::Time() {
    FILETIME ft;
    uint64_t clock;
    GetSystemTimeAsFileTime(&ft);
    clock = ft.dwHighDateTime;
    clock <<= 32;
    clock |= ft.dwLowDateTime;
    /* clock is in 100 nano-second units */
    return clock / 10000000.0;
    }
#else
double MSR::Time() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (tv.tv_sec+(tv.tv_usec*1e-6));
    }
#endif

void MSR::Leader(void) {
    puts("pkdgrav" PACKAGE_VERSION " Joachim Stadel & Doug Potter Sept 2015");
    puts("USAGE: pkdgrav3 [SETTINGS | FLAGS] [SIM_FILE]");
    puts("SIM_FILE: Configuration file of a particular simulation, which");
    puts("          includes desired settings and relevant input and");
    puts("          output files. Settings specified in this file override");
    puts("          the default settings.");
    puts("SETTINGS");
    puts("or FLAGS: Command line settings or flags for a simulation which");
    puts("          will override any defaults and any settings or flags");
    puts("          specified in the SIM_FILE.");
    }
void MSR::Trailer(void) {
    puts("(see man page for more information)");
    }

void MSR::Exit(int status) {
    exit(status);
    }

std::string MSR::BuildName(const char *path,int iStep,const char *type) {
    if (!path[0]) path = "{name}.{step:05d}{type}";
    return fmt::format(path,"name"_a=OutName(),"step"_a=iStep,"type"_a=type);
    }
std::string MSR::BuildName(int iStep,const char *type) {
    return BuildName(param.achOutPath,iStep,type);
    }
std::string MSR::BuildIoName(int iStep,const char *type) {
    if ( param.achIoPath[0] )
	return BuildName(param.achIoPath,iStep,type);
    else return BuildName(iStep,type);
    }
std::string MSR::BuildCpName(int iStep,const char *type) {
    if ( param.achCheckpointPath[0] )
	return BuildName(param.achCheckpointPath,iStep,type);
    else return BuildName(iStep,type);
    }

size_t MSR::getLocalGridMemory(int nGrid) {
    struct inGetFFTMaxSizes inFFTSizes;
    struct outGetFFTMaxSizes outFFTSizes;
    inFFTSizes.nx = inFFTSizes.ny = inFFTSizes.nz = nGrid;
    pstGetFFTMaxSizes(pst,&inFFTSizes,sizeof(inFFTSizes),&outFFTSizes,sizeof(outFFTSizes));
    return outFFTSizes.nMaxLocal*sizeof(FFTW3(real));
    }

void MSR::MakePath(const char *dir,const char *base,char *path) {
    /*
    ** Prepends "dir" to "base" and returns the result in "path". It is the
    ** caller's responsibility to ensure enough memory has been allocated
    ** for "path".
    */

    if (!path) return;
    path[0] = 0;
    if (dir&&dir[0]) {
	strcat(path,dir);
	strcat(path,"/");
	}
    if (!base) return;
    strcat(path,base);
    }

uint64_t MSR::getMemoryModel() {
    uint64_t mMemoryModel = 0;
    /*
    ** Figure out what memory models are in effect.  Configuration flags
    ** can be used to request a specific model, but certain operations
    ** will force these flags to be on.
    */
    if (param.bFindGroups) mMemoryModel |= PKD_MODEL_GROUPS|PKD_MODEL_VELOCITY;
    if (DoGravity()) {
	mMemoryModel |= PKD_MODEL_VELOCITY|PKD_MODEL_NODE_MOMENT;
	if (!param.bNewKDK) mMemoryModel |= PKD_MODEL_ACCELERATION;
	}
    if (param.bDoDensity)       mMemoryModel |= PKD_MODEL_DENSITY;
    if (param.bMemIntegerPosition) mMemoryModel |= PKD_MODEL_INTEGER_POS;
    if (param.bMemUnordered)    mMemoryModel |= PKD_MODEL_UNORDERED;
    if (param.bMemParticleID)   mMemoryModel |= PKD_MODEL_PARTICLE_ID;
    if (param.bTraceRelaxation) mMemoryModel |= PKD_MODEL_RELAXATION;
    if (param.bMemAcceleration || param.bDoAccOutput) mMemoryModel |= PKD_MODEL_ACCELERATION;
    if (param.bMemVelocity)     mMemoryModel |= PKD_MODEL_VELOCITY;
    if (param.bMemPotential || param.bDoPotOutput)    mMemoryModel |= PKD_MODEL_POTENTIAL;
    if (param.bFindHopGroups)   mMemoryModel |= PKD_MODEL_GROUPS | PKD_MODEL_DENSITY | PKD_MODEL_BALL;
    if (param.bMemGroups)       mMemoryModel |= PKD_MODEL_GROUPS;
    if (param.bMemMass)         mMemoryModel |= PKD_MODEL_MASS;
    if (param.bMemSoft)         mMemoryModel |= PKD_MODEL_SOFTENING;
    if (param.bMemRelaxation)   mMemoryModel |= PKD_MODEL_RELAXATION;
    if (param.bMemVelSmooth)    mMemoryModel |= PKD_MODEL_VELSMOOTH;

    if (param.bMemNodeAcceleration) mMemoryModel |= PKD_MODEL_NODE_ACCEL;
    if (param.bMemNodeVelocity)     mMemoryModel |= PKD_MODEL_NODE_VEL;
    if (param.bMemNodeMoment)       mMemoryModel |= PKD_MODEL_NODE_MOMENT;
    if (param.bMemNodeSphBounds)    mMemoryModel |= PKD_MODEL_NODE_SPHBNDS;

    if (param.bMemNodeBnd)          mMemoryModel |= PKD_MODEL_NODE_BND;
    if (param.bMemNodeVBnd)         mMemoryModel |= PKD_MODEL_NODE_VBND;

    return mMemoryModel;
    }

void MSR::InitializePStore(uint64_t *nSpecies,uint64_t mMemoryModel) {
    struct inInitializePStore ps;
    double dStorageAmount = (1.0+param.dExtraStore);
    int i;
    for( i=0; i<FIO_SPECIES_LAST; ++i) ps.nSpecies[i] = nSpecies[i];
    ps.nStore = ceil( dStorageAmount * ps.nSpecies[FIO_SPECIES_ALL] / mdlThreads(mdl));
    ps.nTreeBitsLo = param.nTreeBitsLo;
    ps.nTreeBitsHi = param.nTreeBitsHi;
    ps.iCacheSize  = param.iCacheSize;
    ps.iWorkQueueSize  = param.iWorkQueueSize;
    ps.iCUDAQueueSize  = param.iCUDAQueueSize;
    ps.fPeriod[0] = param.dxPeriod;
    ps.fPeriod[1] = param.dyPeriod;
    ps.fPeriod[2] = param.dzPeriod;
    ps.mMemoryModel = mMemoryModel | PKD_MODEL_VELOCITY;
    ps.bLightCone  = param.bLightCone;
    ps.bLightConeParticles  = param.bLightConeParticles;    

#define SHOW(m) ((ps.mMemoryModel&PKD_MODEL_##m)?" " #m:"")
       printf("Memory Models:%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n", 
	   param.bMemIntegerPosition ? " INTEGER_POSITION" : " DOUBLE_POSITION",
	   SHOW(UNORDERED),SHOW(VELOCITY),SHOW(ACCELERATION),SHOW(POTENTIAL),
	   SHOW(GROUPS),SHOW(RELAXATION),SHOW(MASS),SHOW(DENSITY),
	   SHOW(BALL),SHOW(SOFTENING),SHOW(VELSMOOTH),SHOW(SPH),
	   SHOW(STAR),SHOW(PARTICLE_ID));
#undef SHOW
    ps.nMinEphemeral = 0;
    ps.nMinTotalStore = 0;

    /* Various features require more or less ephemeral storage */
    ps.nEphemeralBytes = 0;
    if (param.iFofInterval   && ps.nEphemeralBytes < 4) ps.nEphemeralBytes = 4;
    if (param.bFindHopGroups && ps.nEphemeralBytes < 8) ps.nEphemeralBytes = 8;
    if (param.iPkInterval    && ps.nEphemeralBytes < 4) ps.nEphemeralBytes = 4;
    if (param.bGravStep      && ps.nEphemeralBytes < 8) ps.nEphemeralBytes = 8;
    if (param.bDoGas         && ps.nEphemeralBytes < 8) ps.nEphemeralBytes = 8;
    if (param.bDoDensity     && ps.nEphemeralBytes < 12) ps.nEphemeralBytes = 12;
#ifdef MDL_FFTW
    if (param.nGridPk>0) {
	struct inGetFFTMaxSizes inFFTSizes;
	struct outGetFFTMaxSizes outFFTSizes;
	inFFTSizes.nx = inFFTSizes.ny = inFFTSizes.nz = param.nGridPk;
	pstGetFFTMaxSizes(pst,&inFFTSizes,sizeof(inFFTSizes),&outFFTSizes,sizeof(outFFTSizes));
	/* The new MeasurePk requires two FFTs to eliminate aliasing */
	ps.nMinEphemeral = (param.bPkInterlace?2:1)*outFFTSizes.nMaxLocal*sizeof(FFTW3(real));
	}
    /* 
     * Add some ephemeral memory (if needed) for the linGrid.
     * 3 grids are stored : forceX, forceY, forceZ
     */
    if (strlen(param.achLinearSpecies)){
	struct inGetFFTMaxSizes inFFTSizes;
	struct outGetFFTMaxSizes outFFTSizes;

	inFFTSizes.nx = inFFTSizes.ny = inFFTSizes.nz = param.nGridLin;
	pstGetFFTMaxSizes(pst, &inFFTSizes,sizeof(inFFTSizes), &outFFTSizes, sizeof(outFFTSizes));

	if (ps.nMinEphemeral < 3*outFFTSizes.nMaxLocal*sizeof(FFTW3(real)))
	    ps.nMinEphemeral = 3*outFFTSizes.nMaxLocal*sizeof(FFTW3(real));
	}

    int nGrid = param.nGrid;
    if (nGrid>0) {
	struct inGetFFTMaxSizes inFFTSizes;
	struct outGetFFTMaxSizes outFFTSizes;
	inFFTSizes.nx = inFFTSizes.ny = inFFTSizes.nz = nGrid;
	pstGetFFTMaxSizes(pst,&inFFTSizes,sizeof(inFFTSizes),&outFFTSizes,sizeof(outFFTSizes));
	ps.nMinTotalStore = 10*outFFTSizes.nMaxLocal*sizeof(FFTW3(real));
	}
#endif
    // Check all registered Python analysis routines and account for their memory requirements
    for( msr_analysis_callback & i : analysis_callbacks) {
	auto attr_per_node = PyObject_GetAttrString(i.memory,"bytes_per_node");
	auto attr_per_part = PyObject_GetAttrString(i.memory,"bytes_per_particle");
	auto per_node = PyLong_AsSize_t(attr_per_node);
	auto per_part = PyLong_AsSize_t(attr_per_part);
	if (ps.nEphemeralBytes < per_part) ps.nEphemeralBytes = per_part;
	if (ps.nMinEphemeral < per_node) ps.nMinEphemeral = per_node;
	Py_DECREF(attr_per_node);
	Py_DECREF(attr_per_part);
	}
    pstInitializePStore(pst,&ps,sizeof(ps),NULL,0);
    PKD pkd = pst->plcl->pkd;
    printf("Allocated %lu MB for particle store on each processor.\n",
	pkdParticleMemory(pkd)/(1024*1024));
    printf("Particles: %lu bytes (persistent) + %d bytes (ephemeral), Nodes: %lu bytes\n",
	pkdParticleSize(pkd),ps.nEphemeralBytes,pkdNodeSize(pkd));
    if (ps.nMinEphemeral)
	printf("Ephemeral will be at least %" PRIu64 " MB per node.\n",ps.nMinEphemeral/(1024*1024));
    }

void MSR::Restart(int n, const char *baseName, int iStep, int nSteps, double dTime, double dDelta) {
    auto sec = MSR::Time();

    if (mdlThreads(mdl) != n) {
	fprintf(stderr,"ERROR: You must restart a checkpoint with the same number of threads\n");
	fprintf(stderr,"       nThreads=%d, nCheckpointThreads=%d\n",mdlThreads(mdl),n);
	fprintf(stderr,"       RESTART WITH %d THREADS\n",n);
	Exit(1);
	}

    bVDetails = getParameterBoolean("bVDetails");
    if (param.bVStart)
	printf("Restoring from checkpoint\n");

    nMaxOrder = N;

    uint64_t nSpecies[FIO_SPECIES_LAST];
    for( auto i=0; i<FIO_SPECIES_LAST; ++i) nSpecies[i] = 0;
    nSpecies[FIO_SPECIES_ALL]  = N;
    nSpecies[FIO_SPECIES_SPH]  = nGas;
    nSpecies[FIO_SPECIES_DARK] = nDark;
    nSpecies[FIO_SPECIES_STAR] = nStar;
    InitializePStore(nSpecies,getMemoryModel());

    struct inRestore restore;
    restore.nProcessors = param.bParaRead==0?1:(param.nParaRead<=1 ? nThreads:param.nParaRead);
    strcpy(restore.achInFile,baseName);
    pstRestore(pst,&restore,sizeof(restore),NULL,0);
    pstSetClasses(pst,aCheckpointClasses,nCheckpointClasses*sizeof(PARTCLASS),NULL,0);
    CalcBound();
    CountRungs(NULL);

    auto dsec = MSR::Time() - sec;
    double dExp = csmTime2Exp(csm,dTime);
    msrprintf("Checkpoint Restart Complete @ a=%g, Wallclock: %f secs\n\n",dExp,dsec);

    /* We can indicate that the DD was already done at rung 0 */
    iLastRungRT = 0;
    iLastRungDD = 0;

    InitCosmology();
    if (prmSpecified(prm,"dSoft")) SetSoft(Soft());
    Simulate(dTime,dDelta,iStep,nSteps);
    }

void MSR::writeParameters(const char *baseName,int iStep,int nSteps,double dTime,double dDelta) {
    char *p, achOutName[PST_FILENAME_SIZE];
    uint64_t nSpecies[FIO_SPECIES_LAST];
    int i;
    int nBytes;

    nBytes = pstGetClasses(pst,NULL,0,aCheckpointClasses,PKD_MAX_CLASSES*sizeof(PARTCLASS));
    nCheckpointClasses = nBytes / sizeof(PARTCLASS);
    assert(nCheckpointClasses*sizeof(PARTCLASS)==nBytes);

    strcpy( achOutName, baseName );
    p = strstr( achOutName, "&I" );
    if ( p ) {
	int n = p - achOutName;
	strcpy( p, "chk" );
	strcat( p, baseName + n + 2 );
	}
    else {
	strcat(achOutName,".chk");
	}

    FILE *fp = fopen(achOutName,"w");
    if (fp==NULL) {perror(achOutName); abort(); }

    for(i=0; i<FIO_SPECIES_LAST; ++i) nSpecies[i] = 0;
    nSpecies[FIO_SPECIES_ALL]  = N;
    nSpecies[FIO_SPECIES_SPH]  = nGas;
    nSpecies[FIO_SPECIES_DARK] = nDark;
    nSpecies[FIO_SPECIES_STAR] = nStar;

    fprintf(fp,"%s\n%s\n%s",
	"from MASTER import MSR",
	"from argparse import Namespace",
	"arguments=");
    PyObject_Print(arguments,fp,0);
    fprintf(fp,"\n%s","specified=");
    PyObject_Print(specified,fp,0);
    fprintf(fp,"\nspecies=[ ");
    for(i=0; i<FIO_SPECIES_LAST; ++i) fprintf(fp,"%" PRIu64 ",",nSpecies[i]);
    fprintf(fp," ]\n");
    fprintf(fp,"classes=[ ");
    for(i=0; i<nCheckpointClasses; ++i) {
	fprintf(fp, "[%d,%.17g,%.17g], ", aCheckpointClasses[i].eSpecies,
		aCheckpointClasses[i].fMass, aCheckpointClasses[i].fSoft);
	}
    fprintf(fp," ]\n");
    fprintf(fp,"msr=MSR()\n");
    fprintf(fp,"msr.Restart(arguments=arguments, specified=specified, species=species, classes=classes,\n"
    	       "            n=%d,name='%s',step=%d,steps=%d,time=%.17g,delta=%.17g,\n"
    	       "            E=%.17g,U=%.17g,Utime=%.17g)\n",
    	       mdlThreads(mdl),baseName,iStep,nSteps,dTime,dDelta,dEcosmo,dUOld,dTimeOld);

    fclose(fp);
    }

void MSR::Checkpoint(int iStep,int nSteps,double dTime,double dDelta) {
    struct inWrite in;
    double sec,dsec;

    auto filename = BuildCpName(iStep,".chk");
    assert(filename.size() < sizeof(in.achOutFile));
    strcpy(in.achOutFile,filename.c_str());
    in.nProcessors = param.bParaWrite==0?1:(param.nParaWrite<=1 ? nThreads:param.nParaWrite);
    if (csm->val.bComove) {
	double dExp = csmTime2Exp(csm,dTime);
	msrprintf("Writing checkpoint for Step: %d Time:%g Redshift:%g\n",
	    iStep,dTime,(1.0/dExp - 1.0));
	}
    else
	msrprintf("Writing checkpoint for Step: %d Time:%g\n",iStep,dTime);
    sec = MSR::Time();

    SaveParameters();
    writeParameters(in.achOutFile,iStep,nSteps,dTime,dDelta);

    pstCheckpoint(pst,&in,sizeof(in),NULL,0);

    /* This is not necessary, but it means the bounds will be identical upon restore */
    CalcBound();

    dsec = MSR::Time() - sec;
    msrprintf("Checkpoint has been successfully written, Wallclock: %f secs.\n", dsec);
    }

void MSR::Initialize() {
    char ach[256];

    lcl.pkd = NULL;
    nThreads = mdlThreads(mdl);
    lStart=time(0);
    fCenter = 0; // Center is at (0,0,0)
    /* Storage for output times*/
    dOutTimes.reserve(100); // Reasonable number
    dOutTimes.push_back(INFINITY); // Sentinal node
    iOut = 0;

    iCurrMaxRung = 0;
    iRungDD = 0;
    iRungDT = 0;
    iLastRungRT = -1;
    iLastRungDD = -1;  /* Domain decomposition is not done */
    nRung.resize(MAX_RUNG+1,0);
    csmInitialize(&csm);
    /*
    ** Now setup for the input parameters.
    */
    prmInitialize(&prm,MSR::Leader,MSR::Trailer);
    param.bOverwrite = 0;
    prmAddParam(prm,"bOverwrite",0,&param.bOverwrite,sizeof(int),
		"overwrite","enable/disable overwrite safety lock = -overwrite");
    param.bVWarnings = 1;
    prmAddParam(prm,"bVWarnings",0,&param.bVWarnings,sizeof(int),
		"vwarnings","enable/disable warnings = +vwarnings");
    param.bVStart = 1;
    prmAddParam(prm,"bVStart",0,&param.bVStart,sizeof(int),
		"vstart","enable/disable verbose start = +vstart");
    param.bVStep = 1;
    prmAddParam(prm,"bVStep",0,&param.bVStep,sizeof(int),
		"vstep","enable/disable verbose step = +vstep");
    param.bVRungStat = 1;
    prmAddParam(prm,"bVRungStat",0,&param.bVRungStat,sizeof(int),
		"vrungstat","enable/disable rung statistics = +vrungstat");
    param.bVDetails = 0;
    prmAddParam(prm,"bVDetails",0,&param.bVDetails,sizeof(int),
		"vdetails","enable/disable verbose details = +vdetails");
    param.nDigits = 5;
    prmAddParam(prm,"nDigits",1,&param.nDigits,sizeof(int),"nd",
		"<number of digits to use in output filenames> = 5");
    param.bPeriodic = 0;
    prmAddParam(prm,"bPeriodic",0,&param.bPeriodic,sizeof(int),"p",
		"periodic/non-periodic = -p");
    param.bRestart = 0;
    prmAddParam(prm,"bRestart",0,&param.bRestart,sizeof(int),"restart",
		"restart from checkpoint");
    param.bParaRead = 1;
    prmAddParam(prm,"bParaRead",0,&param.bParaRead,sizeof(int),"par",
		"enable/disable parallel reading of files = +par");
    param.bParaWrite = 0;
    prmAddParam(prm,"bParaWrite",0,&param.bParaWrite,sizeof(int),"paw",
		"enable/disable parallel writing of files = +paw");
    param.nParaRead = 0;
    prmAddParam(prm,"nParaRead",1,&param.nParaRead,sizeof(int),"npar",
		"number of threads to read with during parallel read = 0 (unlimited)");
    param.nParaWrite = 0;
    prmAddParam(prm,"nParaWrite",1,&param.nParaWrite,sizeof(int),"npaw",
		"number of threads to write with during parallel write = 0 (unlimited)");
    param.bDoDensity = 1;
    prmAddParam(prm,"bDoDensity",0,&param.bDoDensity,sizeof(int),
		"den","enable/disable density outputs = +den");
    param.nBucket = 16;
    prmAddParam(prm,"nBucket",1,&param.nBucket,sizeof(int),"b",
		"<max number of particles in a bucket> = 16");
    param.nGroup = 64;
    prmAddParam(prm,"nGroup",1,&param.nGroup,sizeof(int),"grp",
		"<max number of particles in a group> = 64");
    param.iStartStep = 0;
    prmAddParam(prm,"iStartStep",1,&param.iStartStep,
		sizeof(int),"nstart","<initial step numbering> = 0");
    param.nSteps = 0;
    prmAddParam(prm,"nSteps",1,&param.nSteps,sizeof(int),"n",
		"<number of timesteps> = 0");
    param.nSteps10 = 0;
    prmAddParam(prm,"nSteps10",1,&param.nSteps10,sizeof(int),"n10",
		"<number of timesteps to redshift 10> = 0");
    param.iOutInterval = 0;
    prmAddParam(prm,"iOutInterval",1,&param.iOutInterval,sizeof(int),
		"oi","<number of timesteps between snapshots> = 0");
    param.iFofInterval = 0;
    prmAddParam(prm,"iFofInterval",1,&param.iFofInterval,sizeof(int),
		"fof","<number of timesteps between fof group finding> = 0");
    param.iCheckInterval = 0;
    prmAddParam(prm,"iCheckInterval",1,&param.iCheckInterval,sizeof(int),
		"ci","<number of timesteps between checkpoints> = 0");
    param.iLogInterval = 1;
    prmAddParam(prm,"iLogInterval",1,&param.iLogInterval,sizeof(int),
		"ol","<number of timesteps between logfile outputs> = 1");
    param.iPkInterval = 1;
    prmAddParam(prm,"iPkInterval",1,&param.iPkInterval,sizeof(int),
		"opk","<number of timesteps between pk outputs> = 1");
    param.iDeltakInterval = 0;
    prmAddParam(prm,"iDeltakInterval",1,&param.iDeltakInterval,sizeof(int),
		"odk","<number of timesteps between DeltaK outputs> = 0 (off)");
    param.dDeltakRedshift = 2.0;
    prmAddParam(prm,"dDeltakRedshift",2,&param.dDeltakRedshift,sizeof(double),"zdel",
		"starting redshift to output delta(k) field = 2.0");
    param.bEwald = 1;
    prmAddParam(prm,"bEwald",0,&param.bEwald,sizeof(int),"ewald",
		"enable/disable Ewald correction = +ewald");
    param.iEwOrder = 4;
    prmAddParam(prm,"iEwOrder",1,&param.iEwOrder,sizeof(int),"ewo",
		"<Ewald multipole expansion order: 1, 2, 3 or 4> = 4");
    param.nReplicas = 0;
    prmAddParam(prm,"nReplicas",1,&param.nReplicas,sizeof(int),"nrep",
		"<nReplicas> = 0 for -p, or 1 for +p");
    param.dSoft = 0.0;
    prmAddParam(prm,"dSoft",2,&param.dSoft,sizeof(double),"e",
		"<gravitational softening length> = 0.0");
    param.dSoftMax = 0.0;
    prmAddParam(prm,"dSoftMax",2,&param.dSoftMax,sizeof(double),"eMax",
		"<maximum comoving gravitational softening length (abs or multiplier)> = 0.0");
    param.bPhysicalSoft = 0;
    prmAddParam(prm,"bPhysicalSoft",0,&param.bPhysicalSoft,sizeof(int),"PhysSoft",
		"<Physical gravitational softening length> -PhysSoft");
    param.bSoftMaxMul = 1;
    prmAddParam(prm,"bSoftMaxMul",0,&param.bSoftMaxMul,sizeof(int),"SMM",
		"<Use maximum comoving gravitational softening length as a multiplier> +SMM");
    param.bDoSoftOutput = 0;
    prmAddParam(prm,"bDoSoftOutput",0,&param.bDoSoftOutput,sizeof(int),
		"softout","enable/disable soft outputs = -softout");
    param.bDoAccOutput = 0;
    prmAddParam(prm,"bDoAccOutput",0,&param.bDoAccOutput,sizeof(int),
		"accout","enable/disable acceleration outputs = -accout");
    param.bDoPotOutput = 0;
    prmAddParam(prm,"bDoPotOutput",0,&param.bDoPotOutput,sizeof(int),
		"potout","enable/disable potential outputs = -potout");
    param.bDoRungOutput = 0;
    prmAddParam(prm,"bDoRungOutput",0,&param.bDoRungOutput,sizeof(int),
		"rungout","enable/disable rung outputs = -rungout");
    param.bDoRungDestOutput = 0;
    prmAddParam(prm,"bDoRungDestOutput",0,&param.bDoRungDestOutput,sizeof(int),
		"rungdestout","enable/disable rung destination outputs = -rungdestout");
    param.dDelta = 0.0;
    prmAddParam(prm,"dDelta",2,&param.dDelta,sizeof(double),"dt",
		"<time step>");
    param.dEta = 0.2;
    prmAddParam(prm,"dEta",2,&param.dEta,sizeof(double),"eta",
		"<time step criterion> = 0.1");
    param.bGravStep = 0;
    prmAddParam(prm,"bGravStep",0,&param.bGravStep,sizeof(int),
		"gs","<Gravity timestepping according to iTimeStep Criterion>");
    param.bEpsAccStep = 0;
    prmAddParam(prm,"bEpsAccStep",0,&param.bEpsAccStep,sizeof(int),
		"ea", "<Sqrt(Epsilon on a) timestepping>");
    param.bDensityStep = 0;
    prmAddParam(prm,"bDensityStep",0,&param.bDensityStep,sizeof(int),
		"isrho", "<Sqrt(1/Rho) timestepping>");
    param.iTimeStepCrit = 0;
    prmAddParam(prm,"iTimeStepCrit",1,&param.iTimeStepCrit,sizeof(int),
		"tsc", "<Criteria for dynamical time-stepping>");
    param.nPartRhoLoc = 32;
    prmAddParam(prm,"nPartRhoLoc",1,&param.nPartRhoLoc,sizeof(int),
		"nprholoc", "<Number of particles for local density in dynamical time-stepping>");
    param.dPreFacRhoLoc = 4.0*M_PI/3.0;
    prmAddParam(prm,"dPreFacRhoLoc",2,&param.dPreFacRhoLoc,sizeof(double),
		"dprefacrholoc", "<Pre-factor for local density in dynamical time-stepping>");
    param.dEccFacMax = 3000;
    prmAddParam(prm,"dEccFacMax",2,&param.dEccFacMax,sizeof(double),
		"deccfacmax", "<Maximum correction factor for eccentricity correction>");
    param.nPartColl = 0;
    prmAddParam(prm,"nPartColl",1,&param.nPartColl,sizeof(int),
		"npcoll", "<Number of particles in collisional regime>");
    param.nTruncateRung = 0;
    prmAddParam(prm,"nTruncateRung",1,&param.nTruncateRung,sizeof(int),"nTR",
		"<number of MaxRung particles to delete MaxRung> = 0");
    param.iMaxRung = IRUNGMAX;
    sprintf(ach,"<maximum timestep rung> = %d",param.iMaxRung);
    prmAddParam(prm,"iMaxRung",1,&param.iMaxRung,sizeof(int),
		"mrung",ach);
    param.bNewKDK = 0;
    prmAddParam(prm,"bNewKDK",0,&param.bNewKDK,
		sizeof(int), "NewKDK", "<Use new implementation of KDK time stepping=no>");
    param.dEwCut = 2.6;
    prmAddParam(prm,"dEwCut",2,&param.dEwCut,sizeof(double),"ew",
		"<dEwCut> = 2.6");
    param.dEwhCut = 2.8;
    prmAddParam(prm,"dEwhCut",2,&param.dEwhCut,sizeof(double),"ewh",
		"<dEwhCut> = 2.8");
    param.dTheta = 0.7;
    param.dTheta2 = param.dTheta;
    param.dTheta20 = param.dTheta;
    prmAddParam(prm,"dTheta",2,&param.dTheta,sizeof(double),"theta",
		"<Barnes opening criterion> = 0.8");
    prmAddParam(prm,"dTheta20",2,&param.dTheta20,sizeof(double),
		"theta20","<Barnes opening criterion for 2 < z <= 20> = 0.8");
    prmAddParam(prm,"dTheta2",2,&param.dTheta2,sizeof(double),
		"theta2","<Barnes opening criterion for z <= 2> = 0.8");
    param.dPeriod = 1.0;
    prmAddParam(prm,"dPeriod",2,&param.dPeriod,sizeof(double),"L",
		"<periodic box length> = 1.0");
    param.dxPeriod = 1.0;
    prmAddParam(prm,"dxPeriod",2,&param.dxPeriod,sizeof(double),"Lx",
		"<periodic box length in x-dimension> = 1.0");
    param.dyPeriod = 1.0;
    prmAddParam(prm,"dyPeriod",2,&param.dyPeriod,sizeof(double),"Ly",
		"<periodic box length in y-dimension> = 1.0");
    param.dzPeriod = 1.0;
    prmAddParam(prm,"dzPeriod",2,&param.dzPeriod,sizeof(double),"Lz",
		"<periodic box length in z-dimension> = 1.0");
    param.achInFile[0] = 0;
    prmAddParam(prm,"achInFile",3,param.achInFile,256,"I",
		"<input file name> (file in TIPSY binary format)");
    strcpy(param.achOutName,"pkdgrav");
    prmAddParam(prm,"achOutName",3,param.achOutName,256,"o",
		"<output name for snapshots and logfile> = \"pkdgrav\"");
    strcpy(param.achOutPath,"");
    prmAddParam(prm,"achOutPath",3,param.achOutPath,256,"op",
		"<output path for snapshots and logfile> = \"\"");
    strcpy(param.achIoPath,"");
    prmAddParam(prm,"achIoPath",3,param.achIoPath,256,"iop",
		"<output path for snapshots and logfile> = \"\"");
    strcpy(param.achCheckpointPath,"");
    prmAddParam(prm,"achCheckpointPath",3,param.achCheckpointPath,256,"cpp",
		"<output path for checkpoints> = \"\"");
    csm->val.bComove = 0;
    prmAddParam(prm,"bComove",0,&csm->val.bComove,sizeof(int),
		"cm", "enable/disable comoving coordinates = -cm");
    csm->val.dHubble0 = 0.0;
    prmAddParam(prm,"dHubble0",2,&csm->val.dHubble0,
		sizeof(double),"Hub", "<dHubble0> = 0.0");
    csm->val.dOmega0 = 1.0;
    prmAddParam(prm,"dOmega0",2,&csm->val.dOmega0,
		sizeof(double),"Om", "<dOmega0> = 1.0");
    csm->val.dLambda = 0.0;
    prmAddParam(prm,"dLambda",2,&csm->val.dLambda,
		sizeof(double),"Lambda", "<dLambda> = 0.0");
    csm->val.dOmegaDE = 0.0;
    prmAddParam(prm,"dOmegaDE",2,&csm->val.dOmegaDE,
		sizeof(double),"OmDE", "Omega for Dark Energy using w0 and wa parameters: <dOmegaDE> = 0.0");
    csm->val.w0 = -1.0;
    prmAddParam(prm,"w0",2,&csm->val.w0,
		sizeof(double),"w0", "w0 parameter for Dark Energy <w0> = -1.0 (pure Lambda)");
    csm->val.wa = 0.0;
    prmAddParam(prm,"wa",2,&csm->val.wa,
		sizeof(double),"wa", "wa parameter for Dark Energy <wa> = 0.0 (pure Lambda)");
    csm->val.dOmegaRad = 0.0;
    prmAddParam(prm,"dOmegaRad",2,&csm->val.dOmegaRad,
		sizeof(double),"Omrad", "<dOmegaRad> = 0.0");
    csm->val.dOmegab = 0.0;
    prmAddParam(prm,"dOmegab",2,&csm->val.dOmegab,
		sizeof(double),"Omb", "<dOmegab> = 0.0");
    csm->val.dSigma8 = 0.0;
    prmAddParam(prm,"dSigma8",2,&csm->val.dSigma8,
		sizeof(double),"S8", "<dSimga8> = 0.0");
    csm->val.dNormalization = 0.0;
    prmAddParam(prm,"dNormalization",2,&csm->val.dNormalization,
		sizeof(double),"As", "<dNormalization> = 0.0");
    csm->val.dSpectral = 0.0;
    prmAddParam(prm,"dSpectral",2,&csm->val.dSpectral,
		sizeof(double),"ns", "<dSpectral> = 0.0");
    csm->val.dRunning = 0.0;
    prmAddParam(prm,"dRunning",2,&csm->val.dRunning,
        sizeof(double), "alphas", "Primordial tilt running: <dRunning> = 0.0");
    csm->val.dPivot = 0.05;
    prmAddParam(prm,"dPivot",2,&csm->val.dPivot,
        sizeof(double), "kpivot", "Primordial pivot scale in 1/Mpc (not h/Mpc): <dPivot> = 0.05");
    strcpy(param.achDataSubPath,"");
    prmAddParam(prm,"achDataSubPath",3,param.achDataSubPath,256,
		NULL,NULL);
    param.dExtraStore = 0.1;
    prmAddParam(prm,"dExtraStore",2,&param.dExtraStore,
		sizeof(double),NULL,NULL);
    param.bDualTree = 0;
    prmAddParam(prm,"bDualTree",0,&param.bDualTree,sizeof(int),"2tree",
		"enable/disable second tree for active rungs = -2tree");
    param.nTreeBitsLo = 14;
    prmAddParam(prm,"nTreeBitsLo",1,&param.nTreeBitsLo,
	sizeof(int),"treelo",
	"<number of low bits for tree> = 14");
    param.nTreeBitsHi = 18;
    prmAddParam(prm,"nTreeBitsHi",1,&param.nTreeBitsHi,
	sizeof(int),"treehi",
	"<number of high bits for tree> = 18");
#ifdef MDL_CACHE_SIZE
    param.iCacheSize = MDL_CACHE_SIZE;
#else
    param.iCacheSize = 0;
#endif
    prmAddParam(prm,"iCacheSize",1,&param.iCacheSize,sizeof(int),"cs",
		"<size of the MDL cache (0=default)> = 0");
    param.iWorkQueueSize = 0;
    prmAddParam(prm,"iWorkQueueSize",1,&param.iWorkQueueSize,sizeof(int),"wqs",
		"<size of the MDL work queue> = 0");
    param.iCUDAQueueSize = 8;
    prmAddParam(prm,"iCUDAQueueSize",1,&param.iCUDAQueueSize,sizeof(int),"cqs",
		"<size of the CUDA work queue> = 8");
    param.nSmooth = 64;
    prmAddParam(prm,"nSmooth",1,&param.nSmooth,sizeof(int),"s",
		"<number of particles to smooth over> = 64");
    param.bStandard = 1;
    prmAddParam(prm,"bStandard",0,&param.bStandard,sizeof(int),"std",
		"output in standard TIPSY binary format = -std");
    param.iCompress = 0;
    prmAddParam(prm,"iCompress",1,&param.iCompress,sizeof(int),NULL,
		"compression format, 0=none, 1=gzip, 2=bzip2");
    param.bHDF5 = 0;
    prmAddParam(prm,"bHDF5",0,&param.bHDF5,sizeof(int),"hdf5",
		"output in HDF5 format = -hdf5");
    param.bDoublePos = 0;
    prmAddParam(prm,"bDoublePos",0,&param.bDoublePos,sizeof(int),"dp",
		"input/output double precision positions (standard format only) = -dp");
    param.bDoubleVel = 0;
    prmAddParam(prm,"bDoubleVel",0,&param.bDoubleVel,sizeof(int),"dv",
		"input/output double precision velocities (standard format only) = -dv");
    param.bLightCone = 0;
    prmAddParam(prm,"bLightCone",0,&param.bLightCone,sizeof(int),"lc",
		"output light cone data = -lc");
    param.nSideHealpix = 8192;
    prmAddParam(prm,"nSideHealpix",1,&param.nSideHealpix,
		sizeof(int),"healpix",
		"<Number per side of the healpix map> = 8192");
    param.bLightConeParticles = 0;
    prmAddParam(prm,"bLightConeParticles",0,&param.bLightConeParticles,sizeof(int),"lcp",
		"output light cone particles = -lcp");
    param.bInFileLC = 0;
    prmAddParam(prm,"bInFileLC",0,&param.bInFileLC,sizeof(int),"lcin",
		"input light cone data = -lcin");
    param.dRedshiftLCP = 0;
    prmAddParam(prm,"dRedshiftLCP",2,&param.dRedshiftLCP,sizeof(double),"zlcp",
		"starting redshift to output light cone particles = 0");
    param.dRedTo = 0.0;
    prmAddParam(prm,"dRedTo",2,&param.dRedTo,sizeof(double),"zto",
		"specifies final redshift for the simulation");
    param.dRedFrom = 0.0;
    prmAddParam(prm,"dRedFrom",2,&param.dRedFrom,sizeof(double),"z",
		"specifies initial redshift for the simulation");
    param.dFracDualTree = 0.05;
    prmAddParam(prm,"dFracDualTree",2,&param.dFracDualTree,
		sizeof(double),"fndt",
		"<Fraction of Active Particles for to use a dual tree> = dFracNoDomainDecomp ");
    param.dFracNoDomainDecomp = 0.1;
    prmAddParam(prm,"dFracNoDomainDecomp",2,&param.dFracNoDomainDecomp,
		sizeof(double),"fndd",
		"<Fraction of Active Particles for no DD> = 0.1");
    param.dFracNoDomainRootFind = 0.1;
    prmAddParam(prm,"dFracNoDomainRootFind",2,&param.dFracNoDomainRootFind,
		sizeof(double),"fndrf",
		"<Fraction of Active Particles for no DD root finding> = 0.1");
    param.dFracNoDomainDimChoice = 0.1;
    prmAddParam(prm,"dFracNoDomainDimChoice",2,&param.dFracNoDomainDimChoice,
		sizeof(double),"fnddc",
		"<Fraction of Active Particles for no DD dimension choice> = 0.1");
    param.bDoGravity = 1;
    prmAddParam(prm,"bDoGravity",0,&param.bDoGravity,sizeof(int),"g",
		"enable/disable interparticle gravity = +g");
    param.iWallRunTime = 0;
    prmAddParam(prm,"iWallRunTime",1,&param.iWallRunTime,
		sizeof(int),"wall",
		"<Maximum Wallclock time (in minutes) to run> = 0 = infinite");
    param.iSignalSeconds = 0;
    prmAddParam(prm,"iSignalSeconds",1,&param.iSignalSeconds,
		sizeof(int),"signal",
		"<Time (in seconds) that USR1 is sent before termination> = 0 = immediate");
    param.bFindGroups = 0;
    prmAddParam(prm,"bFindGroups",0,&param.bFindGroups,sizeof(int),
		"groupfinder","<enable/disable group finder> = -groupfinder");
    param.bFindHopGroups = 0;
    prmAddParam(prm,"bFindHopGroups",0,&param.bFindHopGroups,sizeof(int),
		"hop","<enable/disable phase-space group finder> = -hop");
    param.dHopTau = -4.0;
    prmAddParam(prm,"dHopTau",2,&param.dHopTau,sizeof(double),"hoptau",
		"<linking length for Gasshopper (negative for multiples of softening)> = -4.0");
    param.nMinMembers = 10;
    prmAddParam(prm,"nMinMembers",1,&param.nMinMembers,sizeof(int),
		"nMinMembers","<minimum number of group members> = 10");
    param.dTau = 0.164;
    prmAddParam(prm,"dTau",2,&param.dTau,sizeof(double),"dTau",
		"<linking length for FOF in units of mean particle separation> = 0.164");
    param.dEnvironment0 = -1.0;
    prmAddParam(prm,"dEnvironment0",2,&param.dEnvironment0,sizeof(double),"dEnv0",
		"<first radius for density environment about a group> = -1.0 (disabled)");
    param.dEnvironment1 = -1.0;
    prmAddParam(prm,"dEnvironment1",2,&param.dEnvironment1,sizeof(double),"dEnv1",
		"<second radius for density environment about a group> = -1.0 (disabled)");
    param.bTraceRelaxation = 0;
    prmAddParam(prm,"bTraceRelaxation",0,&param.bTraceRelaxation,sizeof(int),
		"rtrace","<enable/disable relaxation tracing> = -rtrace");

#ifdef MDL_FFTW
    param.nBinsPk = 0;
    prmAddParam(prm,"nBinsPk",1,&param.nBinsPk,
		sizeof(int),"npk","<Number of log bins for P(k)> = nGridPk/2");
    param.nGridPk = 0;
    prmAddParam(prm,"nGridPk",1,&param.nGridPk,
		sizeof(int),"pk","<Grid size for measure P(k) 0=disabled> = 0");
    param.bPkInterlace = 1;
    prmAddParam(prm,"bPkInterlace",0,&param.bPkInterlace,
		sizeof(int),"pkinterlace","<Use interlacing to measure P(k)> = +pkinterlace");
    param.iPkOrder = 4;
    prmAddParam(prm,"iPkOrder",1,&param.iPkOrder,
		sizeof(int),"pko","<Mass assignment order for measuring P(k) = 3");
    param.bFixedAmpIC = 0;
    prmAddParam(prm,"bFixedAmpIC",0,&param.bFixedAmpIC,
		sizeof(int),"fixedamp","<Use fixed amplitude of 1 for ICs> = -fixedamp");
    param.dFixedAmpPhasePI = 0.0;
    prmAddParam(prm,"dFixedAmpPhasePI",2,&param.dFixedAmpPhasePI,
		sizeof(double),"fixedphase","<Phase shift for fixed amplitude in units of PI> = 0.0");
    param.nGridLin = 0;
    prmAddParam(prm, "nGridLin", 1, &param.nGridLin,
        sizeof(int), "lingrid", "<Grid size for linear species 0=disabled> =0");
    param.bDoLinPkOutput = 0;
    prmAddParam(prm, "bDoLinPkOutput", 0, &param.bDoLinPkOutput,
        sizeof(int), "linPk", "<enable/disable power spectrum output for linear species> = 0");
#endif

    /* IC Generation */
    csm->val.classData.bClass = 0;
    prmAddParam(prm,"bClass",0,&csm->val.classData.bClass,
		sizeof(int),"class","<Enable/disable the use of CLASS> = -class");
    param.achClassFilename[0] = 0;
    prmAddParam(prm, "achClassFilename", 3, param.achClassFilename,
		256, "class_filename", "<Name of hdf5 file containing the CLASS data> -class_filename");
    param.achLinearSpecies[0] = 0;
    prmAddParam(prm, "achLinSpecies", 3, param.achLinearSpecies,
                128, "lin_species",
                "<plus-separated string of linear species, e.g. \"ncdm[0]+g+metric\"> -lin_species");
    param.achPowerSpecies[0] = 0;
    prmAddParam(prm, "achPkSpecies", 3, param.achPowerSpecies,
                128, "pk_species",
                "<plus-separated string of P(k) linear species, e.g. \"ncdm[0]+g\"> -pk_species");
    param.h = 0.0;
    prmAddParam(prm,"h",2,&param.h,
		sizeof(double),"h","<hubble parameter h> = 0");
    param.dBoxSize = 1.0;
    prmAddParam(prm,"dBoxSize",2,&param.dBoxSize,
		sizeof(double),"mpc","<Simulation Box size in Mpc> = 1.0");
    param.nGrid = 0;
    prmAddParam(prm,"nGrid",1,&param.nGrid,
		sizeof(int),"grid","<Grid size for IC 0=disabled> = 0");
    param.achTfFile[0] = 0;
    prmAddParam(prm,"achTfFile",3,param.achTfFile,256,"tf",
		"<transfer file name> (file in CMBFAST format)");
    param.iSeed = 0;
    prmAddParam(prm,"iSeed",1,&param.iSeed,
		sizeof(int),"seed","<Random seed for IC> = 0");
    param.b2LPT = 1;
    prmAddParam(prm,"b2LPT",0,&param.b2LPT,
		sizeof(int),"2lpt","<Enable/disable 2LPT> = 1");
    param.bWriteIC = 0;
    prmAddParam(prm,"bWriteIC",0,&param.bWriteIC,
		sizeof(int),"wic","<Write IC after generating> = 0");

    /* Memory models */
    param.bMemIntegerPosition = 0;
    prmAddParam(prm,"bMemIntegerPosition",0,&param.bMemIntegerPosition,
		sizeof(int),"integer","<Particles have integerized positions> = -integer");
    param.bMemUnordered = 0;
    prmAddParam(prm,"bMemUnordered",0,&param.bMemUnordered,
		sizeof(int),"unordered","<Particles have no specific order> = -unordered");
    param.bMemParticleID = 0;
    prmAddParam(prm,"bMemParticleID",0,&param.bMemParticleID,
		sizeof(int),"pid","<Particles have a unique identifier> = -pid");
    param.bMemAcceleration = 0;
    prmAddParam(prm,"bMemAcceleration",0,&param.bMemAcceleration,
		sizeof(int),"Ma","<Particles have acceleration> = -Ma");
    param.bMemVelocity = 0;
    prmAddParam(prm,"bMemVelocity",0,&param.bMemVelocity,
		sizeof(int),"Mv","<Particles have velocity> = -Mv");
    param.bMemPotential = 0;
    prmAddParam(prm,"bMemPotential",0,&param.bMemPotential,
		sizeof(int),"Mp","<Particles have potential> = -Mp");
    param.bMemGroups = 0;
    prmAddParam(prm,"bMemGroups",0,&param.bMemGroups,
		sizeof(int),"Mg","<Particles support group finding> = -Mg");
    param.bMemMass = 0;
    prmAddParam(prm,"bMemMass",0,&param.bMemMass,
		sizeof(int),"Mm","<Particles have individual masses> = -Mm");
    param.bMemSoft = 0;
    prmAddParam(prm,"bMemSoft",0,&param.bMemSoft,
		sizeof(int),"Ms","<Particles have individual softening> = -Ms");
    param.bMemRelaxation = 0;
    prmAddParam(prm,"bMemRelaxation",0,&param.bMemRelaxation,
		sizeof(int),"Mr","<Particles have relaxation> = -Mr");
    param.bMemVelSmooth = 0;
    prmAddParam(prm,"bMemVelSmooth",0,&param.bMemVelSmooth,
		sizeof(int),"Mvs","<Particles support velocity smoothing> = -Mvs");
    param.bMemNodeMoment = 0;
    prmAddParam(prm,"bMemNodeMoment",0,&param.bMemNodeMoment,
		sizeof(int),"MNm","<Tree nodes support multipole moments> = 0");
    param.bMemNodeAcceleration = 0;
    prmAddParam(prm,"bMemNodeAcceleration",0,&param.bMemNodeAcceleration,
		sizeof(int),"MNa","<Tree nodes support acceleration (for bGravStep)> = 0");
    param.bMemNodeVelocity = 0;
    prmAddParam(prm,"bMemNodeVelocity",0,&param.bMemNodeVelocity,
		sizeof(int),"MNv","<Tree nodes support velocity (for iTimeStepCrit = 1)> = 0");
    param.bMemNodeSphBounds = 0;
    prmAddParam(prm,"bMemNodeSphBounds",0,&param.bMemNodeSphBounds,
		sizeof(int),"MNsph","<Tree nodes support fast-gas bounds> = 0");

    param.bMemNodeBnd = 1;
    /*prmAddParam(prm,"bMemNodeBnd",1,&param.bMemNodeBnd,
      sizeof(int),"MNbnd","<Tree nodes support 3D bounds> = 1");*/

    param.bMemNodeVBnd = 0;
    prmAddParam(prm,"bMemNodeVBnd",0,&param.bMemNodeVBnd,
		sizeof(int),"MNvbnd","<Tree nodes support velocity bounds> = 0");

/* Gas Parameters */
    param.bDoGas = 0;
    prmAddParam(prm,"bDoGas",0,&param.bDoGas,sizeof(int),"gas",
		"calculate gas/don't calculate gas = +gas");
    param.bGasAdiabatic = 1;
    prmAddParam(prm,"bGasAdiabatic",0,&param.bGasAdiabatic,
		sizeof(int),"GasAdiabatic",
		"<Gas is Adiabatic> = +GasAdiabatic");
    param.bGasIsothermal = 0;
    prmAddParam(prm,"bGasIsothermal",0,&param.bGasIsothermal,
		sizeof(int),"GasIsothermal",
		"<Gas is Isothermal> = +GasIsothermal");
    param.bGasCooling = 0;
    prmAddParam(prm,"bGasCooling",0,&param.bGasCooling,
		sizeof(int),"GasCooling",
		"<Gas is Cooling> = +GasCooling");
    param.bInitTFromCooling = 0;
    prmAddParam(prm,"bInitTFromCooling",0,&param.bInitTFromCooling,
		sizeof(int),"bInitTFromCooling",
		"set T (also E, Y, etc..) using Cooling initialization value = +bInitTFromCooling");
    param.iRungCoolTableUpdate = 0;
    prmAddParam(prm,"iRungCoolTableUpdate",1,&param.iRungCoolTableUpdate,
		sizeof(int),"iRungCoolTableUpdate",
		"<Rung on which to update cool tables, def. 0>");
    param.bInitTFromCooling = 0;
    param.dEtaCourant = 0.4;
    prmAddParam(prm,"dEtaCourant",2,&param.dEtaCourant,sizeof(double),"etaC",
				"<Courant criterion> = 0.4");
    param.dEtaUDot = 0.25;
    prmAddParam(prm,"dEtaUDot",2,&param.dEtaUDot,sizeof(double),"etau",
		"<uDot timestep criterion> = 0.25");
    param.dConstAlpha = 1.0; 
    prmAddParam(prm,"dConstAlpha",2,&param.dConstAlpha,
		sizeof(double),"alpha",
		"<Alpha constant in viscosity> = 1.0 or 0.5 (bBulkViscosity)");
    param.dConstBeta = 2.0; 
    prmAddParam(prm,"dConstBeta",2,&param.dConstBeta,
		sizeof(double),"beta",
		"<Beta constant in viscosity> = 2.0 or 0.5 (bBulkViscosity)");
    param.dConstGamma = 5.0/3.0;
    prmAddParam(prm,"dConstGamma",2,&param.dConstGamma,
		sizeof(double),"gamma",
		"<Ratio of specific heats> = 5/3");
    param.dMeanMolWeight = 1.0;
    prmAddParam(prm,"dMeanMolWeight",2,&param.dMeanMolWeight,
		sizeof(double),"mmw",
		"<Mean molecular weight in amu> = 1.0");
    param.dGasConst = 1.0;
    prmAddParam(prm,"dGasConst",2,&param.dGasConst,
		sizeof(double),"gcnst",
		"<Gas Constant>");
    param.dKBoltzUnit = 1.0;
    prmAddParam(prm,"dKBoltzUnit",2,&param.dKBoltzUnit,
		sizeof(double),"kb",
		"<Boltzmann Constant in System Units>");
    param.dhMinOverSoft = 0.0;
    prmAddParam(prm,"dhMinOverSoft",2,&param.dhMinOverSoft,
		sizeof(double),"hmin",
		"<Minimum h as a fraction of Softening> = 0.0");
    param.dMetalDiffusionCoeff = 0;
    prmAddParam(prm,"dMetalDiffusionCoeff",2,&param.dMetalDiffusionCoeff,
		sizeof(double),"metaldiff",
		"<Coefficient in Metal Diffusion> = 0.0");
    param.dThermalDiffusionCoeff = 0;
    prmAddParam(prm,"dThermalDiffusionCoeff",2,&param.dThermalDiffusionCoeff,
		sizeof(double),"thermaldiff",
		"<Coefficient in Thermal Diffusion> = 0.0");
    param.dMsolUnit = 1.0;
    prmAddParam(prm,"dMsolUnit",2,&param.dMsolUnit,
		sizeof(double),"msu",
		"<Solar mass/system mass unit>");
    param.dKpcUnit = 1000.0;
    prmAddParam(prm,"dKpcUnit",2,&param.dKpcUnit,
		sizeof(double),"kpcu",
		"<Kiloparsec/system length unit>");
    param.ddHonHLimit = 0.1;
    prmAddParam(prm,"ddHonHLimit",2,&param.ddHonHLimit,
		sizeof(double),"dhonh",
		"<|dH|/H Limiter> = 0.1");
    param.iViscosityLimiter = 1;
    prmAddParam(prm,"iViscosityLimiter",1,&param.iViscosityLimiter,sizeof(int),
		"vlim","<iViscosity Limiter> = 1");
    param.iDiffusion = 0;
    prmAddParam(prm,"iDiffusion",1,&param.iDiffusion,sizeof(int),
		"idiff","<iDiffusion> = 0");
    param.bAddDelete = 0;
    prmAddParam(prm,"bAddDelete",0,&param.bAddDelete,sizeof(int),
		"adddel","<Add Delete Particles> = 0");
    /* Star Form parameters */
    param.bStarForm = 0;
    prmAddParam(prm,"bStarForm",0,&param.bStarForm,sizeof(int),
		"stfm","<Star Forming> = 0");
    param.bFeedback = 0;
    prmAddParam(prm,"bFeedback",0,&param.bFeedback,sizeof(int),
		"fdbk","<Stars provide feedback> = 0");
    param.SFdComovingDenMin = 0; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdComovingDenMin", 2, &param.SFdComovingDenMin,
		sizeof(double), "stODmin",
		"<Minimum overdensity for forming stars> = 2");
    param.SFdPhysDenMin =  0.1/.76*1.66e-24; /* 0.1 nH/cc; RAMSES DEFAULT */
    prmAddParam(prm,"SFdPhysDenMin", 2, &param.SFdPhysDenMin,
		sizeof(double), "stPDmin",
		"<Minimum physical density for forming stars (gm/cc)> =  7e-26");
/* Duplicatee below.
    param.SFdInitStarMass = 0;
    prmAddParam(prm,"SFdInitStarMass", 2, &param.SFdInitStarMass,
		sizeof(double), "stm0",
		"<Initial star mass> = 0");
*/
    param.SFdESNPerStarMass = 5.0276521e+15; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdESNPerStarMass", 2, &param.SFdESNPerStarMass,
		sizeof(double), "ESNPerStarMass",
		"<ESN per star mass, erg per g of stars> = 1.25e16");
    param.SFdTMax = 1e20; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdTMax", 2, &param.SFdTMax,
		sizeof(double), "SFdTMax",
		"<Maximum temperature for forming stars, K> = 3e4");
    param.SFdEfficiency = 0.01; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdEfficiency", 2, &param.SFdEfficiency,
		sizeof(double), "SFdEfficiency",
		"<SF Efficiency> = 0.1");
    param.SFdtCoolingShutoff = 30e6; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdtCoolingShutoff", 2, &param.SFdtCoolingShutoff,
		sizeof(double), "SFdtCoolingShutoff",
		"<SF Cooling Shutoff duration> = 30e6");

    param.SFdtFeedbackDelay = 10e6; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdtFeedbackDelay", 2, &param.SFdtFeedbackDelay,
		sizeof(double), "SFdtFBD",
		"<SF FB delay> = 10e6");
    param.SFdMassLossPerStarMass = 0.1; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdMassLossPerStarMass", 2, &param.SFdMassLossPerStarMass,
		sizeof(double), "SFMLPSM",
		"<SFMSPSM > = 0.1");
    param.SFdZMassPerStarMass = 0.01; /* RAMSES DEFAULT */
    prmAddParam(prm,"SFdZMassPerStarMass", 2, &param.SFdZMassPerStarMass,
		sizeof(double), "SFZMPSM",
		"<SF ZMPSM> = 0.01");
    param.SFdInitStarMass = 0; 
    prmAddParam(prm,"SFdInitStarMass", 2, &param.SFdInitStarMass,
		sizeof(double), "SFISM",
		"<SF ISM> = ?");
    param.SFdMinGasMass = 0;
    prmAddParam(prm,"SFdMinGasMass", 2, &param.SFdMinGasMass,
		sizeof(double), "SFMGM",
		"<SF MGM> = ?");
    param.SFdvFB = 100;
    prmAddParam(prm,"SFdvFB", 2, &param.SFdvFB,
		sizeof(double), "SFVFB",
		"<SF dvFB sound speed in FB region expected, km/s> = 100");

    param.SFbdivv = 0;
    prmAddParam(prm,"SFbdivv", 0, &param.SFbdivv,
		sizeof(int), "SFbdivv",
		"<SF Use div v for star formation> = 1");
    /* END Gas/Star Parameters */
    param.nOutputParticles = 0;
    prmAddArray(prm,"lstOrbits",4,&param.iOutputParticles,sizeof(uint64_t),&param.nOutputParticles);
    param.bAccelStep = 0;
    /*
    ** Create the processor subset tree.
    */
    if (nThreads > 1) {
	msrprintf("Adding %d through %d to the PST\n",1,nThreads);
	ServiceSetAdd::input inAdd(nThreads);
	mdl->RunService(PST_SETADD,sizeof(inAdd),&inAdd);
	}
    }

#if 0
void msrLogParams(MSR &msr,FILE *fp) {
#if defined(MAXHOSTNAMELEN) && defined(HAVE_GETHOSTNAME)
    char hostname[MAXHOSTNAMELEN];
#endif
    double z;
    int i;

#ifdef __DATE__
#ifdef __TIME__
    fprintf(fp,"# Compiled: %s %s\n",__DATE__,__TIME__);
#endif
#endif
#if defined(__AVX__)
    fprintf(fp,"# with AVX support\n");
#elif defined(__SSE3__)
    fprintf(fp,"# with SSE3 support\n");
#elif defined(__SSE2__)
    fprintf(fp,"# with SSE2 support\n");
#elif defined(__SSE__)
    fprintf(fp,"# with SSE support\n");
#endif

    fprintf(fp,"# Preprocessor macros:");
#ifdef DEBUG
    fprintf(fp," DEBUG");
#endif
#ifdef _REENTRANT
    fprintf(fp," _REENTRANT");
#endif
#if defined(MAXHOSTNAMELEN) && defined(HAVE_GETHOSTNAME)
    fprintf(fp,"\n# Master host: ");
    if (gethostname(hostname,MAXHOSTNAMELEN))
	fprintf(fp,"unknown");
    else
	fprintf(fp,"%s",hostname);
#endif
    fprintf(fp,"\n# N: %" PRIu64,N);
    fprintf(fp," ngas: %" PRIu64,nGas);
    fprintf(fp," nstar: %" PRIu64,nStar);
    fprintf(fp," nThreads: %d",nThreads);
    fprintf(fp," bDiag: %d",param.bDiag);
    fprintf(fp," Verbosity flags: (%d,%d,%d,%d,%d)",param.bVWarnings,
	    param.bVStart,param.bVStep,param.bVRungStat,
	    bVDetails);
    fprintf(fp,"\n# bPeriodic: %d",param.bPeriodic);
    fprintf(fp," bComove: %d",csm->val.bComove);
    fprintf(fp,"\n# bRestart: %d",param.bRestart);
    fprintf(fp," bParaRead: %d",param.bParaRead);
    fprintf(fp," nParaRead: %d",param.nParaRead);
    fprintf(fp," bParaWrite: %d",param.bParaWrite);
    fprintf(fp," nParaWrite: %d",param.nParaWrite);
    fprintf(fp," bStandard: %d",param.bStandard);
    fprintf(fp," iCompress: %d",param.iCompress);
    fprintf(fp," bHDF5: %d",param.bHDF5);
    fprintf(fp," nBucket: %d",param.nBucket);
    fprintf(fp," nGroup: %d",param.nGroup);
    fprintf(fp,"\n# iOutInterval: %d",param.iOutInterval);
    fprintf(fp," iCheckInterval: %d",param.iCheckInterval);
    fprintf(fp," iLogInterval: %d",param.iLogInterval);
    fprintf(fp," iEwOrder: %d",param.iEwOrder);
    fprintf(fp," nReplicas: %d",param.nReplicas);
    fprintf(fp,"\n# dEwCut: %f",param.dEwCut);
    fprintf(fp," dEwhCut: %f",param.dEwhCut);
    fprintf(fp,"\n# iStartStep: %d",param.iStartStep);
    fprintf(fp," nSteps: %d",param.nSteps);
    fprintf(fp," nSmooth: %d",param.nSmooth);
    fprintf(fp," dExtraStore: %f",param.dExtraStore);
    fprintf(fp," nTreeBitsLo: %d",param.nTreeBitsLo);
    fprintf(fp," nTreeBitsHi: %d",param.nTreeBitsHi);
    fprintf(fp," iCacheSize: %d",param.iCacheSize);
    fprintf(fp," iWorkQueueSize: %d",param.iWorkQueueSize);
    fprintf(fp," iCUDAQueueSize: %d",param.iCUDAQueueSize);
    if (prmSpecified(prm,"dSoft"))
	fprintf(fp," dSoft: %g",param.dSoft);
    else
	fprintf(fp," dSoft: input");
    fprintf(fp,"\n# bPhysicalSoft: %d",param.bPhysicalSoft);
    fprintf(fp," bSoftMaxMul: %d",param.bSoftMaxMul);
    fprintf(fp," dSoftMax: %g",param.dSoftMax);
    fprintf(fp," bDoSoftOutput: %d",param.bDoSoftOutput);
    fprintf(fp," bDoAccOutput: %d",param.bDoAccOutput);
    fprintf(fp," bDoPotOutput: %d",param.bDoPotOutput);
    fprintf(fp,"\n# dDelta: %g",param.dDelta);
    fprintf(fp," dEta: %g",param.dEta);
    fprintf(fp," iMaxRung: %d",param.iMaxRung);
    fprintf(fp," bDoRungOutput: %d",param.bDoRungOutput);
    fprintf(fp," bDoRungDestOutput: %d",param.bDoRungDestOutput);
    fprintf(fp,"\n# bGravStep: %d",param.bGravStep);
    fprintf(fp," bEpsAccStep: %d",param.bEpsAccStep);
    fprintf(fp," bDensityStep: %d",param.bDensityStep);
    fprintf(fp," nTruncateRung: %d",param.nTruncateRung);
    fprintf(fp,"\n# iTimeStepCrit: %d",param.iTimeStepCrit);
    fprintf(fp," nPartRhoLoc: %d", param.nPartRhoLoc);
    fprintf(fp," dPreFacRhoLoc: %g", param.dPreFacRhoLoc);
    fprintf(fp," dEccFacMax: %g", param.dEccFacMax);
    fprintf(fp," nPartColl: %d", param.nPartColl);
    fprintf(fp,"\n# bDoGravity: %d",param.bDoGravity);
    fprintf(fp,"\n# dFracDualTree: %g",param.dFracDualTree);
    fprintf(fp,"dFracNoDomainDecomp: %g",param.dFracNoDomainDecomp);
    fprintf(fp," dFracNoDomainRootFind: %g",param.dFracNoDomainRootFind);
    fprintf(fp," dFracNoDomainDimChoice: %g",param.dFracNoDomainDimChoice);
    fprintf(fp,"\n# nTruncateRung: %d",param.nTruncateRung);

    fprintf(fp,"\n# SPH: bDoGas: %d",param.bDoGas);	
    fprintf(fp," bGasAdiabatic: %d",param.bGasAdiabatic);	
    fprintf(fp," bGasIsothermal: %d",param.bGasIsothermal);	
    fprintf(fp," bGasCooling: %d",param.bGasCooling);	
    fprintf(fp," bInitTFromCooling: %d",param.bInitTFromCooling);	
    fprintf(fp," iRungCoolTableUpdate: %d",param.iRungCoolTableUpdate);
    fprintf(fp," iViscosityLimiter: %d",param.iViscosityLimiter);	
    fprintf(fp," iDiffusion: %d",param.iDiffusion);	
    fprintf(fp,"\n# dConstAlpha: %g",param.dConstAlpha);
    fprintf(fp," dConstBeta: %g",param.dConstBeta);
    fprintf(fp," dConstGamma: %g",param.dConstGamma);
    fprintf(fp," dMeanMolWeight: %g",param.dMeanMolWeight);
    fprintf(fp," dGasConst: %g",param.dGasConst);
    fprintf(fp,"\n# dEtaCourant: %g",param.dEtaCourant);
    fprintf(fp," dEtaUDot: %g",param.dEtaUDot);
    fprintf(fp," dTuFac: %g",dTuFac);
    fprintf(fp," dhMinOverSoft: %g",param.dhMinOverSoft);
    fprintf(fp," dMetalDiffusionCoeff: %g",param.dMetalDiffusionCoeff);
    fprintf(fp," dThermalDiffusionCoeff: %g",param.dThermalDiffusionCoeff);
    fprintf(fp,"\n# UNITS: dKBoltzUnit: %g",param.dKBoltzUnit);
    fprintf(fp," dMsolUnit: %g",param.dMsolUnit);
    fprintf(fp," dKpcUnit: %g",param.dKpcUnit);
    if(prmSpecified(prm, "dMsolUnit") &&
       prmSpecified(prm, "dKpcUnit")) {
	fprintf(fp," dErgPerGmUnit: %g", param.dErgPerGmUnit );
	fprintf(fp," dGmPerCcUnit (z=0): %g", param.dGmPerCcUnit );
	fprintf(fp," dSecUnit: %g", param.dSecUnit );
	fprintf(fp," dKmPerSecUnit (z=0): %g", param.dKmPerSecUnit );
	}
    fprintf(fp,"\n# STARFORM: bStarForm %d",param.bStarForm);
    fprintf(fp," bFeedback %d",param.bFeedback);
    fprintf(fp," SFdEfficiency %g",param.SFdEfficiency);
    fprintf(fp," SFdTMax %g",param.SFdTMax);
    fprintf(fp," SFdPhysDenMin %g",param.SFdPhysDenMin);
    fprintf(fp," SFdComovingDenMin %g",param.SFdComovingDenMin);
    fprintf(fp," SFdESNPerStarMass %g",param.SFdESNPerStarMass);
    fprintf(fp,"\n# SFdtCoolingShutoff %g",param.SFdtCoolingShutoff);
    fprintf(fp," SFdtFeedbackDelay %g",param.SFdtFeedbackDelay);
    fprintf(fp," SFdMassLossPerStarMass %g",param.SFdMassLossPerStarMass);
    fprintf(fp," SFdZMassPerStarMass %g",param.SFdZMassPerStarMass);
    fprintf(fp," SFdInitStarMass %g",param.SFdInitStarMass);
    fprintf(fp," SFdMinGasMass %g",param.SFdMinGasMass);
    fprintf(fp," SFbdivv %d",param.SFbdivv);
    /* -- */
    fprintf(fp,"\n# Group Find: bFindHopGroups: %d",param.bFindHopGroups);
    fprintf(fp," dHopTau: %g",param.dHopTau);
    /* -- */
    fprintf(fp,"\n# Group Find: bFindGroups: %d",param.bFindGroups);
    fprintf(fp," dTau: %g",param.dTau);
    fprintf(fp," nMinMembers: %d",param.nMinMembers);
    fprintf(fp," nBins: %d",param.nBins);
    fprintf(fp," bLogBins: %d",param.bLogBins);
    fprintf(fp,"\n# Relaxation estimate: bTraceRelaxation: %d",param.bTraceRelaxation);
    fprintf(fp," dTheta: %f",param.dTheta);
    fprintf(fp,"\n# dPeriod: %g",param.dPeriod);
    fprintf(fp," dxPeriod: %g",
	    param.dxPeriod >= FLOAT_MAXVAL ? 0 : param.dxPeriod);
    fprintf(fp," dyPeriod: %g",
	    param.dyPeriod >= FLOAT_MAXVAL ? 0 : param.dyPeriod);
    fprintf(fp," dzPeriod: %g",
	    param.dzPeriod >= FLOAT_MAXVAL ? 0 : param.dzPeriod);
    fprintf(fp,"\n# dHubble0: %g",csm->val.dHubble0);
    fprintf(fp," dOmega0: %g",csm->val.dOmega0);
    fprintf(fp," dLambda: %g",csm->val.dLambda);
    fprintf(fp," dOmegaDE: %g",csm->val.dOmegaDE);
    fprintf(fp," w0: %g",csm->val.w0);
    fprintf(fp," wa: %g",csm->val.wa);
    fprintf(fp," dOmegaRad: %g",csm->val.dOmegaRad);
    fprintf(fp," dOmegab: %g",csm->val.dOmegab);
    fprintf(fp,"\n# achInFile: %s",param.achInFile);
    fprintf(fp,"\n# achOutName: %s",param.achOutName);
    fprintf(fp,"\n# achOutPath: %s",param.achOutPath);
    fprintf(fp,"\n# achIoPath: %s",param.achIoPath);
    fprintf(fp,"\n# achDataSubPath: %s",param.achDataSubPath);
    if (csm->val.bComove) {
	fprintf(fp,"\n# RedOut:");
	if (nOuts == 0) fprintf(fp," none");
	for (i=0;i<nOuts;i++) {
	    if (i%5 == 0) fprintf(fp,"\n#   ");
	    z = 1.0/csmTime2Exp(csm, pdOutTime[i]) - 1.0;
	    fprintf(fp," %f",z);
	    }
	fprintf(fp,"\n");
	}
    else {
	fprintf(fp,"\n# TimeOut:");
	if (nOuts == 0) fprintf(fp," none");
	for (i=0;i<nOuts;i++) {
	    if (i%5 == 0) fprintf(fp,"\n#   ");
	    fprintf(fp," %f",pdOutTime[i]);
	    }
	fprintf(fp,"\n");
	}
    }
#endif

int MSR::GetLock() {
    /*
    ** Attempts to lock run directory to prevent overwriting. If an old lock
    ** is detected with the same achOutName, an abort is signaled. Otherwise
    ** a new lock is created. The bOverwrite parameter flag can be used to
    ** suppress lock checking.
    */

    FILE *fp = NULL;
    char achTmp[256],achFile[256];

    MakePath(param.achDataSubPath,LOCKFILE,achFile);
    if (!param.bOverwrite && (fp = fopen(achFile,"r"))) {
	if (fscanf(fp,"%s",achTmp) != 1) achTmp[0] = '\0';
	(void) fclose(fp);
	if (!strcmp(param.achOutName,achTmp)) {
	    (void) printf("ABORT: %s detected.\nPlease ensure data is safe to "
			  "overwrite. Delete lockfile and try again.\n",achFile);
	    return 0;
	    }
	}
    if (!(fp = fopen(achFile,"w"))) {
	if (param.bOverwrite && param.bVWarnings) {
	    (void) printf("WARNING: Unable to create %s...ignored.\n",achFile);
	    return 1;
	    }
	else {
	    (void) printf("Unable to create %s\n",achFile);
	    return 0;
	    }
	}
    (void) fprintf(fp,"%s",param.achOutName);
    (void) fclose(fp);
    return 1;
    }

int MSR::CheckForStop(const char *achStopFile) {
    /*
    ** Checks for existence of STOPFILE in run directory. If found, the file
    ** is removed and the return status is set to 1, otherwise 0.
    */

    char achFile[256];
    FILE *fp = NULL;
    MakePath(param.achDataSubPath,achStopFile,achFile);
    if ((fp = fopen(achFile,"r"))) {
	(void) printf("User interrupt detected.\n");
	(void) fclose(fp);
	(void) unlink(achFile);
	return 1;
	}
    return 0;
    }

MSR::~MSR() {
    csmFinish(csm);
    prmFinish(prm);
    Py_XDECREF(arguments);
    Py_XDECREF(specified);
    if (Py_IsInitialized()) Py_Finalize();
    }

void MSR::SetClasses() {
    std::vector<PARTCLASS> classes(PKD_MAX_CLASSES);
    auto nClass = pstGetClasses(pst,NULL,0,classes.data(),classes.size()*sizeof(PARTCLASS));
    auto n = nClass / sizeof(PARTCLASS);
    assert(n*sizeof(PARTCLASS)==nClass);
    classes.resize(n);
    std::sort(classes.begin(),classes.end());
    pstSetClasses(pst,classes.data(),nClass,NULL,0);
    }

void MSR::SwapClasses(int id) {
    LCL *plcl = pst->plcl;
    PST pst0 = pst;
    int n;
    int rID;

    std::unique_ptr<PARTCLASS[]> pClass {new PARTCLASS[PKD_MAX_CLASSES]};
    n = pkdGetClasses( plcl->pkd, PKD_MAX_CLASSES, pClass.get() );
    rID = mdlReqService(pst0->mdl,id,PST_SWAPCLASSES,pClass.get(),n*sizeof(PARTCLASS));
    mdlGetReply(pst0->mdl,rID,pClass.get(),&n);
    n = n / sizeof(PARTCLASS);
    pkdSetClasses( plcl->pkd, n, pClass.get(), 0 );
    }

void MSR::OneNodeRead(struct inReadFile *in, FIO fio) {
    int id;
    uint64_t nStart;
    PST pst0;
    LCL *plcl;
    int nid;
    ServiceSwapAll::input inswap;
    int rID;

    std::unique_ptr<int[]> nParts {new int[nThreads]};
    for (id=0;id<nThreads;++id) {
	nParts[id] = -1;
	}

    nid = pstOneNodeReadInit(pst, in, sizeof(*in), nParts.get(), nThreads*sizeof(nParts[0]));
    assert((size_t)nid == nThreads*sizeof(nParts[0]));
    for (id=0;id<nThreads;++id) {
	assert(nParts[id] > 0);
	}

    pst0 = pst;
    while (pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;

    nStart = nParts[0];
    for (id=1;id<nThreads;++id) {
	/*
	 * Read particles into the local storage.
	 */
	assert(plcl->pkd->nStore >= nParts[id]);
	pkdReadFIO(plcl->pkd, fio, nStart, nParts[id], in->dvFac,in->dTuFac);
	nStart += nParts[id];
	/*
	 * Now shove them over to the remote processor.
	 */
	SwapClasses(id);
	inswap.idSwap = 0;
	rID = mdl->ReqService(id,PST_SWAPALL,&inswap,sizeof(inswap));
	//rID = mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
	pkdSwapAll(plcl->pkd, id);
	mdlGetReply(pst0->mdl,rID,NULL,NULL);
	}
    assert(nStart == N);
    /*
     * Now read our own particles.
     */
    pkdReadFIO(plcl->pkd, fio, 0, nParts[0], in->dvFac, in->dTuFac);
    }

double MSR::SwitchDelta(double dTime,double dDelta,int iStep,int nSteps) {
    if (csm->val.bComove && prmSpecified(prm,"dRedTo")
        && prmSpecified(prm,"nSteps") && prmSpecified(prm,"nSteps10")) {
	double aTo,tTo;
	if (iStep < param.nSteps10) {
	    aTo = 1.0 / (10.0 + 1.0);
	    nSteps = param.nSteps10 - iStep;
	    }
	else {
	    aTo = 1.0/(param.dRedTo + 1.0);
	    nSteps = nSteps - iStep;
	    }
	assert(nSteps>0);
	tTo = csmExp2Time(csm,aTo);
	dDelta = (tTo-dTime) / nSteps;
	if (iStep == param.nSteps10 && bVDetails)
	    printf("dDelta changed to %g at z=10\n",dDelta);
	}
    return dDelta;
    }

double MSR::getTime(double dExpansion) {
    if (csm->val.bComove) {
	if (csm->val.dHubble0 == 0.0) {
	    printf("No hubble constant specified\n");
	    Exit(1);
	    }
        return csmExp2Time(csm,dExpansion);
	}
    else return dExpansion;
    }

double MSR::getVfactor(double dExpansion) {
    return csm->val.bComove ? dExpansion*dExpansion : 1.0;
    }

void MSR::RecvArray(void *vBuffer,int field,int iUnitSize,double dTime,bool bMarked) {
    PKD pkd = pst->plcl->pkd;
    inSendArray in;
    in.field = field;
    in.iUnitSize = iUnitSize;
    in.bMarked = bMarked;
    if (csm->val.bComove) {
	auto dExp = csmTime2Exp(csm,dTime);
	in.dvFac = 1.0/(dExp*dExp);
	}
    else in.dvFac = 1.0;
    int iIndex = 0;
    vBuffer = pkdPackArray(pkd,iUnitSize*pkd->nLocal,vBuffer,&iIndex,pkd->nLocal,field,iUnitSize,in.dvFac,in.bMarked);
    assert (iIndex==pkd->nLocal);
    for(auto i=1; i<nThreads; ++i) {
	in.iTo = 0;
	auto rID = mdlReqService(pkd->mdl,i,PST_SENDARRAY,&in,sizeof(in));
	vBuffer = pkdRecvArray(pkd,i,vBuffer,iUnitSize);
	mdlGetReply(pkd->mdl,rID,NULL,NULL);
	}
    }

/*
** This function makes some potentially problematic assumptions!!!
** Main problem is that it calls pkd level routines, bypassing the
** pst level. It uses plcl pointer which is not desirable.
*/
void MSR::AllNodeWrite(const char *pszFileName, double dTime, double dvFac, int bDouble) {
    int nProcessors;
    PST pst0;
    LCL *plcl;
    struct inWrite in;

    pst0 = pst;
    while (pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;


    /*
    ** Add Data Subpath for local and non-local names.
    */
    MSR::MakePath(param.achDataSubPath,pszFileName,in.achOutFile);

    in.bStandard = param.bStandard;
    /*
    ** If bParaWrite is 0, then we write serially; if it is 1, then we write
    ** in parallel using all available threads, otherwise we write in parallel
    ** using the specified number of threads.  The latter option will reduce
    ** the total amount of simultaneous I/O for file systems that cannot
    ** handle it.
    */
    nProcessors = param.bParaWrite==0?1:(param.nParaWrite<=1 ? nThreads:param.nParaWrite);
    in.iIndex = 0;

    if (csm->val.bComove) {
	in.dTime = csmTime2Exp(csm,dTime);
	in.dvFac = 1.0/(in.dTime*in.dTime);
	}
    else {
	in.dTime = dTime;
	in.dvFac = 1.0;
	}

    /* We need to enforce periodic boundaries (when applicable) */
    if (param.bPeriodic &&
	    param.dxPeriod < FLOAT_MAXVAL &&
	    param.dyPeriod < FLOAT_MAXVAL &&
	    param.dzPeriod < FLOAT_MAXVAL) {
	Bound::coord offset(0.5*param.dxPeriod,0.5*param.dyPeriod,0.5*param.dzPeriod);
	in.bnd = Bound(fCenter-offset,fCenter+offset);
	}
    else {
	in.bnd = Bound(Bound::coord(-std::numeric_limits<double>::max()),Bound::coord(std::numeric_limits<double>::max()));
	}

    in.dEcosmo    = dEcosmo;
    in.dTimeOld   = dTimeOld;
    in.dUOld      = dUOld;
    in.dTuFac     = dTuFac;
    in.dBoxSize   = param.dBoxSize;
    in.Omega0     = csm->val.dOmega0;
    in.OmegaLambda= csm->val.dLambda;
    in.HubbleParam= param.h;

    in.nDark = nDark;
    in.nSph  = nGas;
    in.nStar = nStar;

    in.bHDF5 = param.bHDF5;
    in.mFlags = FIO_FLAG_POTENTIAL | FIO_FLAG_DENSITY
	| (bDouble?FIO_FLAG_CHECKPOINT:0)
	| (param.bDoublePos?FIO_FLAG_DOUBLE_POS:0)
	| (param.bDoubleVel?FIO_FLAG_DOUBLE_VEL:0)
	| (param.bMemMass?0:FIO_FLAG_COMPRESS_MASS)
	| (param.bMemSoft?0:FIO_FLAG_COMPRESS_SOFT);

    if (!param.bHDF5 && strstr(in.achOutFile,"&I")==0) {
	FIO fio;
	fio = fioTipsyCreate(in.achOutFile,
			     in.mFlags&FIO_FLAG_CHECKPOINT,
			     in.bStandard,in.dTime,
			     in.nSph, in.nDark, in.nStar);
	fioClose(fio);
	}
    in.iLower = 0;
    in.iUpper = nThreads;
    in.iIndex = 0;
    in.nProcessors = nProcessors;
    pstWrite(pst,&in,sizeof(in),NULL,0);
    }


uint64_t MSR::CalcWriteStart() {
    struct outSetTotal out;
    struct inSetWriteStart in;

    pstSetTotal(pst,NULL,0,&out,sizeof(out));
    assert(out.nTotal <= N);
    in.nWriteStart = 0;
    pstSetWriteStart(pst,&in,sizeof(in),NULL,0);
    return out.nTotal;
    }

void MSR::Write(const char *pszFileName,double dTime,int bCheckpoint) {
    char achOutFile[PST_FILENAME_SIZE];
    int nProcessors;
    double dvFac, dExp;
    double sec,dsec;
    uint64_t N;

#ifdef NAIVE_DOMAIN_DECOMP
    Reorder();
#else
    if ( iLastRungRT >= 0 ) Reorder();
#endif

    /*
    ** Calculate where to start writing.
    ** This sets plcl->nWriteStart.
    */
    N = CalcWriteStart();
    /*
    ** Add Data Subpath for local and non-local names.
    */
    MSR::MakePath(param.achDataSubPath,pszFileName,achOutFile);

    /*
    ** Count the number of particles per species
    */
    struct outGetNParts counts;
    bzero(&counts, sizeof(counts));
    pstCountSpecies(pst, NULL, 0, &counts, sizeof(counts));

    /*
    ** If bParaWrite is 0, then we write serially; if it is 1, then we write
    ** in parallel using all available threads, otherwise we write in parallel
    ** using the specified number of threads.  The latter option will reduce
    ** the total amount of simultaneous I/O for file systems that cannot
    ** handle it.
    */
    nProcessors = param.bParaWrite==0?1:(param.nParaWrite<=1 ? nThreads:param.nParaWrite);

    /*
    ** Determine the number of particles per species per parallel writer.
    ** This requires recursing in the same way as pstWrite
    */
    struct inWriteSpeciesCounts countsIn;
    countsIn.iLower = 0;
    countsIn.iUpper = nThreads;
    countsIn.iIndex = 0;
    countsIn.nProcessors = nProcessors;
    pstSetWriteSpeciesCounts(pst,&countsIn,sizeof(countsIn),NULL,0);

    if (csm->val.bComove) {
	dExp = csmTime2Exp(csm,dTime);
	dvFac = 1.0/(dExp*dExp);
	}
    else {
	dExp = dTime;
	dvFac = 1.0;
	}
    if ( !param.bParaWrite ) {
	msrprintf("Writing %s in %s format serially ...\n",
		  achOutFile, (param.bHDF5?"HDF5":"Tipsy"));
	}
    else {
	if ( param.nParaWrite > 1 )
	    msrprintf("Writing %s in %s format in parallel (but limited to %d processors) ...\n",
		      achOutFile, (param.bHDF5?"HDF5":"Tipsy"), nProcessors);
	else
	    msrprintf("Writing %s in %s format in parallel ...\n",
		      achOutFile, (param.bHDF5?"HDF5":"Tipsy"));
	}

    if (csm->val.bComove)
	msrprintf("Time:%g Redshift:%g\n",dTime,(1.0/dExp - 1.0));
    else
	msrprintf("Time:%g\n",dTime);

    sec = MSR::Time();
    AllNodeWrite(achOutFile, dTime, dvFac, bCheckpoint);
    dsec = MSR::Time() - sec;

    msrprintf("Output file has been successfully written, Wallclock: %f secs.\n", dsec);
    }


void MSR::SetSoft(double dSoft) {
    msrprintf("Set Softening...\n");
    ServiceSetSoft::input in(dSoft);
    mdl->RunService(PST_SETSOFT,sizeof(in),&in);
    }


void MSR::DomainDecompOld(int iRung) {
    OldDD::ServiceDomainDecomp::input in;
    uint64_t nActive;
    const uint64_t nDT = d2u64(N*param.dFracDualTree);
    const uint64_t nDD = d2u64(N*param.dFracNoDomainDecomp);
    const uint64_t nRT = d2u64(N*param.dFracNoDomainRootFind);
    const uint64_t nSD = d2u64(N*param.dFracNoDomainDimChoice);
    double sec,dsec;
    int iRungDT, iRungDD=0,iRungRT,iRungSD;
    int i;
    int bRestoreActive = 0;

    in.bDoRootFind = 1;
    in.bDoSplitDimFind = 1;
    if (iRung >= 0) {
	/*
	** All of this could be calculated once for the case that the number
	** of particles don't change. Or calculated every time the number of
	** particles does change.
	*/
	nActive = 0;
	iRungDT = 0;
	iRungDD = 0;
	iRungRT = 0;
	iRungSD = 0;
	for (i=iCurrMaxRung;i>=0;--i) {
	    nActive += nRung[i];
	    if (nActive > nDT && !iRungDT) iRungDT = i;
	    if (nActive > nDD && !iRungDD) iRungDD = i;
	    if (nActive > nRT && !iRungRT) iRungRT = i;
	    if (nActive > nSD && !iRungSD) iRungSD = i;
	    }
	assert(iRungDD >= iRungRT);
	assert(iRungRT >= iRungSD);
#ifdef NAIVE_DOMAIN_DECOMP
	if (iLastRungRT < 0) {
	    /*
	    ** We need to do a full domain decompotition with iRungRT particles being active.
	    ** However, since I am not sure what the exact state of the domains can be at this point
	    ** I had better do a split dim find as well.
	    */
	    iLastRungRT = 0;
	    ActiveRung(iLastRungRT,1);
	    bRestoreActive = 1;
	    in.bDoRootFind = 1;
	    in.bDoSplitDimFind = 1;
	    if (param.bVRungStat) {
		printf("Doing Domain Decomposition (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungRT:%d)\n",
		    nActive,N,iRung,iRungRT);
		}
	    }
	else if (iRung <= iRungRT) {
	    /*
	    ** We need to do a full domain decomposition with *ALL* particles being active.
	    */
	    in.bDoRootFind = 1;
	    if (iRung <= iRungSD) {
		if (param.bVRungStat) {
		    printf("Doing Domain Decomposition (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungRT:%d)\n",
			nActive,N,iRung,iRungRT);
		    }
		in.bDoSplitDimFind = 1;
		}
	    else { 
		if (param.bVRungStat) {
		    printf("Skipping Domain Dim Choice (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungSD:%d)\n",
			nActive,N,iRung,iRungSD);
		    }
		in.bDoSplitDimFind = 0;
		}
	    ActiveRung(0,1); /* Here we activate all particles. */
	    bRestoreActive = 1;
	    }	    
	else if (iRung <= iRungDD) {
	    if (param.bVRungStat) {
		printf("Skipping Root Finder (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungRT:%d iRungDD:%d)\n",
		    nActive,N,iRung,iRungRT,iRungDD);
		}
	    in.bDoRootFind = 0;
	    in.bDoSplitDimFind = 0;
	    bRestoreActive = 0;	    
	    }
	else {
	    if (param.bVRungStat) {
		printf("Skipping Domain Decomposition (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungDD:%d)\n",
		    nActive,N,iRung,iRungDD);
		}
	    return; /* do absolutely nothing! */
	    }
#else
	if (iLastRungRT < 0) {
	    /*
	    ** We need to do a full domain decompotition with iRungRT particles being active.
	    ** However, since I am not sure what the exact state of the domains can be at this point
	    ** I had better do a split dim find as well.
	    */
	    iLastRungRT = iRungRT;
	    ActiveRung(iRungRT,1);
	    bRestoreActive = 1;
	    in.bDoRootFind = 1;
	    in.bDoSplitDimFind = 1;
	    }
	else if (iRung == iLastRungDD) {
	    if (param.bVRungStat) {
		printf("Skipping Domain Decomposition (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungDD:%d iLastRungRT:%d)\n",
		    nActive,N,iRung,iRungDD,iLastRungRT);
		}
	    return;  /* do absolutely nothing! */
	    }
	else if (iRung >= iRungDD && !bSplitVA) {
	    if (iLastRungRT < iRungRT) {
		iLastRungRT = iRungRT;
		ActiveRung(iRungRT,1);
		bRestoreActive = 1;
		in.bDoRootFind = 1;
		in.bDoSplitDimFind = 0;
		}
	    else {
		if (param.bVRungStat) {
		    printf("Skipping Domain Decomposition (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungDD:%d iLastRungRT:%d)\n",
			nActive,N,iRung,iRungDD,iLastRungRT);
		    }
		return;  /* do absolutely nothing! */
		}
	    }
	else if (iRung > iRungRT) {
	    if (iLastRungRT < iRungRT) {
		iLastRungRT = iRungRT;
		ActiveRung(iRungRT,1);
		bRestoreActive = 1;
		in.bDoRootFind = 1;
		in.bDoSplitDimFind = 0;
		}
	    else {
		if (param.bVRungStat) {
		    printf("Skipping Root Finder (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungRT:%d iRungDD:%d iLastRungRT:%d)\n",
			nActive,N,iRung,iRungRT,iRungDD,iLastRungRT);
		    }
		in.bDoRootFind = 0;
		in.bDoSplitDimFind = 0;
		}
	    }
	else if (iRung > iRungSD) {
	    if (iLastRungRT == iRung) {
		if (param.bVRungStat) {
		    printf("Skipping Root Finder (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungRT:%d iRungDD:%d iLastRungRT:%d)\n",
			nActive,N,iRung,iRungRT,iRungDD,iLastRungRT);
		    }
		in.bDoRootFind = 0;
		in.bDoSplitDimFind = 0;
		}
	    else {
		if (param.bVRungStat) {
		    printf("Skipping Domain Dim Choice (nActive = %" PRIu64 "/%" PRIu64 ", iRung:%d iRungSD:%d iLastRungRT:%d)\n",
			nActive,N,iRung,iRungSD,iLastRungRT);
		    }
		iLastRungRT = iRung;
		in.bDoRootFind = 1;
		in.bDoSplitDimFind = 0;
		}
	    }
	else {
	    if (iLastRungRT == iRung) {
		in.bDoRootFind = 0;
		in.bDoSplitDimFind = 0;
		}
	    else {
		iLastRungRT = iRung;
		in.bDoRootFind = 1;
		in.bDoSplitDimFind = 1;
		}
	    }
#endif
	}
    else nActive = N;
    iLastRungDD = iLastRungRT;
    in.nActive = nActive;
    in.nTotal = N;

    in.nBndWrap[0] = 0;
    in.nBndWrap[1] = 0;
    in.nBndWrap[2] = 0;
    /*
    ** If we are dealing with a nice periodic volume in all
    ** three dimensions then we can set the initial bounds
    ** instead of calculating them.
    */
    if (param.bPeriodic &&
	param.dxPeriod < FLOAT_MAXVAL &&
	param.dyPeriod < FLOAT_MAXVAL &&
	param.dzPeriod < FLOAT_MAXVAL) {
	Bound::coord offset(0.5*param.dxPeriod,0.5*param.dyPeriod,0.5*param.dzPeriod);
	in.bnd = Bound(fCenter-offset,fCenter+offset);
	mdl->RunService(PST_ENFORCEPERIODIC,sizeof(in.bnd),&in.bnd);
	}
    else {
	mdl->RunService(PST_COMBINEBOUND,&in.bnd);
	}
    /*
    ** If we are doing SPH we need to make absolutely certain to clear
    ** all neighbor lists here since the pointers in the particles will
    ** only be valid on the node where it was malloc'ed!
    */
#ifdef FAST_GAS
    if (param.bDoGas) {
	pstFastGasCleanup(pst,NULL,0,NULL,0);
	}
#endif
    msrprintf("Domain Decomposition: nActive (Rung %d) %" PRIu64 "\n",
	iLastRungRT,nActive);
    msrprintf("Domain Decomposition... \n");
    sec = MSR::Time();

    mdl->RunService(PST_DOMAINDECOMP,sizeof(in),&in);
    dsec = MSR::Time() - sec;
    printf("Domain Decomposition complete, Wallclock: %f secs\n\n",dsec);
    if (bRestoreActive) {
	/* Restore Active data */
	ActiveRung(iRung,1);
	}
    }

void MSR::DomainDecomp(int iRung) {
    DomainDecompOld(iRung);
    }

/*
** This the meat of the tree build, but will be called by differently named
** functions in order to implement special features without recoding...
*/
void MSR::BuildTree(int bNeedEwald,uint32_t uRoot,uint32_t utRoot) {
    struct inBuildTree in;
    const double ddHonHLimit = param.ddHonHLimit;
    PST pst0;
    LCL *plcl;
    PKD pkd;
    double sec,dsec;

    pst0 = pst;
    while (pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;
    pkd = plcl->pkd;

    auto nTopTree = pkdNodeSize(pkd) * (2*nThreads-1);
    auto nMsgSize = sizeof(ServiceDistribTopTree::input) + nTopTree;

    std::unique_ptr<char[]> buffer {new char[nMsgSize]};
    auto pDistribTop = new (buffer.get()) ServiceDistribTopTree::input;
    auto pkdn = reinterpret_cast<KDN*>(pDistribTop + 1);
    pDistribTop->uRoot = uRoot;

    in.nBucket = param.nBucket;
    in.nGroup = param.nGroup;
    in.uRoot = uRoot;
    in.utRoot = utRoot;
    in.ddHonHLimit = ddHonHLimit;
    sec = MSR::Time();
    nTopTree = pstBuildTree(pst,&in,sizeof(in),pkdn,nTopTree);
    pDistribTop->nTop = nTopTree / pkdNodeSize(pkd);
    assert(pDistribTop->nTop == (2*nThreads-1));
    mdl->RunService(PST_DISTRIBTOPTREE,nMsgSize,pDistribTop);
    dsec = MSR::Time() - sec;
    printf("Tree built, Wallclock: %f secs\n\n",dsec);

    if (bNeedEwald) {
	/*
	** For simplicity we will skip calculating the Root for all particles
	** with exclude very active since there are missing particles which
	** could add to the mass and because it probably is not important to
	** update the root so frequently.
	*/
	ServiceCalcRoot::input calc;
	ServiceCalcRoot::output root;
	pkdNodeGetPos(pkd,pkdn,calc.com);
	calc.uRoot = uRoot;

	mdl->RunService(PST_CALCROOT,sizeof(calc),&calc,&root);
	momTreeRoot[uRoot] = root.momc;
	momTreeCom[uRoot][0] = calc.com[0];
	momTreeCom[uRoot][1] = calc.com[1];
	momTreeCom[uRoot][2] = calc.com[2];
	}
    }

void MSR::BuildTree(int bNeedEwald) {
    msrprintf("Building local trees...\n\n");

    ServiceDumpTrees::input dump(IRUNGMAX);
    mdl->RunService(PST_DUMPTREES,sizeof(dump),&dump);
    BuildTree(bNeedEwald,ROOT,0);

    if (bNeedEwald) {
	ServiceDistribRoot::input droot;
	droot.momc = momTreeRoot[ROOT];
	droot.r[0] = momTreeCom[ROOT][0];
	droot.r[1] = momTreeCom[ROOT][1];
	droot.r[2] = momTreeCom[ROOT][2];
	mdl->RunService(PST_DISTRIBROOT,sizeof(droot),&droot);
	}
    }

/*
** Separates the particles into two trees, and builds the "fixed" tree.
*/
void MSR::BuildTreeFixed(int bNeedEwald,uint8_t uRungDD) {
    msrprintf("Building fixed local trees...\n\n");
    BuildTree(bNeedEwald,FIXROOT,0);
    }

void MSR::BuildTreeActive(int bNeedEwald,uint8_t uRungDD) {
   /*
    ** The trees reset/removed. This does the following:
    **   1. Closes any open cell cache (it will be subsequently invalid)
    **   2. Resets the number of used nodes to zero (or more if we keep the main tree)
    **   3. Sets up the ROOT and FIXROOT node (either of which may have zero particles).
    */

    msrprintf("Building active local trees...\n\n");

    ServiceDumpTrees::input dump(uRungDD,true);
    mdl->RunService(PST_DUMPTREES,sizeof(dump),&dump);

    /* New build the very active tree */
    BuildTree(bNeedEwald,ROOT,FIXROOT);

    /* For ewald we have to shift and combine the individual tree moments */
    if (bNeedEwald) {
	ServiceDistribRoot::input droot;
	MOMC momc;
	double *com1 = momTreeCom[FIXROOT];
	double    m1 = momTreeRoot[FIXROOT].m;
	double *com2 = momTreeCom[ROOT];
	double    m2 = momTreeRoot[ROOT].m;
	double ifMass = 1.0 / (m1 + m2);
	double x, y, z;
	int j;

	/* New Center of Mass, then shift and scale the moments */
	for (j=0;j<3;++j) droot.r[j] = ifMass*(m1*com1[j] + m2*com2[j]);

	droot.momc = momTreeRoot[FIXROOT];
	x = com1[0] - droot.r[0];
	y = com1[1] - droot.r[1];
	z = com1[2] - droot.r[2];
	momShiftMomc(&droot.momc,x,y,z);

	momc = momTreeRoot[ROOT];
	x = com2[0] - droot.r[0];
	y = com2[1] - droot.r[1];
	z = com2[2] - droot.r[2];
	momShiftMomc(&momc,x,y,z);

	momAddMomc(&droot.momc, &momc);

	mdl->RunService(PST_DISTRIBROOT,sizeof(droot),&droot);
	}
    }

void MSR::BuildTreeMarked(int bNeedEwald) {
    ServiceDumpTrees::input dump(IRUNGMAX);
    mdl->RunService(PST_DUMPTREES,sizeof(dump),&dump);

    pstTreeInitMarked(pst,NULL,0,NULL,0);
    BuildTree(bNeedEwald,FIXROOT,0);
    BuildTree(bNeedEwald,ROOT,FIXROOT);

    /* For ewald we have to shift and combine the individual tree moments */
    if (bNeedEwald) {
	ServiceDistribRoot::input droot;
	MOMC momc;
	double *com1 = momTreeCom[FIXROOT];
	double    m1 = momTreeRoot[FIXROOT].m;
	double *com2 = momTreeCom[ROOT];
	double    m2 = momTreeRoot[ROOT].m;
	double ifMass = 1.0 / (m1 + m2);
	double x, y, z;
	int j;

	/* New Center of Mass, then shift and scale the moments */
	for (j=0;j<3;++j) droot.r[j] = ifMass*(m1*com1[j] + m2*com2[j]);

	droot.momc = momTreeRoot[FIXROOT];
	x = com1[0] - droot.r[0];
	y = com1[1] - droot.r[1];
	z = com1[2] - droot.r[2];
	momShiftMomc(&droot.momc,x,y,z);

	momc = momTreeRoot[ROOT];
	x = com2[0] - droot.r[0];
	y = com2[1] - droot.r[1];
	z = com2[2] - droot.r[2];
	momShiftMomc(&momc,x,y,z);

	momAddMomc(&droot.momc, &momc);

	mdl->RunService(PST_DISTRIBROOT,sizeof(droot),&droot);
	}
    }

void MSR::Reorder() {
    if (!param.bMemUnordered) {
	double sec,dsec;

	msrprintf("Ordering...\n");
	sec = Time();
	OldDD::ServiceDomainOrder::input indomain(MaxOrder()-1);
	mdl->RunService(PST_DOMAINORDER,sizeof(indomain),&indomain);

	OldDD::ServiceLocalOrder::input inlocal(MaxOrder()-1);
	mdl->RunService(PST_LOCALORDER,sizeof(inlocal),&inlocal);
	dsec = Time() - sec;
	msrprintf("Order established, Wallclock: %f secs\n\n",dsec);

	/*
	** Mark domain decomp as not done.
	*/
	iLastRungRT = -1;
	iLastRungDD = -1;
	}
    }

void MSR::OutASCII(const char *pszFile,int iType,int nDims,int iFileType) {

    char achOutFile[PST_FILENAME_SIZE];
    LCL *plcl;
    PST pst0;
    int id,iDim;
    ServiceSwapAll::input inswap;
    PKDOUT pkdout;
    const char *arrayOrVector;
    struct outSetTotal total;
    int rID;

    switch(nDims) {
    case 1: arrayOrVector = "array";  break;
    case 3: arrayOrVector = "vector"; break;
    default:arrayOrVector = NULL;
	    assert(nDims==1 || nDims==3);
	}

    pst0 = pst;
    while (pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;

    pstSetTotal(pst,NULL,0,&total,sizeof(total));

    if (pszFile) {
	/*
	** Add Data Subpath for local and non-local names.
	*/
	MSR::MakePath(param.achDataSubPath,pszFile,achOutFile);

	switch(iFileType) {
#ifdef HAVE_LIBBZ2
	case PKDOUT_TYPE_BZIP2:
	    strcat(achOutFile,".bz2");
	    break;
#endif
#ifdef HAVE_LIBZ
	case PKDOUT_TYPE_ZLIB:
	    strcat(achOutFile,".gz");
	    break;
#endif
	default:
	    break;
	    }

	msrprintf( "Writing %s to %s\n", arrayOrVector, achOutFile );
	}
    else {
	printf("No %s Output File specified\n", arrayOrVector);
	Exit(1);
	return;
	}

    if (param.bParaWrite && iFileType>1) {
	struct inCompressASCII in;
	struct outCompressASCII out;
	struct inWriteASCII inWrite;
	FILE *fp;

	fp = fopen(achOutFile,"wb");
	if ( fp==NULL) {
	    printf("Could not create %s Output File:%s\n",arrayOrVector, achOutFile);
	    Exit(1);
	    }
	fclose(fp);

	inWrite.nFileOffset = 0;
	for( iDim=0; iDim<nDims; iDim++ ) {
	    in.nTotal = total.nTotal;
	    in.iFile = iFileType;
	    in.iType = iType;
	    in.iDim = iDim;
	    pstCompressASCII(pst,&in,sizeof(in),&out,sizeof(out));
	    strcpy(inWrite.achOutFile,achOutFile);
	    pstWriteASCII(pst,&inWrite,sizeof(inWrite),NULL,0);
	    inWrite.nFileOffset += out.nBytes;
	    }
	}
    else {
	pkdout = pkdOpenOutASCII(plcl->pkd,achOutFile,"wb",iFileType,iType);
	if (!pkdout) {
	    printf("Could not open %s Output File:%s\n",arrayOrVector,achOutFile);
	    Exit(1);
	    }

	pkdOutHdr(plcl->pkd,pkdout,total.nTotal);

	/*
	 * First write our own particles.
	 */
	for (iDim=0;iDim<nDims;++iDim) {
	    pkdOutASCII(plcl->pkd,pkdout,iType,iDim);
	    for (id=1;id<nThreads;++id) {
		/*
		 * Swap particles with the remote processor.
		 */
		inswap.idSwap = 0;
		rID = mdl->ReqService(id,PST_SWAPALL,&inswap,sizeof(inswap));
		//rID = mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
		pkdSwapAll(plcl->pkd, id);
		mdlGetReply(pst0->mdl,rID,NULL,NULL);
		/*
		 * Write the swapped particles.
		 */
		pkdOutASCII(plcl->pkd,pkdout,iType,iDim);
		/*
		 * Swap them back again.
		 */
		inswap.idSwap = 0;
		rID = mdl->ReqService(id,PST_SWAPALL,&inswap,sizeof(inswap));
		//rID = mdlReqService(pst0->mdl,id,PST_SWAPALL,&inswap,sizeof(inswap));
		pkdSwapAll(plcl->pkd,id);
		mdlGetReply(pst0->mdl,rID,NULL,NULL);
		}
	    }
	pkdCloseOutASCII(plcl->pkd,pkdout);
	}
    }

void MSR::OutArray(const char *pszFile,int iType,int iFileType) {
    OutASCII(pszFile,iType,1,iFileType);
    }
void MSR::OutArray(const char *pszFile,int iType) {
    OutArray(pszFile,iType,param.iCompress);
    }

void MSR::OutVector(const char *pszFile,int iType,int iFileType) {
    OutASCII(pszFile,iType,3,iFileType);
    }
void MSR::OutVector(const char *pszFile,int iType) {
    OutVector(pszFile,iType,param.iCompress);
    }

void MSR::SmoothSetSMF(SMF *smf, double dTime, double dDelta) {
    smf->dTime = dTime;
    smf->iMaxRung = param.iMaxRung;
    if (Comove()) {
	smf->bComove = 1;
	smf->H = csmTime2Hub(csm,dTime);
	smf->a = csmTime2Exp(csm,dTime);
	}
    else {
	smf->bComove = 0;
	smf->H = 0.0;
	smf->a = 1.0;
	}
    smf->alpha = param.dConstAlpha;
    smf->beta = param.dConstBeta;
    smf->gamma = param.dConstGamma;
    smf->dDelta = dDelta;
    smf->dEtaCourant = param.dEtaCourant;
    smf->iViscosityLimiter = param.iViscosityLimiter;
    smf->iDiffusion = param.iDiffusion;
    smf->dMetalDiffusionCoeff = param.dMetalDiffusionCoeff;
    smf->dThermalDiffusionCoeff = param.dThermalDiffusionCoeff;
    /* For SF & FB in code units */
#define SECONDSPERYEAR   31557600.
    if (param.bGasIsothermal) smf->SFdESNPerStarMass = 0;
    else smf->SFdESNPerStarMass = param.SFdESNPerStarMass/param.dErgPerGmUnit;
    smf->SFdtCoolingShutoff = param.SFdtCoolingShutoff*SECONDSPERYEAR/param.dSecUnit;
    /* avoid alignment in steps + feedback time -- increase FB time by small tweak */
    smf->SFdtFeedbackDelay = param.SFdtFeedbackDelay*1.0000013254678*SECONDSPERYEAR/param.dSecUnit;
    smf->SFdMassLossPerStarMass = param.SFdMassLossPerStarMass;
    smf->SFdZMassPerStarMass = param.SFdZMassPerStarMass;
    smf->SFdFBFac = 0.5/((1+0.6*smf->alpha)/(smf->a*smf->dEtaCourant))
	/(param.SFdvFB/param.dKmPerSecUnit);
    }

void MSR::Smooth(double dTime,double dDelta,int iSmoothType,int bSymmetric,int nSmooth) {
    struct inSmooth in;

    in.nSmooth = nSmooth;
    in.bPeriodic = param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    SmoothSetSMF(&(in.smf), dTime, dDelta);
    if (param.bVStep) {
	double sec,dsec;
	printf("Smoothing...\n");
	sec = MSR::Time();
	pstSmooth(pst,&in,sizeof(in),NULL,0);
	dsec = MSR::Time() - sec;
	printf("Smooth Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstSmooth(pst,&in,sizeof(in),NULL,0);
	}
    }


#ifdef FAST_GAS
void MSR::FastGasPhase1(double dTime,double dDelta,int iSmoothType) {
    struct inSmooth in;

    in.nSmooth = param.nSmooth;
    in.bPeriodic = param.bPeriodic;
    in.bSymmetric = 0;
    in.iSmoothType = iSmoothType;
    SmoothSetSMF(&(in.smf), dTime, dDelta);
    if (param.bVStep) {
	double sec,dsec;
	printf("FastGas Phase 1 Smoothing...\n");
	sec = MSR::Time();
	pstFastGasPhase1(pst,&in,sizeof(in),NULL,0);
	dsec = MSR::Time() - sec;
	printf("FastGas Phase 1 Smooth Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstFastGasPhase1(pst,&in,sizeof(in),NULL,0);
	}
    }


void MSR::FastGasPhase2(double dTime,double dDelta,int iSmoothType) {
    struct inSmooth in;

    in.nSmooth = param.nSmooth;
    in.bPeriodic = param.bPeriodic;
    in.bSymmetric = 0;
    in.iSmoothType = iSmoothType;
    SmoothSetSMF(&(in.smf), dTime, dDelta);
    if (param.bVStep) {
	double sec,dsec;
	printf("FastGas Phase 2 Smoothing...\n");
	sec = MSR::Time();
	pstFastGasPhase2(pst,&in,sizeof(in),NULL,0);
	dsec = MSR::Time() - sec;
	printf("FastGas Phase 2 Smooth Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstFastGasPhase2(pst,&in,sizeof(in),NULL,0);
	}
    }
#endif

void MSR::ReSmooth(double dTime,double dDelta,int iSmoothType,int bSymmetric) {
    struct inSmooth in;

    in.nSmooth = param.nSmooth;
    in.bPeriodic = param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    SmoothSetSMF(&(in.smf), dTime, dDelta);
    if (param.bVStep) {
	double sec,dsec;
	printf("ReSmoothing...\n");
	sec = MSR::Time();
	pstReSmooth(pst,&in,sizeof(in),NULL,0);
	dsec = MSR::Time() - sec;
	printf("ReSmooth Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstReSmooth(pst,&in,sizeof(in),NULL,0);
	}
    }

void MSR::UpdateSoft(double dTime) {
    if (!(param.bPhysicalSoft)) return;
    if (param.bPhysicalSoft) {
	struct inPhysicalSoft in;

	in.dFac = 1./csmTime2Exp(csm,dTime);
	in.bSoftMaxMul = param.bSoftMaxMul;
	in.dSoftMax = param.dSoftMax;

	if (param.bSoftMaxMul && in.dFac > in.dSoftMax) in.dFac = in.dSoftMax;

	pstPhysicalSoft(pst,&in,sizeof(in),NULL,0);
	}
    }

#define PRINTGRID(w,FRM,VAR) {						\
    printf("      % *d % *d % *d % *d % *d % *d % *d % *d % *d % *d\n",\
	   w,0,w,1,w,2,w,3,w,4,w,5,w,6,w,7,w,8,w,9);		       \
    for (i=0;i<nThreads/10;++i) {\
	printf("%4d: " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM "\n",i*10,\
	       out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR,out[i*10+4].VAR,\
	       out[i*10+5].VAR,out[i*10+6].VAR,out[i*10+7].VAR,out[i*10+8].VAR,out[i*10+9].VAR);\
	}\
    switch (nThreads%10) {\
    case 0: break;\
    case 1: printf("%4d: " FRM "\n",i*10,\
		   out[i*10+0].VAR); break;\
    case 2: printf("%4d: " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR); break;\
    case 3: printf("%4d: " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR); break;\
    case 4: printf("%4d: " FRM " " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR); break;\
    case 5: printf("%4d: " FRM " " FRM " " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR,out[i*10+4].VAR); break;\
    case 6: printf("%4d: " FRM " " FRM " " FRM " " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR,out[i*10+4].VAR,\
		   out[i*10+5].VAR); break;\
    case 7: printf("%4d: " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR,out[i*10+4].VAR,\
		   out[i*10+5].VAR,out[i*10+6].VAR); break;\
    case 8: printf("%4d: " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR,out[i*10+4].VAR,\
		   out[i*10+5].VAR,out[i*10+6].VAR,out[i*10+7].VAR); break;\
    case 9: printf("%4d: " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM " " FRM "\n",i*10,\
		   out[i*10+0].VAR,out[i*10+1].VAR,out[i*10+2].VAR,out[i*10+3].VAR,out[i*10+4].VAR,\
		   out[i*10+5].VAR,out[i*10+6].VAR,out[i*10+7].VAR,out[i*10+8].VAR); break;\
    }\
}

void MSR::Hostname() {
    int i;
    std::unique_ptr<ServiceHostname::output[]> out {new ServiceHostname::output[nThreads]};
    mdl->RunService(PST_HOSTNAME,out.get());
    printf("Host Names:\n");
    PRINTGRID(12,"%12.12s",szHostname);
    printf("MPI Rank:\n");
    PRINTGRID(8,"% 8d",iMpiID);
    }

void MSR::MemStatus() {
    int i;
    if (bVDetails) {
	std::unique_ptr<struct outMemStatus[]> out {new struct outMemStatus[nThreads]};
	pstMemStatus(pst,0,0,out.get(),nThreads*sizeof(struct outMemStatus));
#ifdef __linux__
	printf("Resident (MB):\n");
	PRINTGRID(8,"%8" PRIu64,rss);
	printf("Free Memory (MB):\n");
	PRINTGRID(8,"%8" PRIu64,freeMemory);
#endif
	printf("Tree size (MB):\n");
	PRINTGRID(8,"%8" PRIu64,nBytesTree/1024/1024);
	printf("Checklist size (KB):\n");
	PRINTGRID(8,"%8" PRIu64,nBytesCl/1024);
	printf("Particle List size (KB):\n");
	PRINTGRID(8,"%8" PRIu64,nBytesIlp/1024);
	printf("Cell List size (KB):\n");
	PRINTGRID(8,"%8" PRIu64,nBytesIlc/1024);
	}
    }

void msrPrintStat(STAT *ps,char const *pszPrefix,int p) {
    double dSum = ps->dSum;
    double dMax = ps->dMax;
    const char *minmax = "max";

    if (dSum<0) {
	dSum = -dSum;
	dMax = -dMax;
	minmax = "min";
	}
    if (ps->n > 1) {
	printf("%s %s=%8.*f @%5d avg=%8.*f of %5d std-dev=%8.*f\n",pszPrefix,minmax,
	    p,dMax,ps->idMax,p,dSum/ps->n,ps->n,p,sqrt((ps->dSum2 - ps->dSum*ps->dSum/ps->n)/(ps->n-1)));
	}
    else if (ps->n == 1) {
	printf("%s %s=%8.*f @%5d\n",pszPrefix,minmax,p,dMax,ps->idMax);
	}
    else {
	printf("%s no data\n",pszPrefix);
	}	
    }


uint8_t MSR::Gravity(uint8_t uRungLo, uint8_t uRungHi,int iRoot1,int iRoot2,
	double dTime, double dDelta, double dStep, double dTheta,
	int bKickClose,int bKickOpen,int bEwald,int bGravStep,int nPartRhoLoc,int iTimeStepCrit,int nGroup) {
    struct inGravity in;
    uint64_t nRungSum[IRUNGMAX+1];
    int i;
    double sec,dsec,dTotFlop,dt,a;
    double dTimeLCP;
    uint8_t uRungMax=0;
    char c;

    if (param.bVStep) printf("Calculating Gravity, Step:%f (rung %d)\n",dStep,uRungLo);
    in.dTime = dTime;
    in.iRoot1 = iRoot1;
    in.iRoot2 = iRoot2;

    in.nReps = param.nReplicas;
    in.nGroup = nGroup;
    in.dTheta = dTheta;

    in.bPeriodic = param.bPeriodic;
    in.bEwald = bEwald;
    in.dEwCut = param.dEwCut;
    in.dEwhCut = param.dEwhCut;

    // Parameters related to timestepping
    in.ts.iTimeStepCrit = iTimeStepCrit;
    in.ts.nPartRhoLoc = nPartRhoLoc;
    in.ts.nPartColl = param.nPartColl;
    in.ts.dEccFacMax = param.dEccFacMax;
    if (bGravStep) {
	double a = csmTime2Exp(csm,dTime);
	in.ts.dRhoFac = 1.0/(a*a*a);
	}
    else in.ts.dRhoFac = 0.0;
    in.ts.dDelta = dDelta;
    in.ts.dEta = param.dEta;
    in.ts.dPreFacRhoLoc = param.dPreFacRhoLoc;
    in.ts.bGravStep = bGravStep;
    in.ts.uRungLo = uRungLo;
    in.ts.uRungHi = uRungHi;
    in.ts.uMaxRung = param.iMaxRung;
    if (csm->val.bComove) {
	a = csmTime2Exp(csm,dTime);
	in.ts.dAccFac = 1.0/(a*a*a);
	}
    else in.ts.dAccFac = 1.0;

    /*
    ** Now calculate the timestepping factors for kick close and open if the
    ** gravity should kick the particles. If the code uses bKickClose and 
    ** bKickOpen it no longer needs to store accelerations per particle.
    */
    in.kick.bKickClose = bKickClose;
    in.kick.bKickOpen = bKickOpen;
    for (i=0,dt=0.5*dDelta;i<=param.iMaxRung;++i,dt*=0.5) {
	in.kick.dtClose[i] = 0.0;
	in.kick.dtOpen[i] = 0.0;
	if (i>=uRungLo) {
	    if (csm->val.bComove) {
		if (bKickClose) {
		    in.kick.dtClose[i] = csmComoveKickFac(csm,dTime-dt,dt);
		    }
		if (bKickOpen) {
		    in.kick.dtOpen[i] = csmComoveKickFac(csm,dTime,dt);
		    }
		}
	    else {
		if (bKickClose) in.kick.dtClose[i] = dt;
		if (bKickOpen) in.kick.dtOpen[i] = dt;
		}
	    }
	}

    in.lc.bLightConeParticles = param.bLightConeParticles;
    in.lc.dBoxSize = param.dBoxSize;
    if (param.bLightCone) {
	in.lc.dLookbackFac = csmComoveKickFac(csm,dTime,(csmExp2Time(csm,1.0) - dTime));
	dTimeLCP = csmExp2Time(csm,1.0/(1.0+param.dRedshiftLCP));
	in.lc.dLookbackFacLCP = csmComoveKickFac(csm,dTimeLCP,(csmExp2Time(csm,1.0) - dTimeLCP));
	}
    else {
	in.lc.dLookbackFac = 0.0;
	in.lc.dLookbackFacLCP = 0.0;
	}
    /*
    ** Note that in this loop we initialize dt with the full step, not a half step!
    */
    for (i=0,dt=dDelta;i<=param.iMaxRung;++i,dt*=0.5) {
	in.lc.dtLCDrift[i] = 0.0;
	in.lc.dtLCKick[i] = 0.0;
	if (i>=uRungLo) {
	    if (csm->val.bComove) {
		in.lc.dtLCDrift[i] = csmComoveDriftFac(csm,dTime,dt);
		in.lc.dtLCKick[i] = csmComoveKickFac(csm,dTime,dt);
		}
	    else {
	        in.lc.dtLCDrift[i] = dt;
	        in.lc.dtLCKick[i] = dt;
		}
	    }
	}

    outGravityReduct outr;

    sec = MSR::Time();
    pstGravity(pst,&in,sizeof(in),&outr,sizeof(outr));
    dsec = MSR::Time() - sec;

    if (bKickOpen) {
	for (i=IRUNGMAX;i>=uRungLo;--i) {
	    if (outr.nRung[i]) break;
	    }
	assert(i >= uRungLo);
	uRungMax = i;
	iCurrMaxRung = uRungMax;   /* this assignment shouldn't be needed */
	/*
	** Update only the active rung counts in the master rung counts.
	** We need to go all the way to IRUNGMAX to clear any prior counts at rungs 
	** deeper than the current uRungMax!
	*/
	for (i=uRungLo;i<=IRUNGMAX;++i) nRung[i] = outr.nRung[i];

	const uint64_t nDT = d2u64(N*param.dFracDualTree);
	const uint64_t nDD = d2u64(N*param.dFracNoDomainDecomp);
	uint64_t nActive = 0;
	iRungDD = 0;
	iRungDT = 0;
	for (i=iCurrMaxRung;i>=0;--i) {
	    nActive += nRung[i];
	    if (nActive > nDT && !iRungDT) iRungDT = i;
	    if (nActive > nDD && !iRungDD) iRungDD = i;
	    }
	}
    if (param.bVStep) {
	/*
	** Output some info...
	*/
	dTotFlop = outr.sFlop.dSum;
	if (dsec > 0.0) {
	    double dGFlops = dTotFlop/dsec;
	    printf("Gravity Calculated, Wallclock: %f secs, Gflops:%.1f, Total Gflop:%.3g\n",
		   dsec,dGFlops,dTotFlop);
	    printf("  Gflops: CPU:%.1f,%.1f GPU:%.1f,%.1f",
		outr.dFlopSingleCPU/dsec,outr.dFlopDoubleCPU/dsec,
		outr.dFlopSingleGPU/dsec,outr.dFlopDoubleGPU/dsec);
	    }
	else {
	    printf("Gravity Calculated, Wallclock: %f secs, Gflops:unknown, Total Gflop:%.3g\n",
		   dsec,dTotFlop);
	    }
	printf("  Gflop: CPU:%.3g,%.3g GPU:%.3g,%.3g\n",
	    outr.dFlopSingleCPU,outr.dFlopDoubleCPU,
	    outr.dFlopSingleGPU,outr.dFlopDoubleGPU);
	msrPrintStat(&outr.sLocal,         "  particle  load:",0);
	msrPrintStat(&outr.sActive,        "  actives   load:",0);
	msrPrintStat(&outr.sFlop,          "  Gflop     load:",1);
	msrPrintStat(&outr.sPart,          "  P-P per active:",2);
	msrPrintStat(&outr.sCell,          "  P-C per active:",2);
#ifdef INSTRUMENT
	msrPrintStat(&outr.sComputing,     "     % computing:",3);
	msrPrintStat(&outr.sWaiting,       "     %   waiting:",3);
	msrPrintStat(&outr.sSynchronizing, "     %   syncing:",3);
#endif
#ifdef __linux__
	msrPrintStat(&outr.sFreeMemory,    "free memory (GB):", 3);
	msrPrintStat(&outr.sRSS,           "   resident size:",3);
#endif
	printf("  (cache access statistics are given per active particle)\n");
	msrPrintStat(&outr.sPartNumAccess, "  P-cache access:",1);
	msrPrintStat(&outr.sCellNumAccess, "  C-cache access:",1);
	msrPrintStat(&outr.sPartMissRatio, "  P-cache miss %:",2);
	msrPrintStat(&outr.sCellMissRatio, "  C-cache miss %:",2);
	}
    if (param.bVRungStat && bKickOpen) {
	printf("Rung distribution:\n");
	printf("\n");
	nRungSum[uRungMax] = nRung[uRungMax];
	for (i=uRungMax-1;i>=0;--i) {
	    nRungSum[i] = nRungSum[i+1] + nRung[i];
	    }
	for (i=0;i<=uRungMax;++i) {
	    if (nRung[i]) break;
	    }
	if (nRungSum[0]>0) for (;i<=uRungMax;++i) {
	    c = ' ';
	    printf(" %c rung:%d %14" PRIu64 "    %14" PRIu64 "  %3.0f %%\n",
		c,i,nRung[i],nRungSum[i],
		ceil(100.0 * nRungSum[i] / nRungSum[0]));
	    }
	printf("\n");
	}
    return(uRungMax);
    }


void MSR::CalcEandL(int bFirst,double dTime,double *E,double *T,double *U,double *Eth,double *L,double *F,double *W) {
    struct outCalcEandL out;
    double a;
    int k;

    pstCalcEandL(pst,NULL,0,&out,sizeof(out));
    *T = out.T;
    *U = out.U;
    *Eth = out.Eth;
    for (k=0;k<3;k++) L[k] = out.L[k];
    for (k=0;k<3;k++) F[k] = out.F[k];
    *W = out.W;
    /*
    ** Do the comoving coordinates stuff.
    ** Currently L is not adjusted for this. Should it be?
    */
    a = csmTime2Exp(csm,dTime);
    if (!csm->val.bComove) *T *= pow(a,4.0);
    /*
     * Estimate integral (\dot a*U*dt) over the interval.
     * Note that this is equal to integral (W*da) and the latter
     * is more accurate when a is changing rapidly.
     */
    if (csm->val.bComove && !bFirst) {
	dEcosmo += 0.5*(a - csmTime2Exp(csm, dTimeOld))
			*((*U) + dUOld);
	}
    else {
	dEcosmo = 0.0;
	}
    dTimeOld = dTime;
    dUOld = *U;
    *U *= a;
    *E = (*T) + (*U) - dEcosmo + a*a*(*Eth);
    }


void MSR::Drift(double dTime,double dDelta,int iRoot) {
    struct inDrift in;

    if (csm->val.bComove) {
	in.dDelta = csmComoveDriftFac(csm,dTime,dDelta);
	in.dDeltaVPred = csmComoveKickFac(csm,dTime,dDelta);
	}
    else {
	in.dDelta = dDelta;
	in.dDeltaVPred = dDelta;
	}
    in.dTime = dTime;
    in.dDeltaUPred = dDelta;
    in.bDoGas = param.bDoGas;
    in.iRoot = iRoot;
    pstDrift(pst,&in,sizeof(in),NULL,0);
    }

/*
 * For gas, updates predicted velocities to beginning of timestep.
 */
void MSR::KickKDKOpen(double dTime,double dDelta,uint8_t uRungLo,uint8_t uRungHi) {
    struct inKick in;

    in.dTime = dTime;
    if (csm->val.bComove) {
	in.dDelta = csmComoveKickFac(csm,dTime,dDelta);
	in.dDeltaVPred = 0;
    }
    else {
	in.dDelta = dDelta;
	in.dDeltaVPred = 0;
    }
    in.dDeltaU = dDelta;
    in.dDeltaUPred = 0;
    in.uRungLo = uRungLo;
    in.uRungHi = uRungHi;
    in.bDoGas = param.bDoGas;
    pstKick(pst,&in,sizeof(in),NULL,0);
    }

/*
 * For gas, updates predicted velocities to end of timestep.
 */
void MSR::KickKDKClose(double dTime,double dDelta,uint8_t uRungLo,uint8_t uRungHi) {
    struct inKick in;

    in.dTime = dTime;
    if (csm->val.bComove) {
	in.dDelta = csmComoveKickFac(csm,dTime,dDelta);
	in.dDeltaVPred = in.dDelta;
    }
    else {
	in.dDelta = dDelta;
	in.dDeltaVPred = in.dDelta;
    }
    in.dDeltaU = dDelta;
    in.dDeltaUPred = in.dDeltaU;
    in.uRungLo = uRungLo;
    in.uRungHi = uRungHi;
    in.bDoGas = param.bDoGas;
    pstKick(pst,&in,sizeof(in),NULL,0);
    }

bool MSR::OutTime(double dTime) {
    if (dOutTimes.back() <= dTime) {
    	while (dOutTimes.back() <= dTime) dOutTimes.pop_back();
    	return true;
	}
    return false;
    }

int cmpTime(const void *v1,const void *v2) {
    double *d1 = (double *)v1;
    double *d2 = (double *)v2;

    if (*d1 < *d2) return(-1);
    else if (*d1 == *d2) return(0);
    else return(1);
    }

void MSR::ReadOuts(double dTime,double dDelta) {
    char achFile[PST_FILENAME_SIZE];
    FILE *fp;
    int i,ret;
    double z,a,t,n;
    char achIn[80];

    /*
    ** Add Data Subpath for local and non-local names.
    */
    MakePath(param.achDataSubPath,param.achOutName,achFile);
    strcat(achFile,".red");

    dOutTimes.clear();
    dOutTimes.push_back(INFINITY); // Sentinal node

    fp = fopen(achFile,"r");
    if (!fp) return;
    i = 0;
    while (1) {
	if (!fgets(achIn,80,fp)) goto NoMoreOuts;
	switch (achIn[0]) {
	case 'z':
	    ret = sscanf(&achIn[1],"%lf",&z);
	    if (ret != 1) goto NoMoreOuts;
	    a = 1.0/(z+1.0);
	    dOutTimes.push_back(csmExp2Time(csm,a));
	    break;
	case 'a':
	    ret = sscanf(&achIn[1],"%lf",&a);
	    if (ret != 1) goto NoMoreOuts;
	    dOutTimes.push_back(csmExp2Time(csm,a));
	    break;
	case 't':
	    ret = sscanf(&achIn[1],"%lf",&t);
	    if (ret != 1) goto NoMoreOuts;
	    dOutTimes.push_back(t);
	    break;
	case 'n':
	    ret = sscanf(&achIn[1],"%lf",&n);
	    if (ret != 1) goto NoMoreOuts;
	    dOutTimes.push_back(dTime + (n-0.5)*dDelta);
	    break;
	default:
	    ret = sscanf(achIn,"%lf",&z);
	    if (ret != 1) goto NoMoreOuts;
	    a = 1.0/(z+1.0);
	    dOutTimes.push_back(csmExp2Time(csm,a));
	    }
	}
NoMoreOuts:
    fclose(fp);
    // Sort in DECENDING order. We pop used values of the back. Sentinal is at the front
    std::sort(dOutTimes.begin(),dOutTimes.end(),std::greater<double>());
    }

/*
** Theta switch. Default is to use dTheta, then switch:
**   at z=20 to dTheta20
**   at z=2 to dTheta2
** We also adjust the number of replicas if the accuracy warrants it.
*/
double MSR::getTheta(double dTime) {
    if (Comove()) {
	double a = csmTime2Exp(csm,dTime);
	double dTheta;
	if (a < (1.0/21.0)) dTheta = param.dTheta;
	else if (a < (1.0/3.0)) dTheta = param.dTheta20;
	else dTheta = param.dTheta2;
	if ( !prmSpecified(prm,"nReplicas") && param.nReplicas>=1 ) {
	    if ( dTheta < 0.52 ) param.nReplicas = 2;
	    else param.nReplicas = 1;
	    }
	return dTheta;
	}
    else return param.dTheta;
    }

void MSR::InitCosmology() {
    mdl->RunService(PST_INITCOSMOLOGY,sizeof(csm->val),&csm->val);
    }

void MSR::ZeroNewRung(uint8_t uRungLo, uint8_t uRungHi, int uRung) {
    ServiceZeroNewRung::input in(uRung,uRungLo, uRungHi);
    mdl->RunService(PST_ZERONEWRUNG,sizeof(in),&in);
    }

/*
 * bGreater = 1 => activate all particles at this rung and greater.
 */
void MSR::ActiveRung(int iRung, int bGreater) {
    ServiceActiveRung::input in(iRung,bGreater);
    mdl->RunService(PST_ACTIVERUNG,sizeof(in),&in);

    if ( iRung==0 && bGreater )
	nActive = N;
    else {
	int i;


	nActive = 0;
	for ( i=iRung; i<= (bGreater?param.iMaxRung:iRung); i++ )
	    nActive += nRung[i];
	}
    }

void MSR::ActiveOrder() {
    pstActiveOrder(pst,NULL,0,&(nActive),sizeof(nActive));
    }

int MSR::CountRungs(uint64_t *nRungs) {
    ServiceCountRungs::output out;
    int i, iMaxRung=0;
    mdl->RunService(PST_COUNTRUNGS,&out);
    for(i=0; i<=MAX_RUNG; ++i) {
	nRung[i] = out.nRungs[i];
	if (nRung[i]) iMaxRung = i;
	if (nRungs) nRungs[i] = nRung[i];
	}
    iCurrMaxRung = iMaxRung;

    const uint64_t nDT = d2u64(N*param.dFracDualTree);
    const uint64_t nDD = d2u64(N*param.dFracNoDomainDecomp);
    uint64_t nActive = 0;
    iRungDD = 0;
    iRungDT = 0;
    for (i=iCurrMaxRung;i>=0;--i) {
	nActive += nRung[i];
	if (nActive > nDT && !iRungDT) iRungDT = i;
	if (nActive > nDD && !iRungDD) iRungDD = i;
	}

    return iMaxRung;
    }

void MSR::AccelStep(uint8_t uRungLo,uint8_t uRungHi,double dTime,double dDelta) {
    struct inAccelStep in;
    double a;

    in.dEta = Eta();
    a = csmTime2Exp(csm,dTime);
    if (csm->val.bComove) {
	in.dVelFac = 1.0/(a*a);
	}
    else {
	in.dVelFac = 1.0;
	}
    in.dAccFac = 1.0/(a*a*a);
    in.bDoGravity = DoGravity();
    in.bEpsAcc = param.bEpsAccStep;
    in.dDelta = dDelta;
    in.iMaxRung = param.iMaxRung;
    in.uRungLo = uRungLo;
    in.uRungHi = uRungHi;
    pstAccelStep(pst,&in,sizeof(in),NULL,0);
    }

/* Requires full forces and full udot (i.e. sph and cooling both done) */
void MSR::SphStep(uint8_t uRungLo,uint8_t uRungHi,double dTime,double dDelta) {
    struct inSphStep in;
    double a;

    a = csmTime2Exp(csm,dTime);
    in.dAccFac = 1.0/(a*a*a);
    in.dEta = param.dEta;
    in.dEtaUDot = param.dEtaUDot;
    in.uRungLo = uRungLo;
    in.uRungHi = uRungHi;
    in.dDelta = dDelta;
    in.iMaxRung = param.iMaxRung;
    pstSphStep(pst,&in,sizeof(in),NULL,0);
    }

void MSR::DensityStep(uint8_t uRungLo,uint8_t uRungHi,double dTime,double dDelta) {
    struct inDensityStep in;
    double expand;
    int bSymmetric;

    msrprintf("Calculating Rung Densities...\n");
    bSymmetric = 0;
    Smooth(dTime,dDelta,SMX_DENSITY,bSymmetric,param.nSmooth);
    in.dDelta = dDelta;
    in.iMaxRung = param.iMaxRung;
    in.dEta = Eta();
    expand = csmTime2Exp(csm,dTime);
    in.dRhoFac = 1.0/(expand*expand*expand);
    in.uRungLo = uRungLo;
    in.uRungHi = uRungHi;
    pstDensityStep(pst,&in,sizeof(in),NULL,0);
    }

/*
** Returns the Very Active rung based on the number of very active particles desired,
** or the fixed rung that was specified in the parameters.
*/
void MSR::UpdateRung(uint8_t uRung) {
    struct inUpdateRung in;
    struct outUpdateRung out;
    int iTempRung,iOutMaxRung;

    /* If we are called, it is a mistake -- this happens in analysis mode */
    if (param.bMemUnordered) return;

    in.uRungLo = uRung;
    in.uRungHi = MaxRung();
    in.uMinRung = uRung;
    in.uMaxRung = MaxRung();

    pstUpdateRung(pst, &in, sizeof(in), &out, sizeof(out));

    iTempRung =MaxRung()-1;
    while (out.nRungCount[iTempRung] == 0 && iTempRung > 0) --iTempRung;
    iOutMaxRung = iTempRung;

    while (out.nRungCount[iOutMaxRung] <= param.nTruncateRung && iOutMaxRung > uRung) {
	msrprintf("n_CurrMaxRung = %" PRIu64 "  (iCurrMaxRung = %d):  Promoting particles to iCurrMaxrung = %d\n",
		  out.nRungCount[iOutMaxRung],iOutMaxRung,iOutMaxRung-1);

	in.uMaxRung = iOutMaxRung; /* Note this is the forbidden rung so no -1 here */
	pstUpdateRung(pst, &in, sizeof(in), &out, sizeof(out));

	iTempRung =MaxRung()-1;
	while (out.nRungCount[iTempRung] == 0 && iTempRung > 0) --iTempRung;
	iOutMaxRung = iTempRung;
	}
    /*
    ** Now copy the rung distribution to the msr structure!
    */
    for (iTempRung=0;iTempRung < MaxRung();++iTempRung) nRung[iTempRung] = out.nRungCount[iTempRung];

    iCurrMaxRung = iOutMaxRung;

    if (param.bVRungStat) {
	printf("Rung distribution:\n");
	printf("\n");
	for (iTempRung=0;iTempRung <= iCurrMaxRung;++iTempRung) {
	    if (out.nRungCount[iTempRung] == 0) continue;
	    printf("   rung:%d %" PRIu64 "\n",iTempRung,out.nRungCount[iTempRung]);
	    }
	printf("\n");
	}
    }


/*
** Open the healpix output file, and also the particles files if requested.
*/
void MSR::LightConeOpen(int iStep) {
    if (param.bLightCone) {
	struct inLightConeOpen lc;
	if (param.bLightConeParticles ) {
	    auto filename = BuildName(iStep);
	    strcpy(lc.achOutFile,filename.c_str());
	    }
	else lc.achOutFile[0] = 0;
	lc.nSideHealpix = param.nSideHealpix;
	pstLightConeOpen(pst,&lc,sizeof(lc),NULL,0);
	}
    }


/*
** Close the files for this step.
*/
void MSR::LightConeClose(int iStep) {
    if (param.bLightCone) {
	struct inLightConeClose lc;
	auto filename = BuildName(iStep);
	strcpy(lc.achOutFile,filename.c_str());
	pstLightConeClose(pst,&lc,sizeof(lc),NULL,0);
	}
    }

/*
** Correct velocities from a^2 x_dot to a x_dot (physical peculiar velocities) using the 
** position dependent scale factor within the light cone. This could be expensive.
*/
void MSR::LightConeVel() {
    double sec,dsec;
    struct inLightConeVel in;

    sec = Time();
    in.dBoxSize = param.dBoxSize;
    pstLightConeVel(pst,&in,sizeof(in),NULL,0);
    dsec = Time() - sec;
    printf("Converted lightcone velocities to physical, Wallclock: %f secs.\n", dsec);
    }


/* True if we should omit the opening kick */
int MSR::CheckForOutput(int iStep,int nSteps,double dTime,int *pbDoCheckpoint,int *pbDoOutput) {
    int iStop, iCheck;
    long lSec = time(0) - lPrior;

    /*
    ** Check for user interrupt.
    */
    iStop = CheckForStop(STOPFILE);
    iCheck = CheckForStop(CHECKFILE);

    /*
    ** Check to see if the runtime has been exceeded.
    */
    if (!iStop && param.iWallRunTime > 0) {
	if (param.iWallRunTime*60 - (time(0)-lStart) < ((int) (lSec*1.5)) ) {
	    printf("RunTime limit exceeded.  Writing checkpoint and exiting.\n");
	    printf("    iWallRunTime(sec): %d   Time running: %ld   Last step: %ld\n",
		param.iWallRunTime*60,time(0)-lStart,lSec);
	    iStop = 1;
	    }
	}

    /* Check to see if there should be an output */
    if (!iStop && timeGlobalSignalTime>0) { /* USR1 received */
	if ( (time(0)+(lSec*1.5)) > timeGlobalSignalTime+param.iSignalSeconds) {
	    printf("RunTime limit exceeded.  Writing checkpoint and exiting.\n");
	    printf("    iSignalSeconds: %d   Time running: %ld   Last step: %ld\n",
		param.iSignalSeconds,time(0)-lStart,lSec);
	    iStop = 1;
	    }
	}

    /*
    ** Output if 1) we've hit an output time
    **           2) We are stopping
    **           3) we're at an output interval
    */
    if (iCheck || (CheckInterval()>0 &&
		    (bGlobalOutput
		    || iStop
			|| (iStep%CheckInterval() == 0) )) ) {
	bGlobalOutput = 0;
	*pbDoCheckpoint = 1 | (iStop<<1);
	}

    if (OutTime(dTime) 
	|| (OutInterval() > 0 &&
		(bGlobalOutput
		|| iStop
		|| iStep == nSteps
		|| (iStep%OutInterval() == 0))) ) {
	bGlobalOutput = 0;
	*pbDoOutput = 1  | (iStop<<1);
	}

    return (iStep==param.nSteps10) || *pbDoOutput || *pbDoCheckpoint;
    }


int MSR::NewTopStepKDK(
    double &dTime,	/* MODIFIED: Current simulation time */
    double dDelta,
    double dTheta,
    int nSteps,
    int bDualTree,      /* Should be zero at rung 0! */
    uint8_t uRung,	/* Rung level */
    double *pdStep,	/* Current step */
    uint8_t *puRungMax,
    int *pbDoCheckpoint,int *pbDoOutput,int *pbNeedKickOpen) {
    double dDeltaRung,dTimeFixed;
    uint32_t uRoot2=0;
    int bKickOpen=1;
    /*
    ** The iStep variable serves only to give a number to the lightcone and group output files.
    ** We define this to be the output number of the final radius of the lightcone surface.
    */
    int iStep = (int)(*pdStep) + 1;

    if (uRung == iRungDT+1) {
	if ( param.bDualTree && uRung < *puRungMax) {
	    /* HACK: FIXME: Don't use the dual tree before z=2; the overlap region is too large */
	    /* better would be to construct the tree matching remote processor shape as well as local */
	    double a = csmTime2Exp(csm,dTime);
	    if (a < (1.0/3.0)) bDualTree = 0;
	    else {
		bDualTree = 1;
		ServiceDumpTrees::input dump(iRungDT);
		mdl->RunService(PST_DUMPTREES,sizeof(dump),&dump);
		msrprintf("Half Drift, uRung: %d\n",iRungDT);
		dDeltaRung = dDelta/(1 << iRungDT); // Main tree step
		Drift(dTime,0.5 * dDeltaRung,FIXROOT);
		dTimeFixed = dTime + 0.5 * dDeltaRung;
		BuildTreeFixed(param.bEwald,iRungDT);
		}
	    }
	else bDualTree = 0;
	}
    if (uRung < *puRungMax) {
	bDualTree = NewTopStepKDK(dTime,dDelta,dTheta,nSteps,bDualTree,uRung+1,pdStep,puRungMax,pbDoCheckpoint,pbDoOutput,pbNeedKickOpen);
	}

    /* Drift the "ROOT" (active) tree or all particle */
    dDeltaRung = dDelta/(1 << *puRungMax);
    if (bDualTree) {
	msrprintf("Drift very actives, uRung: %d\n",*puRungMax);
	Drift(dTime,dDeltaRung,ROOT);
	}
    else {
	msrprintf("Drift, uRung: %d\n",*puRungMax);
	Drift(dTime,dDeltaRung,-1);
	}
    dTime += dDeltaRung;
    *pdStep += 1.0/(1 << *puRungMax);

    ActiveRung(uRung,1);
    UpdateSoft(dTime);
    if (bDualTree && uRung > iRungDT) {
	uRoot2 = FIXROOT;
	BuildTreeActive(param.bEwald,iRungDT);
	}
    else {
	DomainDecomp(uRung);
	uRoot2 = 0;
	BuildTree(param.bEwald);
	}

    if (!uRung) {
	bKickOpen = !CheckForOutput(iStep,nSteps,dTime,pbDoCheckpoint,pbDoOutput);	
	}
    else bKickOpen = 1;
    *pbNeedKickOpen = !bKickOpen;

    /*
    ** At this point the particles are in sync. As soon as we call the next gravity it will
    ** advance the particles as part of the opening kick. For this reason we do the requested
    ** analysis at this poing while everything is properly synchronized. This includes
    ** writing the healpix and lightcone particles, as well as measuring P(k) for example.
    */
    if (uRung==0) {
	runAnalysis(iStep,dTime); // Run any registered Python analysis tasks

	if (param.iPkInterval && iStep%param.iPkInterval == 0) OutputPk(iStep,dTime);

	/*
	** We need to write all light cone files (healpix and LCP) at this point before the last
	** gravity is called since it will advance the particles in the light cone as part of the
	** opening kick! We also need to open
	*/
	LightConeClose(iStep);
	if (bKickOpen) LightConeOpen(iStep+1);

	/* Compute the grids of linear species at main timesteps, before gravity is called */
	if (csm->val.classData.bClass && strlen(param.achLinearSpecies) && param.nGridLin){
	    GridCreateFFT(param.nGridLin);
            SetLinGrid(dTime,dDelta,param.nGridLin,1,bKickOpen);
            if (param.bDoLinPkOutput)
        	OutputLinPk( *pdStep, dTime);
	    LinearKick(dTime,dDelta,1,bKickOpen);
	    GridDeleteFFT();
	    }

	if (param.bFindGroups) NewFof(dTime);
	}

    // We need to make sure we descend all the way to the bucket with the
    // active tree, or we can get HUGE group cells, and hence too much P-P/P-C
    int nGroup = (bDualTree && uRung > iRungDT) ? 1 : param.nGroup;
    *puRungMax = Gravity(uRung,MaxRung(),ROOT,uRoot2,dTime,dDelta,*pdStep,dTheta,
    	1,bKickOpen,param.bEwald,param.bGravStep,param.nPartRhoLoc,param.iTimeStepCrit,nGroup);

    if (!uRung && param.bFindGroups) {
	GroupStats();
	HopWrite(BuildName(iStep,".fofstats").c_str());
	}

    if (uRung && uRung < *puRungMax) bDualTree = NewTopStepKDK(dTime,dDelta,dTheta,nSteps,bDualTree,uRung+1,pdStep,puRungMax,pbDoCheckpoint,pbDoOutput,pbNeedKickOpen);
    if (bDualTree && uRung==iRungDT+1) {
	msrprintf("Half Drift, uRung: %d\n",iRungDT);
	dDeltaRung = dDelta/(1 << iRungDT);
	Drift(dTimeFixed,0.5 * dDeltaRung,FIXROOT);
	}
    return bDualTree;
    }

void MSR::TopStepKDK(
		   double dStep,	/* Current step */
		   double dTime,	/* Current time */
		   double dDeltaRung,	/* Time step */
		   double dTheta,
		   int iRung,		/* Rung level */
		   int iKickRung,	/* Gravity on all rungs from iRung to iKickRung */
		   int iAdjust) {	/* Do an adjust? */
    double dDeltaStep = dDeltaRung * (1 << iRung);

    if (iAdjust && (iRung < MaxRung()-1)) {
	msrprintf("%*cAdjust, iRung: %d\n",2*iRung+2,' ',iRung);
	/* JW: Note -- can't trash uRungNew here! Force calcs set values for it! */
	ActiveRung( iRung, 1);
	if (param.bAccelStep) AccelStep(iRung,MAX_RUNG,dTime,dDeltaStep);
	if (DoGas()) SphStep(iRung,MAX_RUNG,dTime,dDeltaStep);
	if (param.bDensityStep) {
	    DomainDecomp(iRung);
	    ActiveRung(iRung,1);
	    BuildTree(0);
	    DensityStep(iRung,MAX_RUNG,dTime,dDeltaStep);
	    }
	UpdateRung(iRung);
        }

    msrprintf("%*cmsrKickOpen  at iRung: %d 0.5*dDelta: %g\n",
	      2*iRung+2,' ',iRung,0.5*dDeltaRung);
    KickKDKOpen(dTime,0.5*dDeltaRung,iRung,iRung);
    if (CurrMaxRung() > iRung) {
	/*
	** Recurse.
	*/
	TopStepKDK(dStep,dTime,0.5*dDeltaRung,dTheta,iRung+1,iRung+1,0);
	dTime += 0.5*dDeltaRung;
	dStep += 1.0/(2 << iRung);

	ActiveRung(iRung,0); /* is this call even needed? */

	TopStepKDK(dStep,dTime,0.5*dDeltaRung,dTheta,iRung+1,iKickRung,1);
	}
    else if (CurrMaxRung() == iRung) {
	/* This Drifts everybody */
	msrprintf("%*cDrift, iRung: %d\n",2*iRung+2,' ',iRung);
	Drift(dTime,dDeltaRung,ROOT);
	dTime += dDeltaRung;
	dStep += 1.0/(1 << iRung);

	ActiveRung(iKickRung,1);
	DomainDecomp(iKickRung);

	/* JW: Good place to zero uNewRung */
	ZeroNewRung(iKickRung,MAX_RUNG,iKickRung); /* brute force */
	if (DoGravity() || DoGas()) {
	    ActiveRung(iKickRung,1);
	    if (DoGravity()) UpdateSoft(dTime);
	    msrprintf("%*cForces, iRung: %d to %d\n",2*iRung+2,' ',iKickRung,iRung);
	    BuildTree(param.bEwald);
	    }
	if (DoGravity()) {
	    Gravity(iKickRung,MAX_RUNG,ROOT,0,dTime,dDeltaStep,dStep,dTheta,0,0,
	    	param.bEwald,param.bGravStep,param.nPartRhoLoc,param.iTimeStepCrit,
	    	param.nGroup);
	    }
	
	if (DoGas()) {
	    Sph(dTime,dDeltaStep,dStep);  /* dTime = Time at end of kick */
	    Cooling(dTime,dStep,0,
		       (iKickRung<=param.iRungCoolTableUpdate ? 1:0),0);
	    }
	/*
	 * move time back to 1/2 step so that KickClose can integrate
	 * from 1/2 through the timestep to the end.
	 */
	dTime -= 0.5*dDeltaRung;
	}
    else { abort(); }

    msrprintf("%*cKickClose, iRung: %d, 0.5*dDelta: %g\n",
	      2*iRung+2,' ',iRung, 0.5*dDeltaRung);
    KickKDKClose(dTime,0.5*dDeltaRung,iRung,iRung); /* uses dTime-0.5*dDelta */

    dTime += 0.5*dDeltaRung; /* Important to have correct time at step end for SF! */
    /* JW: Creating/Deleting/Merging is best done outside (before or after) KDK cycle 
       -- Tree should still be valid from last force eval (only Drifts + Deletes invalidate it) */
    if (iKickRung == iRung) { /* Do all co-incident kicked p's in one go */
	StarForm(dTime, dDeltaStep, iKickRung); /* re-eval timesteps as needed for next step 
					    -- apply to all neighbours */
	}
    }

void MSR::StarForm(double dTime, double dDelta, int iRung) {
    struct inStarForm in;
    struct outStarForm out;
    double a,d1,d2;
    double sec,sec1,dsec;

    if(!param.bStarForm) return;
    sec = MSR::Time();

    a = csmTime2Exp(csm,dTime);
    in.dDelta = dDelta;
    /* Convert input parameters to code units */
    in.dRateCoeff = param.SFdEfficiency*sqrt(32/(3*M_PI)/pow(a,3)); /* G=1 */
    in.dTMax = param.SFdTMax;
    d1 = param.SFdComovingDenMin;
    d2 = param.SFdPhysDenMin/param.dGmPerCcUnit*pow(a,3);
    in.dDenMin = (d1>d2 ? d1 : d2);
    
    in.dTime = dTime;
    in.dInitStarMass = param.SFdInitStarMass;
    in.dESNPerStarMass = param.SFdESNPerStarMass/param.dErgPerGmUnit;
#define SECONDSPERYEAR   31557600.
    in.dtCoolingShutoff = param.SFdtCoolingShutoff*SECONDSPERYEAR/param.dSecUnit;
    in.dtFeedbackDelay = param.SFdtFeedbackDelay*1.0000013254678*SECONDSPERYEAR/param.dSecUnit;
    in.dMassLossPerStarMass = param.SFdMassLossPerStarMass;
    in.dZMassPerStarMass = param.SFdZMassPerStarMass;
    in.dInitStarMass = param.SFdInitStarMass;
    in.dMinGasMass = param.SFdMinGasMass;
    in.bdivv = param.SFbdivv;
    in.dTuFac = dTuFac;
    in.bGasCooling = param.bGasCooling;

    if (bVDetails) printf("Star Form ... ");
    
    ActiveRung(iRung,1); /* important to limit to active gas only */
    pstStarForm(pst, &in, sizeof(in), &out, sizeof(out));
    if (bVDetails)
	printf("%d Stars formed with mass %g, %d gas deleted\n",
	       out.nFormed, out.dMassFormed, out.nDeleted);
    
    if (out.nDeleted) {
	//msrSelSrcGas(msr); /* Not really sure what the setting here needs to be */
	//msrSelDstDeleted(msr); /* Select only deleted particles */
	ActiveRung(0,1); /* costs nothing -- may be redundant */
/*	BuildTree(param.bEwald);*/
	Smooth(dTime, dDelta, SMX_DIST_DELETED_GAS, 1,param.nSmooth); /* use full smooth to account for deleted */
	//msrSelSrcAll(msr);
	//msrSelDstAll(msr);
	}

    /* Strictly speaking adding/deleting particles invalidates the tree 
       In practice we can find deleted particles (and ignore them) and we aren't looking for 
       any new star particles so we should be able to function with an old tree for FB 
       Could do FB before the AddDel call if careful to exclude deleted particles */
    if (out.nDeleted || out.nFormed) AddDelParticles();
    
    sec1 = MSR::Time();
    dsec = sec1 - sec;
    printf("Star Formation Calculated, Wallclock: %f secs\n\n",dsec);

    if (param.bFeedback) {
	ActiveRung(iRung,1); /* costs nothing -- important to limit to active stars only */
 	//msrSelSrcGas(msr); /* Not really sure what the setting here needs to be */
	//msrSelDstStar(msr,1,dTime); /* Select only stars that have FB to do */ 
/*	BuildTree(param.bEwald);*/
	Smooth(dTime, dDelta, SMX_DIST_SN_ENERGY, 1, param.nSmooth); /* full smooth for stars */
	//msrSelSrcAll(msr);
	//msrSelDstAll(msr);

	dsec = MSR::Time() - sec1;
	printf("Feedback Calculated, Wallclock: %f secs\n\n",dsec);
	}
    }

void MSR::GetNParts() { /* JW: Not pretty -- may be better way via fio */
    struct outGetNParts outget;

    pstGetNParts(pst,NULL,0,&outget,sizeof(outget));
    assert(outget.nGas == nGas);
    assert(outget.nDark == nDark);
    assert(outget.nStar == nStar);
    nMaxOrder = outget.nMaxOrder;
#if 0
    if (outget.iMaxOrderGas > nMaxOrder) {
	nMaxOrder = outget.iMaxOrderGas;
	fprintf(stderr,"WARNING: Largest iOrder of gas > Largest iOrder of star\n");
	}
    if (outget.iMaxOrderDark > nMaxOrder) {
	nMaxOrder = outget.iMaxOrderDark;
	fprintf(stderr,"WARNING: Largest iOrder of dark > Largest iOrder of star\n");
	}
#endif
    }

void MSR::AddDelParticles() {
    struct inSetNParts in;
    int i;

    msrprintf("Changing Particle number\n");


    std::unique_ptr<struct outColNParts[]> pColNParts {new struct outColNParts[nThreads]};
    pstColNParts(pst, NULL, 0, pColNParts.get(), nThreads*sizeof(pColNParts[0]));
    /*
     * Assign starting numbers for new particles in each processor.
     */
    std::unique_ptr<uint64_t[]> pNewOrder {new uint64_t[nThreads]};
    for (i=0;i<nThreads;i++) {
	/*
	 * Detect any changes in particle number, and force a tree
	 * build.
	 */
	if (pColNParts[i].nNew != 0 || pColNParts[i].nDeltaGas != 0 ||
		pColNParts[i].nDeltaDark != 0 || pColNParts[i].nDeltaStar != 0) {
	    /*printf("Particle assignments have changed!\n");
	      printf("need to rebuild tree, code in msrAddDelParticles()\n");
	      printf("needs to be updated. Bailing out for now...\n");
	      exit(-1); */
	    pNewOrder[i] = nMaxOrder+1; /* JW: +1 was missing for some reason */
	    nMaxOrder += pColNParts[i].nNew;
	    nGas += pColNParts[i].nDeltaGas;
	    nDark += pColNParts[i].nDeltaDark;
	    nStar += pColNParts[i].nDeltaStar;
	    }
	}
    N = nGas + nDark + nStar;

    /*nMaxOrderDark = nMaxOrder;*/

    pstNewOrder(pst,pNewOrder.get(),(int)sizeof(pNewOrder[0])*nThreads,NULL,0);

    msrprintf("New numbers of particles: %" PRIu64 " gas %" PRIu64 " dark %" PRIu64 " star\n",
	      nGas, nDark, nStar);

    in.nGas = nGas;
    in.nDark = nDark;
    in.nStar = nStar;
    pstSetNParts(pst,&in,sizeof(in),NULL,0);
    }

/* Gas routines */

void MSR::InitSph(double dTime,double dDelta) {
    /* Init gas, internal energy -- correct estimate from dTuFac */
    ActiveRung(0,1);
    /* Very important NOT to do this if starting from a checkpoint */
    if (param.bGasCooling && !param.bRestart) {
	struct inCorrectEnergy in;
	double a;

 	//msrSelSrcGas(msr); /* Not really sure what the setting here needs to be */
	//msrSelDstGas(msr);  
	Smooth(dTime,dDelta,SMX_DENDVDX,0,param.nSmooth);  
	//msrSelSrcAll(msr);
	//msrSelDstAll(msr);

	in.dTuFac = dTuFac;
	a = csmTime2Exp(csm,dTime);
	in.z = 1/a - 1;
	in.dTime = dTime;
	if (param.bInitTFromCooling) {
	    fprintf(stderr,"INFO: Resetting thermal energies to special value in cooling routines\n");
	    in.iDirection = CORRECTENERGY_SPECIAL;
	    }
	else {
	    fprintf(stderr,"INFO: Correcting (dTuFac) thermal energies using cooling routines\n");
	    in.iDirection = CORRECTENERGY_IN;
	    }
	   
	pstCorrectEnergy(pst, &in, sizeof(in), NULL, 0);
	}
    
    /* Init forces, ... */
    ActiveRung(0,1);
    Sph(dTime,dDelta,0);
    SphStep(0,MAX_RUNG,dTime,dDelta); /* Requires SPH */
    Cooling(dTime,0,0,1,1); /* Interate cooling for consistent dt */
}

void MSR::Sph(double dTime, double dDelta, double dStep) {
    double sec,dsec;

    if (param.bVStep) printf("Calculating Sph, Step:%f\n",dStep);
    sec = MSR::Time();

/* JW: Is the tree aware of this -- does it need to be? 
       Will smooth behave correctly? */

    //msrSelSrcGas(msr); /* Not really sure what the setting here needs to be */
    //msrSelDstGas(msr);  
    Smooth(dTime,dDelta,SMX_DENDVDX,0,param.nSmooth);  
    Smooth(dTime,dDelta,SMX_SPHFORCES,1,param.nSmooth); /* Should be a resmooth */
    //msrSelSrcAll(msr);
    //msrSelDstAll(msr);

    dsec = MSR::Time() - sec;
    if (param.bVStep) {
	printf("SPH Calculated, Wallclock: %f secs\n",  dsec);
	}
    }

void MSR::CoolSetup(double dTime) {
    }

void MSR::Cooling(double dTime,double dStep,int bUpdateState, int bUpdateTable, int bIterateDt) {
    }

/* END Gas routines */

void MSR::HopWrite(const char *fname) {
    LCL *plcl;
    PST pst0;
    double sec,dsec;

    pst0 = pst;
    while (pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;


    if (param.bVStep)
	printf("Writing group statistics to %s\n", fname );
    sec = MSR::Time();

    /* This is the new parallel binary format */
    struct inOutput out;
    out.eOutputType = OUT_TINY_GROUP;
    out.iPartner = -1;
    out.nPartner = -1;
    out.iProcessor = 0;
    out.nProcessor = param.bParaWrite==0?1:(param.nParaWrite<=1 ? nThreads:param.nParaWrite);
    strcpy(out.achOutFile,fname);
    pstOutput(pst,&out,sizeof(out),NULL,0);
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Written statistics, Wallclock: %f secs\n",dsec);

    }

void MSR::Hop(double dTime, double dDelta) {
    struct inSmooth in;
    struct inHopLink h;
    struct outHopJoin j;
    struct inHopFinishUp inFinish;
    struct inGroupStats inGroupStats;
    int i;
    uint64_t nGroups;
    double sec,dsec,ssec;

    ssec = MSR::Time();

    h.nSmooth    = in.nSmooth = 80;
    h.bPeriodic  = in.bPeriodic = param.bPeriodic;
    h.bSymmetric = in.bSymmetric = 0;
    h.dHopTau    = param.dHopTau<0 ? param.dHopTau : param.dHopTau;
    h.smf.a      = in.smf.a = dTime;
    h.smf.dTau2  = in.smf.dTau2 = 0.0;
    h.smf.nMinMembers = in.smf.nMinMembers = param.nMinMembers;
    SmoothSetSMF(&(in.smf), dTime, dDelta);
    SmoothSetSMF(&(h.smf), dTime, dDelta);

    if (param.bVStep) {
	if (h.dHopTau<0.0)
	    printf("Running Grasshopper with adaptive linking length (%g times softening)\n", -h.dHopTau );
	else
	    printf("Running Grasshopper with fixed linking length %g\n", h.dHopTau );
	}

    in.iSmoothType = SMX_DENSITY_M3;
    sec = MSR::Time();
    pstSmooth(pst,&in,sizeof(in),NULL,0);
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Density calculation complete in %f secs, finding chains...\n",dsec);

    h.iSmoothType = SMX_GRADIENT_M3;
    sec = MSR::Time();
    nGroups = 0;
    pstHopLink(pst,&h,sizeof(h),&nGroups,sizeof(nGroups));
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Chain search complete in %f secs, building minimal tree...\n",dsec);

    /* Build a new tree with only marked particles */
    sec = MSR::Time();
    BuildTreeMarked();
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Tree build complete in %f secs, merging %" PRIu64 " chains...\n",dsec,nGroups);

    h.iSmoothType = SMX_HOP_LINK;
    sec = MSR::Time();
    i = 0;
    do {
	++i;
	assert(i<100);
	pstHopJoin(pst,&h,sizeof(h),&j,sizeof(j));
	if (param.bVStep)
	    printf("... %d iteration%s, %" PRIu64 " chains remain\n",i,i==1?"":"s",j.nGroups);
	} while( !j.bDone );
    nGroups = j.nGroups;
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Chain merge complete in %f secs, %" PRIu64 " groups\n",dsec,nGroups);
    inFinish.nMinGroupSize = param.nMinMembers;
    inFinish.bPeriodic = param.bPeriodic;
    inFinish.fPeriod[0] = param.dxPeriod;
    inFinish.fPeriod[1] = param.dyPeriod;
    inFinish.fPeriod[2] = param.dzPeriod;
    pstHopFinishUp(pst,&inFinish,sizeof(inFinish),&nGroups,sizeof(nGroups));
    if (param.bVStep)
	printf("Removed groups with fewer than %d particles, %" PRIu64 " remain\n",
	    inFinish.nMinGroupSize, nGroups);
#if 0
    if (param.bVStep)
	printf("Unbinding\n");

    struct inHopUnbind inUnbind;
    inUnbind.dTime = dTime;
    inUnbind.bPeriodic = param.bPeriodic;
    inUnbind.fPeriod[0] = param.dxPeriod;
    inUnbind.fPeriod[1] = param.dyPeriod;
    inUnbind.fPeriod[2] = param.dzPeriod;
    inUnbind.nMinGroupSize = param.nMinMembers;
    inUnbind.iIteration = 0;
    struct inHopGravity inGravity;
    inGravity.dTime = dTime;
    inGravity.bPeriodic = param.bPeriodic;
    inGravity.nGroup = param.nGroup;
    inGravity.dEwCut = param.dEwCut;
    inGravity.dEwhCut = param.dEwhCut;
    inGravity.uRungLo = 0;
    inGravity.uRungHi = MAX_RUNG;
    inGravity.dTheta = dThetaMin;

    inUnbind.iIteration=0;
    do {
	sec = MSR::Time();
        struct inHopTreeBuild inTreeBuild;
	inTreeBuild.nBucket = param.nBucket;
	inTreeBuild.nGroup = param.nGroup;
	pstHopTreeBuild(pst,&inTreeBuild,sizeof(inTreeBuild),NULL,0);
	dsec = MSR::Time() - sec;
	if (param.bVStep)
	    printf("... group trees built, Wallclock: %f secs\n",dsec);

	sec = MSR::Time();
	pstHopGravity(pst,&inGravity,sizeof(inGravity),NULL,0);
	dsec = MSR::Time() - sec;
	if (param.bVStep)
	    printf("... gravity complete, Wallclock: %f secs\n",dsec);
	
	sec = MSR::Time();
        struct outHopUnbind outUnbind;
	pstHopUnbind(pst,&inUnbind,sizeof(inUnbind),&outUnbind,sizeof(outUnbind));
	nGroups = outUnbind.nGroups;
	dsec = MSR::Time() - sec;
	if (param.bVStep)
	    printf("Unbinding completed in %f secs, %" PRIu64 " particles evaporated, %" PRIu64 " groups remain\n",
		dsec,outUnbind.nEvaporated, nGroups);
	} while(++inUnbind.iIteration < 100 && outUnbind.nEvaporated);
#endif
    /*
    ** This should be done as a separate msr function.
    */
    inGroupStats.bPeriodic = param.bPeriodic;
    inGroupStats.dPeriod[0] = param.dxPeriod;
    inGroupStats.dPeriod[1] = param.dyPeriod;
    inGroupStats.dPeriod[2] = param.dzPeriod;
    inGroupStats.rEnvironment[0] = param.dEnvironment0;
    inGroupStats.rEnvironment[1] = param.dEnvironment1;
    if ( prmSpecified(prm,"dBoxSize") && param.dBoxSize > 0.0 ) {
	inGroupStats.rEnvironment[0] /= param.dBoxSize;
	inGroupStats.rEnvironment[1] /= param.dBoxSize;
	}
    pstGroupStats(pst,&inGroupStats,sizeof(inGroupStats),NULL,0);

    dsec = MSR::Time() - ssec;
    if (param.bVStep)
	printf("Grasshopper complete, Wallclock: %f secs\n\n",dsec);
    }


void MSR::NewFof(double dTime) {
    struct inNewFof in;
    struct outFofPhases out;
    struct inFofFinishUp inFinish;
    int i;
    uint64_t nGroups;
    double sec,dsec,ssec;

    ssec = MSR::Time();

    in.dTau2 = param.dTau*param.dTau;
    in.nMinMembers = param.nMinMembers;
    in.bPeriodic = param.bPeriodic;
    in.nReplicas = param.nReplicas;
    in.nBucket = param.nBucket;

    if (param.bVStep) {
	printf("Running FoF with fixed linking length %g\n", param.dTau );
	}

    sec = MSR::Time();
    pstNewFof(pst,&in,sizeof(in),NULL,0);
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Initial FoF calculation complete in %f secs\n",dsec);

    sec = MSR::Time();
    i = 0;
    do {
	++i;
	assert(i<100);
	pstFofPhases(pst,NULL,0,&out,sizeof(out));
	if (param.bVStep)
	    printf("... %d iteration%s\n",i,i==1?"":"s");
	} while( out.bMadeProgress );
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Global merge complete in %f secs\n",dsec);

    inFinish.nMinGroupSize = param.nMinMembers;
    pstFofFinishUp(pst,&inFinish,sizeof(inFinish),&nGroups,sizeof(nGroups));
    if (param.bVStep)
	printf("Removed groups with fewer than %d particles, %" PRIu64 " remain\n",
	    inFinish.nMinGroupSize, nGroups);
//    pstGroupRelocate(pst,NULL,0,NULL,0);
    dsec = MSR::Time() - ssec;
    if (param.bVStep)
	printf("FoF complete, Wallclock: %f secs\n",dsec);
    }


void MSR::GroupStats() {
    struct inGroupStats inGroupStats;
    double sec,dsec;

    if (param.bVStep)
	printf("Generating Group statistics\n");
    sec = MSR::Time();
    inGroupStats.bPeriodic = param.bPeriodic;
    inGroupStats.dPeriod[0] = param.dxPeriod;
    inGroupStats.dPeriod[1] = param.dyPeriod;
    inGroupStats.dPeriod[2] = param.dzPeriod;
    inGroupStats.rEnvironment[0] = param.dEnvironment0;
    inGroupStats.rEnvironment[1] = param.dEnvironment1;
    if ( prmSpecified(prm,"dBoxSize") && param.dBoxSize > 0.0 ) {
	inGroupStats.rEnvironment[0] /= param.dBoxSize;
	inGroupStats.rEnvironment[1] /= param.dBoxSize;
	}
    pstGroupStats(pst,&inGroupStats,sizeof(inGroupStats),NULL,0);
    dsec = MSR::Time() - sec;
    if (param.bVStep)
	printf("Group statistics complete, Wallclock: %f secs\n\n",dsec);
    }

void MSR::InitRelaxation() {
    pstInitRelaxation(pst,NULL,0,NULL,0);
    }

void MSR::Relaxation(double dTime,double deltaT,int iSmoothType,int bSymmetric) {
    struct inSmooth in;
    in.nSmooth = param.nSmooth;
    in.bPeriodic = param.bPeriodic;
    in.bSymmetric = bSymmetric;
    in.iSmoothType = iSmoothType;
    if (Comove()) {
	in.smf.H = csmTime2Hub(csm,dTime);
	in.smf.a = csmTime2Exp(csm,dTime);
	}
    else {
	in.smf.H = 0.0;
	in.smf.a = 1.0;
	}
    in.smf.dDeltaT = deltaT;
    if (param.bVStep) {
	double sec,dsec;
	printf("Smoothing for relaxation...dDeltaT = %f \n",deltaT);
	sec = MSR::Time();
	pstSmooth(pst,&in,sizeof(in),NULL,0);
	dsec = MSR::Time() - sec;
	printf("Relaxation Calculated, Wallclock: %f secs\n\n",dsec);
	}
    else {
	pstSmooth(pst,&in,sizeof(in),NULL,0);
	}
    }

#ifdef MDL_FFTW
double MSR::GenerateIC() {
    struct inGenerateIC in;
    struct outGenerateIC out;
    struct inGetFFTMaxSizes inFFTSizes;
    struct outGetFFTMaxSizes outFFTSizes;
    uint64_t nSpecies[FIO_SPECIES_LAST];
    double sec,dsec;
    double mean, rms;
    uint64_t nTotal;
    int j;

    // We only support periodic initial conditions
    param.bPeriodic = 1;

    in.dBoxSize = param.dBoxSize;
    in.iSeed = param.iSeed;
    in.bFixed = param.bFixedAmpIC;
    in.fPhase = param.dFixedAmpPhasePI * M_PI;
    in.nGrid = param.nGrid;
    in.b2LPT = param.b2LPT;

    nTotal  = in.nGrid; /* Careful: 32 bit integer cubed => 64 bit integer */
    nTotal *= in.nGrid;
    nTotal *= in.nGrid;
    in.dBoxMass = csm->val.dOmega0 / nTotal;

    for( j=0; j<FIO_SPECIES_LAST; j++) nSpecies[j] = 0;
    nSpecies[FIO_SPECIES_ALL] = nSpecies[FIO_SPECIES_DARK] = nTotal;
    InitializePStore(nSpecies,getMemoryModel());
    InitCosmology();

    assert(param.dRedFrom >= 0.0 );
    in.dExpansion = 1.0 / (1.0 + param.dRedFrom);

    N     = nSpecies[FIO_SPECIES_ALL];
    nGas  = nSpecies[FIO_SPECIES_SPH];
    nDark = nSpecies[FIO_SPECIES_DARK];
    nStar = nSpecies[FIO_SPECIES_STAR];
    nMaxOrder = N;

    if (param.bVStart)
	printf("Generating IC...\nN:%" PRIu64 " nDark:%" PRIu64
	       " nGas:%" PRIu64 " nStar:%" PRIu64 "\n",
	       N, nDark,nGas,nStar);

    /* Read the transfer function */
    in.nTf = 0;
    if (prmSpecified(prm,"achTfFile")) {
	FILE *fp = fopen(param.achTfFile,"r");
	char buffer[256];

	if (param.bVStart)
	    printf("Reading transfer function from %s\n", param.achTfFile);
	if (fp == NULL) {
	    perror(param.achTfFile);
	    Exit(1);
	    }
	while(fgets(buffer,sizeof(buffer),fp)) {
	    assert(in.nTf < MAX_TF);
	    if (sscanf(buffer," %lg %lg\n",&in.k[in.nTf],&in.tf[in.nTf])==2) {
		in.k[in.nTf] = log(in.k[in.nTf]);
		in.tf[in.nTf] = log(in.tf[in.nTf]);
		++in.nTf;
		}
	    }
	fclose(fp);
	if (param.bVStart)
	    printf("Transfer function : %d lines kmin %g kmax %g\n",
		in.nTf, exp(in.k[0]), exp(in.k[in.nTf-1]));

	}

    sec = MSR::Time();

    /* Figure out the minimum number of particles */
    inFFTSizes.nx = inFFTSizes.ny = inFFTSizes.nz = in.nGrid;
    pstGetFFTMaxSizes(pst,&inFFTSizes,sizeof(inFFTSizes),&outFFTSizes,sizeof(outFFTSizes));
    printf("Grid size %d x %d x %d, per node %d x %d x %d and %d x %d x %d\n",
	inFFTSizes.nx, inFFTSizes.ny, inFFTSizes.nz,
	inFFTSizes.nx, inFFTSizes.ny, outFFTSizes.nMaxZ,
	inFFTSizes.nx, outFFTSizes.nMaxY, inFFTSizes.nz);

    msrprintf("IC Generation @ a=%g with seed %d\n",in.dExpansion,param.iSeed);
    in.nPerNode = outFFTSizes.nMaxLocal;
    pstGenerateIC(pst,&in,sizeof(in),&out,sizeof(out));
    mean = 2*out.noiseMean / N;
    rms = sqrt(2*out.noiseCSQ / N);

    msrprintf("Transferring particles between/within nodes\n");
    pstMoveIC(pst,&in,sizeof(in),NULL,0);

    SetClasses();
    dsec = MSR::Time() - sec;
    msrprintf("IC Generation Complete @ a=%g, Wallclock: %f secs\n\n",out.dExpansion,dsec);
    msrprintf("Mean of noise same is %g, RMS %g.\n",mean,rms);

    return getTime(out.dExpansion);
    }
#endif

double MSR::Read(const char *achInFile) {
    double dTime,dExpansion;
    FIO fio;
    int j;
    double sec,dsec;
    uint64_t nSpecies[FIO_SPECIES_LAST];
    inReadFileFilename achFilename;
    uint64_t mMemoryModel = 0;
    LCL *plcl;
    PST pst0;

    pst0 = pst;
    while (pst0->nLeaves > 1)
	pst0 = pst0->pstLower;
    plcl = pst0->plcl;

    mMemoryModel = getMemoryModel();

    sec = MSR::Time();

    auto nBytes = PST_MAX_FILES*(sizeof(fioSpeciesList)+PST_FILENAME_SIZE);
    std::unique_ptr<char[]> buffer {new char[sizeof(inReadFile) + nBytes]};
    auto read = new (buffer.get()) inReadFile;

    /* Add Data Subpath for local and non-local names. */
    MSR::MakePath(param.achDataSubPath,achInFile,achFilename);
    fio = fioOpen(achFilename,csm->val.dOmega0,csm->val.dOmegab);
    if (fio==NULL) {
	fprintf(stderr,"ERROR: unable to open input file\n");
	perror(achFilename);
	Exit(1);
	}
    nBytes = fioDump(fio,nBytes,read+1);

    if (!fioGetAttr(fio,"dTime",FIO_TYPE_DOUBLE,&dExpansion)) dExpansion = 0.0;
    if (!fioGetAttr(fio,"dEcosmo",FIO_TYPE_DOUBLE,&dEcosmo)) dEcosmo = 0.0;
    if (!fioGetAttr(fio,"dTimeOld",FIO_TYPE_DOUBLE,&dTimeOld)) dTimeOld = 0.0;
    if (!fioGetAttr(fio,"dUOld",FIO_TYPE_DOUBLE,&dUOld)) dUOld = 0.0;

    if(!prmSpecified(prm, "dOmega0")) fioGetAttr(fio,"dOmega0",FIO_TYPE_DOUBLE,&csm->val.dOmega0);
    if(!prmSpecified(prm, "dLambda")) fioGetAttr(fio,"dLambda",FIO_TYPE_DOUBLE,&csm->val.dLambda);
    if(!prmSpecified(prm, "dBoxSize")) fioGetAttr(fio,"dBoxSize",FIO_TYPE_DOUBLE,&param.dBoxSize);
    if(!prmSpecified(prm, "h")) fioGetAttr(fio,"h",FIO_TYPE_DOUBLE,&param.h);

    N     = fioGetN(fio,FIO_SPECIES_ALL);
    nGas  = fioGetN(fio,FIO_SPECIES_SPH);
    nDark = fioGetN(fio,FIO_SPECIES_DARK);
    nStar = fioGetN(fio,FIO_SPECIES_STAR);
    nMaxOrder = N;

    read->nProcessors = param.bParaRead==0?1:(param.nParaRead<=1 ? nThreads:param.nParaRead);

    if (!fioGetAttr(fio,"nFiles",FIO_TYPE_UINT32,&j)) j = 1;
    printf("Reading %" PRIu64 " particles from %d file%s using %d processor%s\n",
	N, j, (j==1?"":"s"), read->nProcessors, (read->nProcessors==1?"":"s") );
    printf("  Dark: %" PRIu64 " Gas: %" PRIu64 " Star: %" PRIu64 "\n", nDark, nGas, nStar );

    dTime = getTime(dExpansion);
    if (param.bInFileLC) read->dvFac = 1.0;
    else read->dvFac = getVfactor(dExpansion);
    read->dTuFac = dTuFac;
    
    if (nGas && !prmSpecified(prm,"bDoGas")) param.bDoGas = 1;
    if (DoGas() || nGas) mMemoryModel |= (PKD_MODEL_SPH|PKD_MODEL_ACCELERATION|PKD_MODEL_VELOCITY|PKD_MODEL_NODE_SPHBNDS);		
    if (param.bStarForm || nStar) mMemoryModel |= (PKD_MODEL_SPH|PKD_MODEL_ACCELERATION|PKD_MODEL_VELOCITY|PKD_MODEL_MASS|PKD_MODEL_SOFTENING|PKD_MODEL_STAR);
    
    read->nNodeStart = 0;
    read->nNodeEnd = N - 1;


    for( auto s=FIO_SPECIES_ALL; s<FIO_SPECIES_LAST; s=FIO_SPECIES(s+1)) nSpecies[s] = fioGetN(fio,s);
    InitializePStore(nSpecies,mMemoryModel);

    read->dOmega0 = csm->val.dOmega0;
    read->dOmegab = csm->val.dOmegab;

    /*
    ** If bParaRead is 0, then we read serially; if it is 1, then we read
    ** in parallel using all available threads, otherwise we read in parallel
    ** using the specified number of threads.  The latter option will reduce
    ** the total amount of simultaneous I/O for file systems that cannot
    ** handle it.
    */

    if (param.bParaRead) {
	fioClose(fio);
	pstReadFile(pst,read,sizeof(struct inReadFile)+nBytes,NULL,0);
	}
    else {
	OneNodeRead(read,fio);
	fioClose(fio);
	}

    dsec = MSR::Time() - sec;
    SetClasses();
    printf("Input file has been successfully read, Wallclock: %f secs.\n", dsec);

    /*
    ** If this is a non-periodic box, then we must precalculate the bounds.
    ** We throw away the result, but PKD will keep track for later.
    */
    if (!param.bPeriodic ||
	    param.dxPeriod >= FLOAT_MAXVAL ||
	    param.dyPeriod >= FLOAT_MAXVAL ||
	    param.dzPeriod >= FLOAT_MAXVAL) {
	CalcBound();
	}

    InitCosmology();

    return dTime;
    }

// This sets the local pkd->bnd.
void MSR::CalcBound(Bound &bnd) {
    mdl->RunService(PST_CALCBOUND,&bnd);
    }
void MSR::CalcBound() {
    Bound bnd;
    CalcBound(bnd);
    }

void MSR::OutputGrid(const char *filename, bool k, int iGrid, int nParaWrite) {
    struct inOutput out;
    double dsec, sec = MSR::Time();
    out.eOutputType = k ? OUT_KGRID : OUT_RGRID;
    out.iGrid = iGrid;
    out.iPartner = -1;
    out.nPartner = -1;
    out.iProcessor = 0;
    out.nProcessor = nParaWrite > mdlProcs(mdl) ? mdlProcs(mdl) : nParaWrite;
    strcpy(out.achOutFile,filename);
    printf("Writing grid to %s ...\n",out.achOutFile);
    pstOutput(pst,&out,sizeof(out),NULL,0);
    dsec = MSR::Time() - sec;
    msrprintf("Grid has been successfully written, Wallclock: %f secs.\n\n", dsec);
    }

#ifdef MDL_FFTW
void MSR::OutputPk(int iStep,double dTime) {
    double a, z, vfact, kfact;
    std::string filename;
    int i;

    if (param.nGridPk == 0) return;

    std::unique_ptr<float[]>    fK    {new float[param.nBinsPk]};
    std::unique_ptr<float[]>    fPk   {new float[param.nBinsPk]};
    std::unique_ptr<float[]>    fPkAll{new float[param.nBinsPk]};
    std::unique_ptr<uint64_t[]> nPk   {new uint64_t[param.nBinsPk]};

    if (!csm->val.bComove) a = 1.0;
    else a = csmTime2Exp(csm,dTime);

    MeasurePk(param.iPkOrder,param.bPkInterlace,param.nGridPk,a,param.nBinsPk,nPk.get(),fK.get(),fPk.get(),fPkAll.get());

    /* If the Box Size (in mpc/h) was specified, then we can scale the output power spectrum measurement */
    if ( prmSpecified(prm,"dBoxSize") && param.dBoxSize > 0.0 ) kfact = param.dBoxSize;
    else kfact = 1.0;
    vfact = kfact * kfact * kfact;
    kfact = 1.0 / kfact;

    filename = BuildName(iStep,".pk");
    std::ofstream fs(filename);
    if (fs.fail()) {
	std::cerr << "Could not create P(k) file: " << filename << std::endl;
	perror(filename.c_str());
	Exit(errno);
	}
    for(i=0; i<param.nBinsPk; ++i) {
	if (fPk[i] > 0.0) fmt::print(fs,"{k} {pk} {nk} {all}\n",
		"k"_a   = kfact * fK[i] * 2.0 * M_PI,
		"pk"_a  = vfact * fPk[i],
		"nk"_a  = nPk[i],
		"all"_a = vfact * fPkAll[i]);
	}
    fs.close();
    /* Output the k-grid if requested */
    z = 1/a - 1;
    if (param.iDeltakInterval && (iStep % param.iDeltakInterval == 0) && z < param.dDeltakRedshift) {
	auto filename = BuildName(iStep,".deltak");
	OutputGrid(filename.c_str(),true,0,param.bParaWrite==0?1:(param.nParaWrite<=1 ? nThreads:param.nParaWrite));
	}
    }

void MSR::OutputLinPk(int iStep,double dTime) {
    std::string filename;
    double a, vfact, kfact;
    int i;

    if (param.nGridLin == 0) return;
    if (!csm->val.bComove) return;
    if (!prmSpecified(prm, "dBoxSize")) return; 

    std::unique_ptr<float[]>    fK    {new float[param.nBinsLinPk]};
    std::unique_ptr<float[]>    fPk   {new float[param.nBinsLinPk]};
    std::unique_ptr<uint64_t[]> nPk   {new uint64_t[param.nBinsLinPk]};

    a = csmTime2Exp(csm, dTime);

    MeasureLinPk(param.nGridLin,a,param.dBoxSize,nPk.get(),fK.get(),fPk.get());

    if (!csm->val.bComove) a = 1.0;
    else a = csmTime2Exp(csm,dTime);

    if ( param.dBoxSize > 0.0 ) kfact = param.dBoxSize;
    else kfact = 1.0;
    vfact = kfact * kfact * kfact;
    kfact = 1.0 / kfact;

    filename = BuildName(iStep,".lin_pk");
    std::ofstream fs(filename);
    if (fs.fail()) {
	    std::cerr << "Could not create P_lin(k) file: " << filename << std::endl;
	    perror(filename.c_str());
	    Exit(errno);
	    }
    for(i=0; i<param.nBinsLinPk; ++i) {
	if (fPk[i] > 0.0) fmt::print(fs,"{k} {pk} {nk}\n",
		"k"_a   = kfact * fK[i] * 2.0 * M_PI,
		"pk"_a  = vfact * fPk[i],
		"nk"_a  = nPk[i]);
	}
    fs.close();
    }

#endif

/*
**  This routine will output all requested files and fields
*/

void MSR::Output(int iStep, double dTime, double dDelta, int bCheckpoint) {
    int bSymmetric;

    printf( "Writing output for step %d\n", iStep );
    if ( iStep ) Write(BuildIoName(iStep).c_str(),dTime,bCheckpoint );

    if (DoGas() && !param.nSteps) {  /* Diagnostic Gas */ 
	Reorder();
	OutArray(BuildName(iStep,".uDot").c_str(),OUT_UDOT_ARRAY);
	OutArray(BuildName(iStep,".u").c_str(),OUT_U_ARRAY);
	OutArray(BuildName(iStep,".c").c_str(),OUT_C_ARRAY);
	OutArray(BuildName(iStep,".hsph").c_str(),OUT_HSPH_ARRAY);
	}

    if (DoDensity()) {
#ifdef FAST_GAS
	ActiveRung(3,1); /* Activate some particles */
	DomainDecomp();
	BuildTree(0);

	//msrSelSrcGas(msr);  /* FOR TESTING!! of gas active particles */
	FastGasPhase1(dTime,dDelta,SMX_DENSITY);
	FastGasPhase2(dTime,dDelta,SMX_PRINTNN);
	//msrSelSrcAll(msr);  /* FOR TESTING!! of gas active particles */
#else
	ActiveRung(0,1); /* Activate all particles */
	DomainDecomp(-1);
	BuildTree(0);
	bSymmetric = 0;  /* should be set in param file! */
	Smooth(dTime,dDelta,SMX_DENSITY,bSymmetric,param.nSmooth);
#endif
	}
    if ( param.bFindGroups ) {
	Reorder();
	//sprintf(achFile,"%s.fof",OutName());
	//OutArray(achFile,OUT_GROUP_ARRAY);
	HopWrite(BuildName(iStep,".fofstats").c_str());
	}
    if ( param.bFindHopGroups ) {
	ActiveRung(0,1); /* Activate all particles */
	DomainDecomp(-1);
	BuildTree(0);
	Hop(dTime,dDelta);
	Reorder();
	//OutArray(BuildName(iStep,".hopgrp").c_str(),OUT_GROUP_ARRAY);
	HopWrite(BuildName(iStep,".hopstats").c_str());
	}

    if (param.bDoAccOutput) {
	Reorder();
	OutVector(BuildName(iStep,".acc").c_str(),OUT_ACCEL_VECTOR);
	}
    if (param.bDoPotOutput) {
	Reorder();
	OutArray(BuildName(iStep,".pot").c_str(),OUT_POT_ARRAY);
	}

    if ( param.bTraceRelaxation) {
	Reorder();
	OutArray(BuildName(iStep,".relax").c_str(),OUT_RELAX_ARRAY);
	}
    if ( DoDensity() ) {
	Reorder();
	OutArray(BuildName(iStep,".den").c_str(),OUT_DENSITY_ARRAY);
	}
    if (param.bDoRungOutput) {
	Reorder();
	OutArray(BuildName(iStep,".rung").c_str(),OUT_RUNG_ARRAY);
	}
    if (param.bDoRungDestOutput) {
	Reorder();
	OutArray(BuildName(iStep,".rd").c_str(),OUT_RUNGDEST_ARRAY);
	}
    if (param.bDoSoftOutput) {
	Reorder();
	OutArray(BuildName(iStep,".soft").c_str(),OUT_SOFT_ARRAY);
	}
    /*
    ** Don't allow duplicate outputs.
    */
    while (OutTime(dTime));
    }

uint64_t MSR::CountSelected() {
    uint64_t N;
    mdl->RunService(PST_COUNTSELECTED,&N);
    return N;
    }
uint64_t MSR::SelSpecies(uint64_t mSpecies,bool setIfTrue,bool clearIfFalse) {
    uint64_t N;
    ServiceSelSpecies::input in(mSpecies,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELSPECIES,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelAll(bool setIfTrue,bool clearIfFalse) {
    return SelSpecies(1<<FIO_SPECIES_ALL,setIfTrue,clearIfFalse);
    }
uint64_t MSR::SelGas(bool setIfTrue,bool clearIfFalse) {
    return SelSpecies(1<<FIO_SPECIES_SPH,setIfTrue,clearIfFalse);
    }
uint64_t MSR::SelStar(bool setIfTrue,bool clearIfFalse) {
    return SelSpecies(1<<FIO_SPECIES_STAR,setIfTrue,clearIfFalse);
    }
uint64_t MSR::SelDark(bool setIfTrue,bool clearIfFalse) {
    return SelSpecies(1<<FIO_SPECIES_DARK,setIfTrue,clearIfFalse);
    }
uint64_t MSR::SelDeleted(bool setIfTrue,bool clearIfFalse) {
    // The "LAST" species (normally invalid) marks deleted particles
    return SelSpecies(1<<FIO_SPECIES_LAST,setIfTrue,clearIfFalse);
    }
uint64_t MSR::SelBlackholes(bool setIfTrue,bool clearIfFalse) {
    uint64_t N;
    ServiceSelBlackholes::input in(setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELBLACKHOLES,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelGroup(int iGroup,bool setIfTrue,bool clearIfFalse) {
    uint64_t N;
    ServiceSelGroup::input in(iGroup,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELGROUP,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelById(uint64_t idStart,uint64_t idEnd,int setIfTrue,int clearIfFalse) {
    uint64_t N;
    ServiceSelById::input in(idStart,idEnd,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELBYID,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelMass(double dMinMass,double dMaxMass,int setIfTrue,int clearIfFalse) {
    uint64_t N;
    ServiceSelMass::input in(dMinMass,dMaxMass,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELMASS,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelPhaseDensity(double dMinPhaseDensity,double dMaxPhaseDensity,int setIfTrue,int clearIfFalse) {
    uint64_t N;
    ServiceSelPhaseDensity::input in(dMinPhaseDensity,dMaxPhaseDensity,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELPHASEDENSITY,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelBox(double *dCenter, double *dSize,bool setIfTrue,bool clearIfFalse) {
    uint64_t N;
    ServiceSelBox::input in(dCenter,dSize,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELBOX,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelSphere(double *r, double dRadius,int setIfTrue,int clearIfFalse) {
    uint64_t N;
    ServiceSelSphere::input in(r,dRadius,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELBOX,sizeof(in),&in,&N);
    return N;
    }
uint64_t MSR::SelCylinder(double *dP1, double *dP2, double dRadius,
			   int setIfTrue, int clearIfFalse ) {
    uint64_t N;
    ServiceSelCylinder::input in(dP1,dP2,dRadius,setIfTrue,clearIfFalse);
    mdl->RunService(PST_SELCYLINDER,sizeof(in),&in,&N);
    return N;
    }

double MSR::TotalMass() {
    struct outTotalMass out;
    pstTotalMass(pst, NULL, 0, &out, sizeof(out));
    return out.dMass;
    }

void MSR::CalcDistance(const double *dCenter, double dRadius ) {
    struct inCalcDistance in;
    int j;

    for(j=0;j<3;j++) in.dCenter[j] = dCenter[j];
    in.dRadius = dRadius;
    in.bPeriodic = param.bPeriodic;
    pstCalcDistance(pst, &in, sizeof(in), NULL, 0);
    }

void MSR::CalcCOM(const double *dCenter, double dRadius,
		double *com, double *vcm, double *L, double *M) {
    struct inCalcCOM in;
    struct outCalcCOM out;
    int nOut;
    int j;
    double T[3];

    for(j=0;j<3;j++) in.dCenter[j] = dCenter[j];
    in.dRadius = dRadius;
    in.bPeriodic = param.bPeriodic;
    nOut = pstCalcCOM(pst, &in, sizeof(in), &out, sizeof(out));
    assert( nOut == sizeof(out) );

    *M = out.M;
    if ( out.M > 0.0 ) {
	for( j=0; j<3; j++ ) {
	    com[j] = out.com[j] / out.M;
	    vcm[j] = out.vcm[j] / out.M;
	    }
	cross_product(T, com, vcm);
	vec_add_const_mult(L,out.L,-out.M,T);
	for( j=0; j<3; j++ ) L[j] /= out.M;
	}
    }

uint64_t MSR::CountDistance(double dRadius2Inner, double dRadius2Outer) {
    struct inCountDistance in;
    struct outCountDistance out;
    int nOut;
    in.dRadius2Inner = dRadius2Inner;
    in.dRadius2Outer = dRadius2Outer;
    nOut = pstCountDistance(pst, &in, sizeof(in), &out, sizeof(out));
    assert( nOut == sizeof(out) );
    return out.nCount;
    }

double MSR::countSphere(double r,void *vctx) {
    auto *ctx = reinterpret_cast<SPHERECTX*>(vctx);
    ctx->nSelected = ctx->msr->CountDistance(0.0,r*r);
    return 1.0*ctx->nSelected - 1.0*ctx->nTarget;
    }

void MSR::profileRootFind( double *dBins, int lo, int hi, int nAccuracy, SPHERECTX *ctx ) {
    int nIter;
    int iBin = (lo+hi) / 2;
    if ( lo == iBin ) return;

    ctx->nTarget = d2u64((ctx->nTotal-ctx->nInner) * ctx->dFrac * iBin + ctx->nInner);
    dBins[iBin] = illinois( countSphere, ctx, dBins[lo], dBins[hi], 0.0, 1.0*nAccuracy, &nIter );
    profileRootFind(dBins,lo,iBin,nAccuracy,ctx);
    profileRootFind(dBins,iBin,hi,nAccuracy,ctx);
    }

double MSR::countShell(double rInner,void *vctx) {
    auto *ctx = reinterpret_cast<SHELLCTX*>(vctx);
    double rOuter;
    local_t nSelected;

    if ( rInner == ctx->rMiddle ) nSelected = 0;
    else {
	rOuter = pow(10,2.0*log10(ctx->rMiddle)-log10(rInner));
	nSelected = ctx->msr->CountDistance(rInner*rInner,rOuter*rOuter);
	}
    return 1.0*nSelected - 1.0*ctx->nTarget;
    }


/*
** Calculate a profile.
** Bins are of equal size (same number of particles) between dMinRadius and dLogRadius.
** From dLogRadius to dMaxRadius, the binning is done logarithmicly.
** Setting dLogRadius to dMinRadius results in purely logarithmic binning, while
** setting dLogRadius to dMaxRadius results in purely equal sized binning.
*/
void MSR::Profile( const PROFILEBIN **ppBins, int *pnBins,
		 double *r, double dMinRadius, double dLogRadius, double dMaxRadius,
		 int nPerBin, int nBins, int nAccuracy ) {
    SPHERECTX ctxSphere;
    SHELLCTX ctxShell;
    PROFILEBIN *pBins;
    double sec, dsec;
    double com[3], vcm[3], L[3], M;
    size_t inSize;
    int i,j;
    int nBinsInner;
    total_t N,n;
    LCL *plcl;
    PST pst0;

    assert(dMinRadius<=dLogRadius);
    assert(dLogRadius<=dMaxRadius);
    assert(dLogRadius==dMinRadius || nPerBin>0);
    assert(dLogRadius==dMaxRadius || nBins>0);

    if ( dLogRadius == dMaxRadius ) nBins = 0;

    pst0 = pst;
    while (pst0->nLeaves > 1)
        pst0 = pst0->pstLower;
    plcl = pst0->plcl;

    CalcDistance(r,dMaxRadius);
    CalcCOM(r,dMaxRadius,com,vcm,L,&M);

    if ( dLogRadius > dMinRadius ) {
	/*
	** The inner radius is calculated such that the logarithmic mid-point
	** falls on dMinRadius.  This is done so that the profile is plotted
	** all the way to the inner radius.  The correct radius must be between
	** dMinRadius and the logrithmic difference between dMinRadius and
	** dMaxRadius below dMinRadius.
	*/
	ctxShell.rMiddle = dMinRadius;
	ctxShell.nTarget = nPerBin;
	ctxShell.msr = this;
	dMinRadius = illinois( countShell, &ctxShell,
			       pow(10,2.0*log10(dMinRadius)-log10(dMaxRadius)), dMinRadius,
			       0.0, 0.0, NULL );
	N = CountDistance(dMinRadius*dMinRadius,dLogRadius*dLogRadius);
	nBinsInner = (N+nPerBin/2) / nPerBin;
	}
    else {
	double dOuter;

	nBinsInner = 0;

	/*
	** Calculate the logarithmic mid-point and verify that there are enough particles
	** in the first bin.  If not, invoke the root finder.
	*/
	ctxShell.rMiddle = dMinRadius;
	ctxShell.nTarget = nPerBin;
	ctxShell.msr = this;
	dMinRadius = pow(10,(2.0*(nBins+1)*log10(dMinRadius)-log10(dMaxRadius))/(2*nBins));
	dOuter = pow(10,2.0*log10(ctxShell.rMiddle)-log10(dMinRadius));
	N = CountDistance(dMinRadius*dMinRadius,dOuter*dOuter);
	if ( N < nPerBin-nAccuracy ) {
	    dMinRadius = illinois( countShell, &ctxShell,
				   pow(10,2.0*log10(dMinRadius)-log10(dMaxRadius)), dMinRadius,
				   0.0, 0.0, NULL );
	    }
	dLogRadius = dMinRadius;
	}

    inSize = sizeof(inProfile)-sizeof(inProfile::dRadii[0])*(sizeof(inProfile::dRadii)/sizeof(inProfile::dRadii[0])-nBins-nBinsInner-1);
    std::unique_ptr<char[]> buffer {new char[inSize]};
    auto in = new (buffer.get()) inProfile;

    in->dRadii[0] = dMinRadius;

    /*
    ** Inner, fixed size bins
    */
    if ( nBinsInner ) {
	sec = Time();
	msrprintf( "Root finding for %d bins\n", nBinsInner );
	ctxSphere.nTotal = CountDistance(0.0,dLogRadius*dLogRadius);
	ctxSphere.nInner = CountDistance(0.0,dMinRadius*dMinRadius);
	ctxSphere.msr = this;
	ctxSphere.dFrac = 1.0 / nBinsInner;
	in->dRadii[nBinsInner] = dLogRadius;
	profileRootFind( in->dRadii, 0, nBinsInner, nAccuracy, &ctxSphere );
	dsec = Time() - sec;
	msrprintf("Root finding complete, Wallclock: %f secs\n\n",dsec);
	}

    /*
    ** Now logarithmic binning for the outer region.  We still obey nPerBin
    ** as the minimum number of particles to include in each bin.
    */
    if ( nBins ) {
	double dLogMin;
	double dLogMax = log10(dMaxRadius);
	double dRadius;

	ctxSphere.nTotal = SelSphere(r,dMaxRadius,1,1);
	ctxSphere.msr = this;

	N = CountDistance(0.0,dLogRadius*dLogRadius);
	for( i=1; i<nBins; i++ ) {
	    int nBinsRem = nBins - i + 1;

	    dLogMin = log10(in->dRadii[nBinsInner+i-1]);
	    dRadius = pow(10,(dLogMax-dLogMin)/nBinsRem + dLogMin);
	    n = CountDistance(0.0,dRadius*dRadius);
	    if ( n-N < nPerBin-nAccuracy ) {
		ctxSphere.nTarget = N + nPerBin;
		dRadius = illinois( countSphere, &ctxSphere, 0.0, dMaxRadius,
				    0.0, 1.0*nAccuracy, NULL );
		n = ctxSphere.nSelected;
		}
	    in->dRadii[nBinsInner+i] = dRadius;
	    N = n;
	    }
	}

    nBins = nBins+nBinsInner;

    in->dRadii[nBins] = dMaxRadius;

    sec = Time();
    msrprintf( "Profiling\n" );
    for(i=0; i<3; i++) {
	in->dCenter[i] = r[i];
	in->com[i] = com[i];
	in->vcm[i] = vcm[i];
	in->L[i] = L[i];
	}
    in->nBins = nBins+1;
    in->uRungLo = 0;
    in->uRungHi = MaxRung()-1;
    pstProfile(pst, in, inSize, NULL, 0);

    /*
    ** Finalize bin values
    */
    pBins = plcl->pkd->profileBins;
    for( i=0; i<nBins+1; i++ ) {
	if ( pBins[i].dMassInBin > 0.0 ) {
	    pBins[i].vel_radial /= pBins[i].dMassInBin;
	    pBins[i].vel_radial_sigma /= pBins[i].dMassInBin;
	    pBins[i].vel_tang_sigma = sqrt(pBins[i].vel_tang_sigma / pBins[i].dMassInBin);
	    if (pBins[i].vel_radial_sigma > pBins[i].vel_radial*pBins[i].vel_radial)
		pBins[i].vel_radial_sigma = sqrt(pBins[i].vel_radial_sigma-pBins[i].vel_radial*pBins[i].vel_radial);
	    else
		pBins[i].vel_radial_sigma = 0.0;
	    for(j=0; j<3;j++) {
		pBins[i].L[j] /= pBins[i].dMassInBin;
		}
	    }
	}

    dsec = Time() - sec;
    msrprintf("Profiling complete, Wallclock: %f secs\n\n",dsec);

    if ( ppBins ) *ppBins = plcl->pkd->profileBins;
    if ( pnBins ) *pnBins = nBins+1;
    }

#ifdef MDL_FFTW
void MSR::GridCreateFFT(int nGrid) {
    struct inGridCreateFFT in;
    in.nGrid = nGrid;
    pstGridCreateFFT(pst, &in, sizeof(in), NULL, 0);
    }

void MSR::GridDeleteFFT() {
    pstGridDeleteFFT(pst, NULL, 0, NULL, 0);
    }

/* Important: call msrGridCreateFFT() before, and msrGridDeleteFFT() after */
void MSR::MeasurePk(int iAssignment,int bInterlace,int nGrid,double a,int nBins,uint64_t *nPk,float *fK,float *fPk,float *fPkAll) {
    double sec,dsec;

    GridCreateFFT(nGrid);

    if (nGrid/2 < nBins) nBins = nGrid/2;
    assert(nBins <= PST_MAX_K_BINS);

    sec = MSR::Time();
    printf("Measuring P(k) with grid size %d (%d bins)...\n",nGrid,nBins);

    AssignMass(iAssignment,0,0.0);
    DensityContrast(0);
    if (bInterlace) {
	AssignMass(iAssignment,1,0.5);
	DensityContrast(1);
	Interlace(0,1); // We no longer need grid 1
	}
    WindowCorrection(iAssignment,0);

    GridBinK(nBins,0,nPk,fK,fPk);
    if (csm->val.classData.bClass && param.nGridLin>0 && strlen(param.achPowerSpecies) > 0) {
	AddLinearSignal(0,param.iSeed,param.dBoxSize,a,param.bFixedAmpIC,param.dFixedAmpPhasePI * M_PI);
	GridBinK(nBins,0,nPk,fK,fPkAll);
	}
    else {
	for(auto i=0; i<nBins; ++i) fPkAll[i] = 0;
	}

    GridDeleteFFT();

    dsec = MSR::Time() - sec;
    printf("P(k) Calculated, Wallclock: %f secs\n\n",dsec);
    }

void MSR::MeasureLinPk(int nGrid, double dA, double dBoxSize,
                    uint64_t *nPk,float *fK,float *fPk) {
    struct inMeasureLinPk in;
    int i;
    double sec,dsec;

    sec = Time();

    in.nGrid = nGrid;
    in.nBins = param.nBinsLinPk;
    in.dBoxSize = dBoxSize;
    in.dA = dA;
    in.iSeed = param.iSeed;
    in.bFixed = param.bFixedAmpIC;
    in.fPhase = param.dFixedAmpPhasePI * M_PI;

    std::unique_ptr<struct outMeasureLinPk> out {new struct outMeasureLinPk};
    printf("Measuring P_lin(k) with grid size %d (%d bins)...\n",in.nGrid,in.nBins);
    pstMeasureLinPk(pst, &in, sizeof(in), out.get(), sizeof(*out));
    for( i=0; i<in.nBins; i++ ) {
	if ( out->nPower[i] == 0 ) fK[i] = fPk[i] = 0;
	else {
	    if (nPk) nPk[i] = out->nPower[i];
	    fK[i] = out->fK[i]/out->nPower[i];
	    fPk[i] = out->fPower[i]/out->nPower[i];
	    }
	}
    /* At this point, dPk[] needs to be corrected by the box size */

    dsec = Time() - sec;
    printf("P_lin(k) Calculated, Wallclock: %f secs\n\n",dsec);
    }

void MSR::SetLinGrid(double dTime, double dDelta,int nGrid, int bKickClose, int bKickOpen){
    printf("Setting force grids of linear species with nGridLin = %d \n", nGrid);
    double sec, dsec;
    sec = MSR::Time();

    struct inSetLinGrid in;
    in.nGrid = nGrid;

    int do_DeltaRho_lin_avg = 1;
    in.a0 = in.a1 = in.a = csmTime2Exp(csm, dTime);
    if (do_DeltaRho_lin_avg) {
	if (bKickClose) in.a0 = csmTime2Exp(csm, dTime - 0.5*dDelta);
	if (bKickOpen)  in.a1 = csmTime2Exp(csm, dTime + 0.5*dDelta);
	}

    in.dBSize = param.dBoxSize;
    /* Parameters for the grid realization */
    in.iSeed = param.iSeed;
    in.bFixed = param.bFixedAmpIC;
    in.fPhase = param.dFixedAmpPhasePI*M_PI;
    pstSetLinGrid(pst, &in, sizeof(in), NULL, 0);

    dsec = MSR::Time() - sec;
    printf("Force from linear species calculated, Wallclock: %f, secs\n\n", dsec);
    }

/* First call SetLinGrid() to setup the grid */
void MSR::LinearKick(double dTime, double dDelta, int bKickClose, int bKickOpen) {
    struct inLinearKick in;
    double dt = 0.5*dDelta;
    double sec, dsec;

    printf("Applying Linear Kick...\n");
    sec = MSR::Time();
    in.dtOpen = in.dtClose = 0.0;
    if (csm->val.bComove) {
	if (bKickClose) in.dtClose = csmComoveKickFac(csm,dTime-dt,dt);
	if (bKickOpen) in.dtOpen = csmComoveKickFac(csm,dTime,dt);
	}
    else {
	if (bKickClose) in.dtClose = dt;
	if (bKickOpen) in.dtOpen = dt;
	}
    pstLinearKick(pst, &in, sizeof(in), NULL, 0);
    dsec = MSR::Time() - sec;
    printf("Linear Kick Applied, Wallclock: %f secs\n\n",dsec);
    }
#endif

int MSR::GetParticles(int nIn, uint64_t *ID, struct outGetParticles *out) {
    int nOut;
    nOut = pstGetParticles(pst, ID, sizeof(uint64_t)*nIn, out, nIn*sizeof(struct outGetParticles));
    return nOut / sizeof(struct outGetParticles);
    }

void MSR::OutputOrbits(int iStep,double dTime) {
    int i;

    if (param.nOutputParticles) {
	struct outGetParticles particles[GET_PARTICLES_MAX];
	double dExp, dvFac;

	if (csm->val.bComove) {
	    dExp = csmTime2Exp(csm,dTime);
	    dvFac = 1.0/(dExp*dExp);
	    }
	else {
	    dExp = dTime;
	    dvFac = 1.0;
	    }

	GetParticles(param.nOutputParticles,param.iOutputParticles,particles);

	auto filename = BuildName(iStep,".orb");
	std::ofstream fs(filename);
	if (fs.fail()) {
	    std::cerr << "Could not create orbit file: " << filename << std::endl;
	    perror(filename.c_str());
	    Exit(errno);
	    }
	fmt::print(fs,"{n} {a}\n","n"_a=param.nOutputParticles, "a"_a=dExp);
	for(i=0; i<param.nOutputParticles; ++i) {
	    fmt::print(fs,"{id} {mass:.8e} {x:.16e} {y:.16e} {z:.16e} {vx:.8e} {vy:.8e} {vz:.8e} {phi:.8e}\n",
		"id"_a=particles[i].id, "mass"_a=particles[i].mass, "phi"_a=particles[i].phi,
		"x"_a=particles[i].r[0], "y"_a=particles[i].r[1], "z"_a=particles[i].r[2],
		"vx"_a=particles[i].v[0]*dvFac, "vy"_a=particles[i].v[1]*dvFac, "vz"_a=particles[i].v[2]*dvFac);
	    }
	fs.close();
	}
    }
