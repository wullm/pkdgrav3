#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>
#include "pkd.h"
#include "ic.h"
#include "RngStream.h"

#ifdef __cplusplus
#include <complex>
typedef std::complex<float> COMPLEX;
#define REAL(x) std::real(x)
#define IMAG(x) std::imag(x)
static const std::complex<float> I(0,1);
#else
#include <complex.h>
#define COMPLEX float complex
#define REAL(x) creal(x)
#define IMAG(x) cimag(x)
#endif

typedef union {
    FFTW3(real) *r;
    COMPLEX *k;
    } gridptr;

typedef struct {
    gsl_interp_accel *acc;
    gsl_spline *spline;
    double *tk, *tf;
    double spectral;
    double normalization;
    int nTf;
    } powerParameters;

static double power(powerParameters *P,double k) {
    double T = gsl_spline_eval(P->spline,log(k),P->acc);
    return pow(k,P->spectral) * P->normalization * T * T;
    }

typedef struct {
    powerParameters *P;
    double r;
    } varianceParameters;

static double variance_integrand(double ak, void * params) {
    varianceParameters *vprm = (varianceParameters *)params;
    double x, w;
    /* Window function for spherical tophat of given radius (e.g., 8 Mpc/h) */
    x = ak * vprm->r;
    w = 3.0*(sin(x)-x*cos(x))/(x*x*x);
    return power(vprm->P,ak)*ak*ak*w*w*4.0*M_PI;
    }

static double variance(powerParameters *P,double dRadius) {
    varianceParameters vprm;
    gsl_function F;
    double result, error;
    gsl_integration_workspace *W = gsl_integration_workspace_alloc (1000);
    vprm.P = P;
    vprm.r = dRadius; /* 8 Mpc/h for example */
    F.function = &variance_integrand;
    F.params = &vprm;
    gsl_integration_qag(&F, exp(P->tk[0]), exp(P->tk[P->nTf-1]),
	0.0, 1e-6, 1000, GSL_INTEG_GAUSS61, W, &result, &error);
    gsl_integration_workspace_free(W);
    return result;
    }

/* Gaussian noise in k-space. Note correction sqrt(2) because of FFT normalization. */
static COMPLEX pairc( RngStream g ) {
    float x1, x2, w;
    do {
	x1 = 2.0 * RngStream_RandU01(g) - 1.0;
	x2 = 2.0 * RngStream_RandU01(g) - 1.0;
	w = x1 * x1 + x2 * x2;
        } while ( w >= 1.0 || w == 0.0 ); /* Loop ~ 21.5% of the time */
    w = sqrt(-log(w)/w);
    return w * (x1 + I * x2);
    }

static int wrap(int v,int h,int m) {
    return v - (v > h ? m : 0);
    }

