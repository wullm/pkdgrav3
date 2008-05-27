#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <math.h>
#include "pkd.h"

static void combProfileBins(void *b1, void *b2) {
    PROFILEBIN * pBin1 = (PROFILEBIN *)b1;
    PROFILEBIN * pBin2 = (PROFILEBIN *)b2;

    pBin1->dMassInBin += pBin2->dMassInBin;
    pBin1->nParticles += pBin2->nParticles;
    }

static void initProfileBins(void *b) {
    PROFILEBIN * pBin = (PROFILEBIN *)b;
    pBin->dRadius = 0.0;
    pBin->dMassInBin = 0.0;
    pBin->dVolume = 0.0;
    pBin->nParticles = 0;
    }




/*
** This function will calculate the distance between a particle and a
** reference point.  If a periodic boundary is in effect then the smallest
** possible distance is returned.
*/
double pkdGetDistance2(PKD pkd,PARTICLE *p, double *dCenter ) {
    double d2;
    double dx,dx2;
    int j;

    d2 = 0.0;
    for( j=0; j<3; j++ ) {
	dx = p->r[j] - dCenter[j];
	/*
	** If a periodic wrap results in a smaller distance, then use that.
	*/
	if ( pkd->param.bPeriodic ) {
	    if ( dx<0.0 ) dx2 = dx + pkd->fPeriod[j];
	    else dx2 = dx - pkd->fPeriod[j];
	    if ( dx2*dx2 < dx*dx ) dx = dx2;
	    }
	d2 += dx*dx;
	}
    return d2;
    }

/*
** Count the number of particles in a given shell from
** [dMinRadius,dMaxRadius).
*/
int pkdShellCount(PKD pkd, uint8_t uRungLo, uint8_t uRungHi,
		  double *dCenter, double dMinRadius, double dMaxRadius ) {
    PARTICLE *p;
    int n, i, iCount;
    double d2,r2min, r2max;

    r2min = dMinRadius*dMinRadius;
    r2max = dMaxRadius*dMaxRadius;
    n = pkdLocal(pkd);
    iCount = 0;
    for (i=0;i<n;++i) {
	p = pkdParticle(pkd,i);
	if (pkdIsSrcActive(p,uRungLo,uRungHi)) {
	    d2 = pkdGetDistance2(pkd,p,dCenter);
	    if ( d2>=r2min || d2 < r2max )
		iCount ++;
	    }
	}

    return iCount;
    }

static int cmpRadiusLite(const void *pva,const void *pvb) {
    PLITE *pa = (PLITE *)pva;
    PLITE *pb = (PLITE *)pvb;
    double d = pa->r[0] - pb->r[0];
    if ( d > 0 ) return 1;
    else if ( d < 0 ) return -1;
    return 0;
    }

/*
** Use the pLite structure to calculate the distance to each particle
** Sort by distance when finished.
*/
void pkdCalcDistance(PKD pkd, double *dCenter) {
    PLITE *pl = pkd->pLite;
    int i;

    /*
    ** Initialize the temporary particles.
    */
    for (i=0;i<pkd->nLocal;++i) {
	PARTICLE *p = pkdParticle(pkd,i);
	pl[i].r[0] = pkdGetDistance2(pkd,p,dCenter);
	pl[i].r[1] = pkdMass(pkd,p);
	pl[i].r[2] = 0.0;
	pl[i].i = i;
	}
    qsort(pkd->pLite,pkdLocal(pkd),sizeof(PLITE),cmpRadiusLite);
    }

/*
** Count the number of elements that are interior to r2
*/
uint_fast32_t pkdCountDistance(PKD pkd, double r2 ) {
    PLITE *pl = pkd->pLite;
    uint64_t lo,hi,i;

    lo = 0;
    hi = pkd->nLocal;
    while( lo<hi ) {
	i = (lo+hi) / 2;
	if ( pl[i].r[0] > r2 ) hi = i;
	else lo = i+1;
	}
    return hi;
    }

/*
** Density Profile
*/
void pkdProfile(PKD pkd, uint8_t uRungLo, uint8_t uRungHi,
		double *dCenter, double *dRadii, int nBins) {
    PLITE *pl = pkd->pLite;
    double r;
    int i,iBin,lo,hi;
    PROFILEBIN *pBin;

    if ( pkd->profileBins != NULL ) mdlFree(pkd->mdl,pkd->profileBins);
    pkd->profileBins = mdlMalloc(pkd->mdl,nBins*sizeof(PROFILEBIN));
    assert( pkd->profileBins != NULL );
    r = 0.0;

    lo = 0;
    for(iBin=0;iBin<nBins;iBin++) {
	assert( dRadii[iBin] > r );
	pkd->profileBins[iBin].nParticles = 0;
	pkd->profileBins[iBin].dMassInBin = 0.0;
	pkd->profileBins[iBin].dRadius = dRadii[iBin];
	pkd->profileBins[iBin].dVolume = (4.0/3.0) * M_PI * (dRadii[iBin]*dRadii[iBin]*dRadii[iBin] - r*r*r);
	hi = pkdCountDistance(pkd,dRadii[iBin]*dRadii[iBin]);
	for(i=lo; i<hi; i++ ) {
	    pkd->profileBins[iBin].dMassInBin += pl[i].r[1];
	    pkd->profileBins[iBin].nParticles++;
	    }
	r = dRadii[iBin];
	lo = hi;
	}

    /* Combine the work from all processors */
    mdlCOcache(pkd->mdl,CID_BIN,pkd->profileBins,sizeof(PROFILEBIN),nBins,initProfileBins,combProfileBins);
    if (pkd->idSelf != 0) {
	for (i=0; i<nBins; i++) {
	    if (pkd->profileBins[i].dMassInBin > 0.0) {
		pBin = mdlAquire(pkd->mdl,CID_BIN,i,0);
		*pBin = pkd->profileBins[i];
		mdlRelease(pkd->mdl,CID_BIN,pBin);
		}
	    }
	}
    mdlFinishCache(pkd->mdl,CID_BIN);

    /* Only the main processor needs the result */
    if (pkd->idSelf != 0) {
	mdlFree(pkd->mdl,pkd->profileBins);
	pkd->profileBins = NULL;
	}
    }