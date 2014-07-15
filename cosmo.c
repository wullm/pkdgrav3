#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#ifdef CRAY_T3D
#include "hyperlib.h"
#endif

#include "cosmo.h"

/*
 * Cosmological module for PKDGRAV.
 * N.B.  This code is being shared with skid and the I.C. generator.
 */

void csmInitialize(CSM *pcsm) {
    CSM csm;

    csm = (CSM) malloc(sizeof(struct csmContext));
    assert(csm != NULL);

    csm->dHubble0 = 0.0;
    csm->dOmega0 = 0.0;
    csm->dLambda = 0.0;
    csm->dOmegaDE = 0.0;
    csm->w0 = 0.0;
    csm->wa = 0.0;
    csm->dOmegaRad = 0.0;
    csm->dOmegab = 0.0;
    csm->bComove = 0;

    *pcsm = csm;
    }

void csmFinish(CSM csm) {
    free(csm);
    }

#define EPSCOSMO 1e-7

double dRombergO(void *CTX, double (*func)(void *, double), double a,
		 double b, double eps);
/*
 ** The cosmological equation of state is entirely determined here.  We
 ** will derive all other quantities from this function.
 */

double csmExp2Hub(CSM csm, double dExp) {
    double dOmegaCurve = 1.0 - csm->dOmega0 -
			 csm->dLambda - csm->dOmegaDE - csm->dOmegaRad;

    assert(dExp > 0.0);
    return csm->dHubble0
	   *sqrt(csm->dOmega0*dExp
		 + dOmegaCurve*dExp*dExp
		 + csm->dOmegaRad
		 + csm->dOmegaDE*pow(dExp,1.0 - 3.0*(csm->w0 + csm->wa))*exp(-3.0*csm->wa*(1.0 - dExp))
		 + csm->dLambda*dExp*dExp*dExp*dExp)/(dExp*dExp);
    }


double csmTime2Hub(CSM csm,double dTime) {
    double a = csmTime2Exp(csm,dTime);

    assert(a > 0.0);
    return csmExp2Hub(csm, a);
    }

double csmCosmoTint(CSM csm, double dY) {
    double dExp = pow(dY, 2.0/3.0);

    assert(dExp > 0.0);
    return 2.0/(3.0*dY*csmExp2Hub(csm, dExp));
    }

double csmExp2Time(CSM csm,double dExp) {
    double dOmega0 = csm->dOmega0;
    double dHubble0 = csm->dHubble0;
    double a0,A,B,eta;

    if (!csm->bComove) {
	/*
	 ** Invalid call!
	 */
	assert(0);
	}
    if (csm->dLambda == 0.0 && csm->dOmegaRad == 0.0) {
	if (dOmega0 == 1.0) {
	    assert(dHubble0 > 0.0);
	    if (dExp == 0.0) return(0.0);
	    return(2.0/(3.0*dHubble0)*pow(dExp,1.5));
	    }
	else if (dOmega0 > 1.0) {
	    assert(dHubble0 >= 0.0);
	    if (dHubble0 == 0.0) {
		B = 1.0/sqrt(dOmega0);
		eta = acos(1.0-dExp);
		return(B*(eta-sin(eta)));
		}
	    if (dExp == 0.0) return(0.0);
	    a0 = 1.0/dHubble0/sqrt(dOmega0-1.0);
	    A = 0.5*dOmega0/(dOmega0-1.0);
	    B = A*a0;
	    eta = acos(1.0-dExp/A);
	    return(B*(eta-sin(eta)));
	    }
	else if (dOmega0 > 0.0) {
	    assert(dHubble0 > 0.0);
	    if (dExp == 0.0) return(0.0);
	    a0 = 1.0/dHubble0/sqrt(1.0-dOmega0);
	    A = 0.5*dOmega0/(1.0-dOmega0);
	    B = A*a0;
	    eta = acosh(dExp/A+1.0);
	    return(B*(sinh(eta)-eta));
	    }
	else if (dOmega0 == 0.0) {
	    assert(dHubble0 > 0.0);
	    if (dExp == 0.0) return(0.0);
	    return(dExp/dHubble0);
	    }
	else {
	    /*
	     ** Bad value.
	     */
	    assert(0);
	    return(0.0);
	    }
	}
    else {
	return dRombergO(csm, (double (*)(void *, double)) csmCosmoTint,
			 0.0, pow(dExp, 1.5), EPSCOSMO);
	}
    }