/*
** Generate Gaussian white noise in k-space. The noise is in the proper form for
** an inverse FFT. The complex conjugates in the Nyquist planes are correct, and
** the normalization is such that that the inverse FFT needs to be normalized
** by sqrt(Ngrid^3) compared with Ngrid^3 with FFT followed by IFFT.
*/
static void pkdGenerateNoise(PKD pkd,unsigned long seed,MDLFFT fft,COMPLEX *ic,double *mean,double *csq) {
    MDL mdl = pkd->mdl;
    const int nGrid = fft->kgrid->n3;
    const int iNyquist = nGrid / 2;
    RngStream g;
    unsigned long fullKey[6];
    int i,j,k,jj,kk;
    COMPLEX v_ny,v_wn;

    mdlGridCoord kfirst, klast, kindex;
    mdlGridCoordFirstLast(mdl,fft->kgrid,&kfirst,&klast,0);

    fullKey[0] = seed;
    fullKey[1] = fullKey[0];
    fullKey[2] = fullKey[0];
    fullKey[3] = seed;
    fullKey[4] = fullKey[3];
    fullKey[5] = fullKey[3];

    /* Remember, we take elements from the stream and because we have increased the
    ** precision, we take pairs. Also, the method of determining Gaussian deviates
    ** also requires pairs (so four elements from the random stream), and some of
    ** these pairs are rejected.
    */
    g = RngStream_CreateStream ("IC");
#ifndef USE_SINGLE
    RngStream_IncreasedPrecis (g, 1);
#endif
    RngStream_SetSeed(g,fullKey);

    *mean = *csq = 0.0;

    j = k = nGrid; /* Start with invalid values so we advance the RNG correctly. */
    for( kindex=kfirst; !mdlGridCoordCompare(&kindex,&klast); mdlGridCoordIncrement(&kindex) ) {
	if (j!=kindex.z || k!=kindex.y) {
	    assert(kindex.x==0); /* The contrary should work properly now but needs testing. */
	    j = kindex.z; /* Remember: z and y indexes are permuted in k-space */
	    k = kindex.y;

	    jj = j<=iNyquist ? j*2 : (nGrid-j)*2 + 1;
	    kk = k<=iNyquist ? k*2 : (nGrid-k)*2 + 1;

	    /* We need the sample for x==0 AND/OR x==iNyquist, usually both but at least one. */
	    RngStream_ResetStartStream (g);
	    if (kindex.z <= iNyquist && kindex.y <= iNyquist) { /* Positive zone */
		RngStream_AdvanceState (g, 0, (1LL<<40)*jj + (1LL<<20)*kk );
		v_ny = pairc(g);
		v_wn = pairc(g);
		if ( (kindex.z==0 || kindex.z==iNyquist)  && (kindex.y==0 || kindex.y==iNyquist) ) {
		    /* These are real because they must be a complex conjugate of themselves. */
		    v_ny = REAL(v_ny);
		    v_wn = REAL(v_wn);
		    /* DC mode is zero */
		    if ( kindex.y==0 && kindex.z==0) v_wn = 0.0;
		    }
		}
	    /* We need to generate the correct complex conjugates */
	    else {
		int jjc = (nGrid-j) % nGrid * 2;
		int kkc = (nGrid-k) % nGrid * 2;

		RngStream_AdvanceState (g, 0, (1LL<<40)*jjc + (1LL<<20)*kkc );
		v_ny = conj(pairc(g));
		v_wn = conj(pairc(g));
		RngStream_ResetStartStream (g);
		RngStream_AdvanceState (g, 0, (1LL<<40)*jj + (1LL<<20)*kk );
		pairc(g); pairc(g); /* Burn the two samples we didn't use. */
		}
	    if (kindex.z!=klast.z || kindex.y!=klast.y || klast.x>iNyquist) 
		ic[kindex.i-kindex.x+iNyquist] = v_ny;
	    if (kindex.x < iNyquist) {
		for(i=0; i<kindex.x; ++i) v_wn = pairc(g); /* (optional) advance to this sample */
		ic[kindex.i] = v_wn;
		}
	    }
	else if (kindex.x!=iNyquist) {
	    ic[kindex.i] = pairc(g);
	    }
	*mean += REAL(ic[kindex.i]) + IMAG(ic[kindex.i]);
	*csq += REAL(ic[kindex.i] * conj(ic[kindex.i]));
	}
    RngStream_DeleteStream(&g);
    }