#define MAX_ITER 100

double csmTime2Exp(CSM csm,double dTime) {
    double al=0,ah=1,a0,a1=1,at,a;
    double th,f,f1,h,ho;
    int j;

    if (!csm->bComove) return(1.0);
    else {
	assert(dTime > 0);
	th = csmExp2Time(csm,ah);
	/*
	** Search for upper bracket if needed.
	*/
	while (dTime > th) {
	    a0 = a1;
	    a1 = ah;
	    ah = a1+a0;
	    th = csmExp2Time(csm,ah);
	    }
	a = 0.5*(al+ah);
	ho = ah-al;
	h = ho;
	f = dTime - dRombergO(csm, (double (*)(void *, double)) csmCosmoTint,0.0,pow(a,1.5),EPSCOSMO);
	f1 = 1/(a*csmExp2Hub(csm,a));
	for (j=0;j<MAX_ITER;++j) {
	    if (a+f/f1 < al || a+f/f1 > ah || fabs(2*f) > fabs(ho*f1)) {
		/*
		** Bisection Step.
		*/
		ho = h;
		h = 0.5*(ah-al);
		a = al+h;
		/*
				printf("bisect al:%.14g ah:%.14g a:%.14g\n",al,ah,a);
		*/
		if (a == al) return a;
		}
	    else {
		/*
		** Newton Step.
		*/
		ho = h;
		h = f/f1;
		at = a;
		a += h;
		/*
				printf("newton al:%.14g ah:%.14g a:%.14g\n",al,ah,a);
		*/
		if (a == at) return a;
		}
	    if (fabs(h) < EPSCOSMO) {
		/*
				printf("converged al:%.14g ah:%.14g a:%.14g t:%.14g == %.14g\n",
				       al,ah,a,dRombergO(csm, (double (*)(void *, double)) csmCosmoTint,0.0,pow(a,1.5),EPSCOSMO*1e-1),
				       dTime);
		*/
		return a;
		}
	    f = dTime - dRombergO(csm, (double (*)(void *, double)) csmCosmoTint,0.0,pow(a,1.5),EPSCOSMO*1e-1);
	    f1 = 1/(a*csmExp2Hub(csm,a));
	    if (f < 0) ah = a;
	    else al = a;
	    }
	assert(0);
	}
    return 0.0; /* We never reach here, but this keeps the compiler happy */
    }


double csmComoveDriftInt(CSM csm, double dIExp) {
    return -dIExp/(csmExp2Hub(csm, 1.0/dIExp));
    }

/*
 ** Make the substitution y = 1/a to integrate da/(a^2*H(a))
 */
double csmComoveKickInt(CSM csm, double dIExp) {
    return -1.0/(csmExp2Hub(csm, 1.0/dIExp));
    }

/*
 ** This function integrates the time dependence of the "drift"-Hamiltonian.
 */
double csmComoveDriftFac(CSM csm,double dTime,double dDelta) {
    double dOmega0 = csm->dOmega0;
    double dHubble0 = csm->dHubble0;
    double a0,A,B,a1,a2,eta1,eta2;

    if (!csm->bComove) return(dDelta);
    else if (csm->dLambda == 0.0 && csm->dOmegaRad == 0.0) {
	a1 = csmTime2Exp(csm,dTime);
	a2 = csmTime2Exp(csm,dTime+dDelta);
	if (dOmega0 == 1.0) {
	    return((2.0/dHubble0)*(1.0/sqrt(a1) - 1.0/sqrt(a2)));
	    }
	else if (dOmega0 > 1.0) {
	    assert(dHubble0 >= 0.0);
	    if (dHubble0 == 0.0) {
		A = 1.0;
		B = 1.0/sqrt(dOmega0);
		}
	    else {
		a0 = 1.0/dHubble0/sqrt(dOmega0-1.0);
		A = 0.5*dOmega0/(dOmega0-1.0);
		B = A*a0;
		}
	    eta1 = acos(1.0-a1/A);
	    eta2 = acos(1.0-a2/A);
	    return(B/A/A*(1.0/tan(0.5*eta1) - 1.0/tan(0.5*eta2)));
	    }
	else if (dOmega0 > 0.0) {
	    assert(dHubble0 > 0.0);
	    a0 = 1.0/dHubble0/sqrt(1.0-dOmega0);
	    A = 0.5*dOmega0/(1.0-dOmega0);
	    B = A*a0;
	    eta1 = acosh(a1/A+1.0);
	    eta2 = acosh(a2/A+1.0);
	    return(B/A/A*(1.0/tanh(0.5*eta1) - 1.0/tanh(0.5*eta2)));
	    }
	else if (dOmega0 == 0.0) {
	    /*
	     ** YOU figure this one out!
	     */
	    assert(0);
	    return(0.0);
	    }
	else {
	    /*
	     ** Bad value?
	     */
	    assert(0);
	    return(0.0);
	    }
	}
    else {
	return dRombergO(csm,
			 (double (*)(void *, double)) csmComoveDriftInt,
			 1.0/csmTime2Exp(csm, dTime),
			 1.0/csmTime2Exp(csm, dTime + dDelta), EPSCOSMO);
	}
    }


/*
 ** This function integrates the time dependence of the "kick"-Hamiltonian.
 */
double csmComoveKickFac(CSM csm,double dTime,double dDelta) {
    double dOmega0 = csm->dOmega0;
    double dHubble0 = csm->dHubble0;
    double a0,A,B,a1,a2,eta1,eta2;

    if (!csm->bComove) return(dDelta);
    else if (csm->dLambda == 0.0 && csm->dOmegaRad == 0.0) {
	a1 = csmTime2Exp(csm,dTime);
	a2 = csmTime2Exp(csm,dTime+dDelta);
	if (dOmega0 == 1.0) {
	    return((2.0/dHubble0)*(sqrt(a2) - sqrt(a1)));
	    }
	else if (dOmega0 > 1.0) {
	    assert(dHubble0 >= 0.0);
	    if (dHubble0 == 0.0) {
		A = 1.0;
		B = 1.0/sqrt(dOmega0);
		}
	    else {
		a0 = 1.0/dHubble0/sqrt(dOmega0-1.0);
		A = 0.5*dOmega0/(dOmega0-1.0);
		B = A*a0;
		}
	    eta1 = acos(1.0-a1/A);
	    eta2 = acos(1.0-a2/A);
	    return(B/A*(eta2 - eta1));
	    }
	else if (dOmega0 > 0.0) {
	    assert(dHubble0 > 0.0);
	    a0 = 1.0/dHubble0/sqrt(1.0-dOmega0);
	    A = 0.5*dOmega0/(1.0-dOmega0);
	    B = A*a0;
	    eta1 = acosh(a1/A+1.0);
	    eta2 = acosh(a2/A+1.0);
	    return(B/A*(eta2 - eta1));
	    }
	else if (dOmega0 == 0.0) {
	    /*
	     ** YOU figure this one out!
	     */
	    assert(0);
	    return(0.0);
	    }
	else {
	    /*
	     ** Bad value?
	     */
	    assert(0);
	    return(0.0);
	    }
	}
    else {
	return dRombergO(csm,
			 (double (*)(void *, double)) csmComoveKickInt,
			 1.0/csmTime2Exp(csm, dTime),
			 1.0/csmTime2Exp(csm, dTime + dDelta), EPSCOSMO);
	}
    }

double csmComoveLookbackTime2Exp(CSM csm,double dComoveTime) {
    if (!csm->bComove) return(1.0);
    else {
	double dExpOld = 0.0;
	double dT0 = csmExp2Time(csm, 1.0);
	double dTime = dT0 - dComoveTime;
	double dExpNew;
	int it = 0;

	if (dTime < EPSCOSMO) dTime = EPSCOSMO;
	dExpNew = csmTime2Exp(csm, dTime);
	/*
	 * Root find with Newton's method.
	 */
	do {
	    double dTimeNew = csmExp2Time(csm, dExpNew);
	    double f = dComoveTime
		       - csmComoveKickFac(csm, dTimeNew, dT0 - dTimeNew);
	    double fprime = -1.0/(dExpNew*dExpNew*csmExp2Hub(csm, dExpNew));
	    dExpOld = dExpNew;
	    dExpNew += f/fprime;
	    it++;
	    assert(it < 20);
	    }
	while (fabs(dExpNew - dExpOld)/dExpNew > EPSCOSMO);
	return dExpNew;
	}
    }