#ifdef __cplusplus
extern "C"
#endif
int pkdGenerateIC(PKD pkd,MDLFFT fft,int iSeed,int nGrid,int b2LPT,double dBoxSize,
    struct csmVariables *cosmo,double a,int nTf, double *tk, double *tf,
    double *noiseMean, double *noiseCSQ) {
    MDL mdl = pkd->mdl;
    float twopi = 2.0 * 4.0 * atan(1.0);
    float itwopi = 1.0 / twopi;
    float inGrid = 1.0 / nGrid;
    float fftNormalize = inGrid*inGrid*inGrid;
    int i,j,k,idx;
    float ix, iy, iz;
    int iNyquist = nGrid / 2;
    float iLbox = twopi / dBoxSize;
    float iLbox3 = pow(iLbox,3.0);
    float ak, ak2, amp;
    float dOmega, D0, Da;
    float velFactor;
    float f1, f2;
    basicParticle *p;
    int nLocal;
    float dSigma8 = cosmo->dSigma8;
    CSM csm;

    csmInitialize(&csm);
    csm->val = *cosmo;

    mdlGridCoord kfirst, klast, kindex;
    mdlGridCoord rfirst, rlast, rindex;
    gridptr ic[10];

    powerParameters P;

    D0 = csmComoveGrowthFactor(csm,1.0);
    Da = csmComoveGrowthFactor(csm,a);
    dOmega = cosmo->dOmega0 / (a*a*a*pow(csmExp2Hub(csm, a)/cosmo->dHubble0,2.0));

    P.normalization = 1.0;
    P.spectral = cosmo->dSpectral;
    P.nTf = nTf;
    P.tk = tk;
    P.tf = tf;
    P.acc = gsl_interp_accel_alloc();
    P.spline = gsl_spline_alloc (gsl_interp_cspline, nTf);
    gsl_spline_init(P.spline, P.tk, P.tf, P.nTf);
    if (dSigma8 > 0) {
	dSigma8 *= Da/D0;
	P.normalization *= dSigma8*dSigma8 / variance(&P,8.0);
	}
    else if (cosmo->dNormalization > 0) {
	P.normalization = cosmo->dNormalization * Da/D0;
	dSigma8 = sqrt(variance(&P,8.0));
	}
    f1 = pow(dOmega,5.0/9.0);
    f2 = 2.0 * pow(dOmega,6.0/11.0);

    velFactor = csmExp2Hub(csm,a);
    velFactor *= a*a; /* Comoving */

    mdlGridCoordFirstLast(mdl,fft->kgrid,&kfirst,&klast,0);
    mdlGridCoordFirstLast(mdl,fft->rgrid,&rfirst,&rlast,0);
    assert(rlast.i == klast.i*2); /* Arrays must overlap here. */

    /* The mdlSetArray will use the values from thread 0 */
    ic[0].r = (FFTW3(real)*)pkdParticleBase(pkd);
    ic[1].r = ic[0].r + fft->rgrid->nLocal;
    ic[2].r = ic[1].r + fft->rgrid->nLocal;
    ic[3].r = ic[2].r + fft->rgrid->nLocal;
    ic[4].r = ic[3].r + fft->rgrid->nLocal;
    ic[5].r = ic[4].r + fft->rgrid->nLocal;
    ic[6].r = ic[5].r + fft->rgrid->nLocal;
    ic[7].r = ic[6].r + fft->rgrid->nLocal;
    ic[8].r = ic[7].r + fft->rgrid->nLocal;
    ic[9].r = ic[8].r + fft->rgrid->nLocal;

    ic[0].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[0].r);
    ic[1].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[1].r);
    ic[2].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[2].r);
    ic[3].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[3].r);
    ic[4].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[4].r);
    ic[5].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[5].r);
    ic[6].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[6].r);
    ic[7].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[7].r);
    ic[8].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[8].r);
    ic[9].r = (FFTW3(real) *)mdlSetArray(pkd->mdl,rlast.i,sizeof(FFTW3(real)),ic[9].r);

    /* Particles will overlap ic[0] through ic[5] eventually */
    nLocal = rlast.i / fft->rgrid->a1 * fft->rgrid->n1;
    p = (basicParticle *)mdlSetArray(pkd->mdl,nLocal,sizeof(basicParticle),pkdParticleBase(pkd));

    /* Generate white noise realization -> ic[6] */
    if (mdlSelf(mdl)==0) {printf("Generating random noise\n"); fflush(stdout); }
    pkdGenerateNoise(pkd,iSeed,fft,ic[6].k,noiseMean,noiseCSQ);

    if (mdlSelf(mdl)==0) {printf("Imprinting power\n"); fflush(stdout); }
    for( kindex=kfirst; !mdlGridCoordCompare(&kindex,&klast); mdlGridCoordIncrement(&kindex) ) {
	/* Range: (-iNyquist,iNyquist] */
	iy = wrap(kindex.z,iNyquist,fft->rgrid->n3);
	iz = wrap(kindex.y,iNyquist,fft->rgrid->n2);
	ix = wrap(kindex.x,iNyquist,fft->rgrid->n1);
	idx = kindex.i;

	/* Preserve complex conjugates in the Nyquist planes */
	if (kindex.x==0 || kindex.x==iNyquist) {
	    ix = abs(ix);
	    iy = abs(iy);
	    iz = abs(iz);
	    }
	ak2 = ix*ix + iy*iy + iz*iz;
	if (ak2>0) {
	    ak = sqrt(ak2) * iLbox;
	    amp = sqrt(power(&P,ak) * iLbox3) * itwopi / ak2;
	    }
	else amp = 0.0;
	ic[7].k[idx] = ic[6].k[idx] * amp * ix;
	ic[8].k[idx] = ic[6].k[idx] * amp * iy;
	ic[9].k[idx] = ic[6].k[idx] * amp * iz;
	if (b2LPT) {
	    ic[0].k[idx] = ic[7].k[idx] * twopi * ix * -I; /* xx */
	    ic[1].k[idx] = ic[8].k[idx] * twopi * iy * -I; /* yy */
	    ic[2].k[idx] = ic[9].k[idx] * twopi * iz * -I; /* zz */
	    ic[3].k[idx] = ic[7].k[idx] * twopi * iy * -I; /* xy */
	    ic[4].k[idx] = ic[8].k[idx] * twopi * iz * -I; /* yz */
	    ic[5].k[idx] = ic[9].k[idx] * twopi * ix * -I; /* zx */
	    }
	}

    if (mdlSelf(mdl)==0) {printf("Generating x displacements\n"); fflush(stdout); }
    mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[7].k );
    if (mdlSelf(mdl)==0) {printf("Generating y displacements\n"); fflush(stdout); }
    mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[8].k );
    if (mdlSelf(mdl)==0) {printf("Generating z displacements\n"); fflush(stdout); }
    mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[9].k );
    if (b2LPT) {
	if (mdlSelf(mdl)==0) {printf("Generating xx term\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[0].k );
	if (mdlSelf(mdl)==0) {printf("Generating yy term\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[1].k );
	if (mdlSelf(mdl)==0) {printf("Generating zz term\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[2].k );
	if (mdlSelf(mdl)==0) {printf("Generating xy term\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[3].k );
	if (mdlSelf(mdl)==0) {printf("Generating yz term\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[4].k );
	if (mdlSelf(mdl)==0) {printf("Generating xz term\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[5].k );

	/* Calculate the source term */
	if (mdlSelf(mdl)==0) {printf("Generating source term\n"); fflush(stdout); }
	for( rindex=rfirst; !mdlGridCoordCompare(&rindex,&rlast); mdlGridCoordIncrement(&rindex) ) {
	    int i = rindex.i;
	    ic[6].r[i] = ic[0].r[i]*ic[1].r[i] + ic[0].r[i]*ic[2].r[i] + ic[1].r[i]*ic[2].r[i]
		- ic[3].r[i]*ic[3].r[i] - ic[4].r[i]*ic[4].r[i] - ic[5].r[i]*ic[5].r[i];
	    }
	mdlFFT(mdl, fft, ic[6].r );
	}

    /* Move the 1LPT positions/velocities to the particle area */
    if (mdlSelf(mdl)==0) {printf("Transfering 1LPT results to output area\n"); fflush(stdout); }
    idx = 0;
    for( rindex=rfirst; !mdlGridCoordCompare(&rindex,&rlast); mdlGridCoordIncrement(&rindex) ) {
	float x = ic[7].r[rindex.i];
	float y = ic[8].r[rindex.i];
	float z = ic[9].r[rindex.i];
	p[idx].dr[0] = x;
	p[idx].dr[1] = y;
	p[idx].dr[2] = z;

	if (fabs(x) > 0.1) printf("x=%g\n",x);
	assert(fabs(x) < 0.1 );

	p[idx].v[0] = f1 * x * velFactor;
	p[idx].v[1] = f1 * y * velFactor;
	p[idx].v[2] = f1 * z * velFactor;
	++idx;
	}
    assert(idx == nLocal);

    if (b2LPT) {
	for(kindex=kfirst; !mdlGridCoordCompare(&kindex,&klast); mdlGridCoordIncrement(&kindex)) {
	    iy = wrap(kindex.z,iNyquist,fft->rgrid->n3);
	    iz = wrap(kindex.y,iNyquist,fft->rgrid->n2);
	    ix = wrap(kindex.x,iNyquist,fft->rgrid->n1);
	    idx = kindex.i;
	    ak2 = ix*ix + iy*iy + iz*iz;
	    if (ak2>0.0) {
		float f = (-3.0/7.0) / (ak2 * twopi);
		ic[7].k[idx] = f * ic[6].k[idx] * ix * -I;
		ic[8].k[idx] = f * ic[6].k[idx] * iy * -I;
		ic[9].k[idx] = f * ic[6].k[idx] * iz * -I;
		}
	    else ic[7].k[idx] = ic[8].k[idx] = ic[9].k[idx] = 0.0;
	    }

	if (mdlSelf(mdl)==0) {printf("Generating x2 displacements\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[7].k );
	if (mdlSelf(mdl)==0) {printf("Generating y2 displacements\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[8].k );
	if (mdlSelf(mdl)==0) {printf("Generating z2 displacements\n"); fflush(stdout); }
	mdlIFFT(mdl, fft, (FFTW3(complex)*)ic[9].k );

	/* Add the 2LPT positions/velocities corrections to the particle area */
	if (mdlSelf(mdl)==0) {printf("Transfering 2LPT results to output area\n"); fflush(stdout); }
	idx = 0;
	for( rindex=rfirst; !mdlGridCoordCompare(&rindex,&rlast); mdlGridCoordIncrement(&rindex) ) {
	    float x = ic[7].r[rindex.i] * fftNormalize;
	    float y = ic[8].r[rindex.i] * fftNormalize;
	    float z = ic[9].r[rindex.i] * fftNormalize;

	    p[idx].dr[0] += x;
//	    if (fabs(x) > 0.1) printf("x=%g\n",x);
//	    assert(fabs(x) < 0.1 );
	    p[idx].dr[1] += y;
	    p[idx].dr[2] += z;
	    p[idx].v[0] += f2 * x * velFactor;
	    p[idx].v[1] += f2 * y * velFactor;
	    p[idx].v[2] += f2 * z * velFactor;
	    ++idx;
	    }
	printf("%d idx:%d rfirst.i=%d\n",mdlSelf(mdl),idx,rfirst.i);
	while(idx--) {
	    assert(p[idx].dr[0] !=0);
//	    assert(p[idx].dr[1] !=0);
//	    assert(p[idx].dr[2] !=0);
	    }


	}
    gsl_spline_free(P.spline);
    gsl_interp_accel_free(P.acc);

    csmFinish(csm);

    return nLocal;
    }