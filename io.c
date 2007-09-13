#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <math.h>
#ifdef USE_PNG
#include <gd.h>
#include <gdfontg.h>
#endif

#include "iohdf5.h"
#include "pst.h"
#include "io.h"

#define CHUNKSIZE (32*1024)
#define EPSILON (-1e20)

/* Create an HDF5 file for output */
hid_t ioCreate( const char *filename ) {
    hid_t fileID;

    /* Create the output file */
    fileID = H5Fcreate( filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT );
    H5assert(fileID);

    return fileID;
}

static void ioSave(IO io, const char *filename, double dTime,
		   double dEcosmo, double dTimeOld, double dUOld,
		   int bDouble )
{
    hid_t fileID;
    IOHDF5 iohdf5;
    IOHDF5V ioDen;
    IOHDF5V ioPot;
    uint_fast32_t i;

    /* Create the output file */
    fileID = ioCreate(filename);
    iohdf5 = ioHDF5Initialize( fileID, CHUNKSIZE, bDouble );
    ioDen  = ioHDFF5NewVector( iohdf5, "density",  IOHDF5_SINGLE );
    ioPot  = ioHDFF5NewVector( iohdf5, "potential",IOHDF5_SINGLE );

    ioHDF5WriteAttribute( iohdf5, "dTime",   H5T_NATIVE_DOUBLE, &dTime );
    ioHDF5WriteAttribute( iohdf5, "dEcosmo", H5T_NATIVE_DOUBLE, &dEcosmo );
    ioHDF5WriteAttribute( iohdf5, "dTimeOld",H5T_NATIVE_DOUBLE, &dTimeOld );
    ioHDF5WriteAttribute( iohdf5, "dUOld",   H5T_NATIVE_DOUBLE, &dUOld );

    for( i=0; i<io->N; i++ ) {
//	ioHDF5AddDark(iohdf5, io->iMinOrder+i,
//		   io->r[i].v, io->v[i].v,
//		   io->m[i], io->s[i], io->p[i] );
	ioHDF5AddDark(iohdf5, io->iMinOrder+i,
		   io->r[i].v, io->v[i].v,
		   0.0, 0.0, io->p[i] );
	ioHDF5AddVector( ioDen, io->iMinOrder+i, io->d[i] );
	ioHDF5AddVector( ioPot, io->iMinOrder+1, io->p[i] );
    }
    ioHDF5Finish(iohdf5);

    H5assert(H5Fflush(fileID,H5F_SCOPE_GLOBAL));
    H5assert(H5Fclose(fileID));
}

void ioInitialize(IO *pio,MDL mdl)
{
    IO io;
    io = (IO)malloc(sizeof(struct ioContext));
    mdlassert(mdl,io != NULL);
    io->mdl = mdl;

    io->N = 0;
    io->r = NULL;
    io->v = NULL;
//    io->m = NULL;
//    io->s = NULL;
    io->d = NULL;
    io->p = NULL;

    *pio = io;
}

void ioAddServices(IO io,MDL mdl)
{
    mdlAddService(mdl,IO_START_SAVE,io,
		  (void (*)(void *,void *,int,void *,int *)) ioStartSave,
		  sizeof(struct inStartSave),0);
    mdlAddService(mdl,IO_START_RECV,io,
		  (void (*)(void *,void *,int,void *,int *)) ioStartRecv,
		  sizeof(struct inStartRecv),0);
#ifdef USE_PNG
    mdlAddService(mdl,IO_MAKE_PNG,io,
		  (void (*)(void *,void *,int,void *,int *)) ioMakePNG,
		  sizeof(struct inMakePNG),0);
#endif
}

/*
**  This service is called from the peer root node (processor 0) and initiates
**  a save process.
*/
void ioStartSave(IO io,void *vin,int nIn,void *vout,int *pnOut)
{
    int id;
    struct inStartSave *save = vin;
    struct inStartRecv recv;
    uint_fast32_t iCount;
#ifdef USE_PNG
	struct inMakePNG png;
#endif

    mdlassert(io->mdl,sizeof(struct inStartSave)==nIn);
    mdlassert(io->mdl,mdlSelf(io->mdl)==0);

    mdlSetComm(io->mdl,0); /* Talk to our peers */
    recv.dTime   = save->dTime;
    recv.dEcosmo = save->dEcosmo;
    recv.dTimeOld= save->dTimeOld;
    recv.dUOld   = save->dUOld;
    strcpy(recv.achOutName,save->achOutName);
    recv.bCheckpoint = save->bCheckpoint;
    iCount = save->N / mdlIO(io->mdl);
    for( id=1; id<mdlIO(io->mdl); id++ ) {
	recv.iIndex = iCount * id;
	recv.nCount = iCount;
	if ( id+1 == mdlIO(io->mdl) )
	    recv.nCount = save->N - id*iCount;
	mdlReqService(io->mdl,id,IO_START_RECV,&recv,sizeof(recv));
    }

    recv.iIndex = 0;
    recv.nCount = iCount;
    ioStartRecv(io,&recv,sizeof(recv),NULL,0);

    for( id=1; id<mdlIO(io->mdl); id++ ) {
	mdlGetReply(io->mdl,id,NULL,NULL);
    }

#ifdef USE_PNG
    png.iResolution = 1024;
    png.minValue = -1;
    png.maxValue = 5;
    strcpy(png.achOutName,save->achOutName);
    for( id=1; id<mdlIO(io->mdl); id++ ) {
	mdlReqService(io->mdl,id,IO_MAKE_PNG,&png,sizeof(png));
    }
    ioMakePNG(io,&png,sizeof(png),NULL,0);

    for( id=1; id<mdlIO(io->mdl); id++ ) {
	mdlGetReply(io->mdl,id,NULL,NULL);
    }
#endif
    mdlSetComm(io->mdl,1);
}


static int ioUnpackIO(void *vctx, int nSize, void *vBuff)
{
    IO io = vctx;
    PIO *pio = vBuff;
    uint_fast32_t nIO = nSize / sizeof(PIO);
    uint_fast32_t i, d;

    mdlassert(io->mdl,nIO<=io->nExpected);

    for( i=0; i<nIO; i++ ) {
	uint64_t iOrder = pio[i].iOrder;
	int j = iOrder - io->iMinOrder;

	mdlassert(io->mdl,iOrder>=io->iMinOrder);
	mdlassert(io->mdl,iOrder<io->iMaxOrder);

	for( d=0; d<3; d++ ) {
	    io->r[j].v[d] = pio[i].r[d];
	    io->v[j].v[d] = pio[i].v[d];
	}
//	io->m[j] = pio[i].fMass;
//	io->s[j] = pio[i].fSoft;
	io->d[j] = pio[i].fDensity;
	io->p[j] = pio[i].fPot;

    }

    io->nExpected -= nIO;
    return io->nExpected;
}


static void makeName( IO io, char *achOutName, const char *inName )
{
    char *p;

    strcpy( achOutName, inName );
    p = strstr( achOutName, "&I" );
    if ( p ) {
	int n = p - achOutName;
	sprintf( p, "%03d", mdlSelf(io->mdl) );
	strcat( p, inName + n + 2 );
    }
    else {
	p = achOutName + strlen(achOutName);
	sprintf(p,".%03d", mdlSelf(io->mdl));
    }
}

/*
**  Here we actually wait for the data from the Work nodes
*/
void ioStartRecv(IO io,void *vin,int nIn,void *vout,int *pnOut)
{
    struct inStartRecv *recv = vin;
    char achOutName[256];

    mdlassert(io->mdl,sizeof(struct inStartRecv)==nIn);
    io->nExpected = recv->nCount;

    if ( io->nExpected > io->N ) {
	if ( io->N ) {
	    free(io->p);
	    free(io->d);
//	    free(io->s);
//	    free(io->m);
	    free(io->v);
	    free(io->r);
	}
	io->N = io->nExpected;
	io->r = malloc(io->N*sizeof(ioV3));
	io->v = malloc(io->N*sizeof(ioV3));
	//io->m = malloc(io->N*sizeof(FLOAT));
	//io->s = malloc(io->N*sizeof(FLOAT));
	io->d = malloc(io->N*sizeof(float));
	io->p = malloc(io->N*sizeof(float));
    }

    io->iMinOrder = recv->iIndex;
    io->iMaxOrder = recv->iIndex + recv->nCount;

    mdlSetComm(io->mdl,1); /* Talk to the work process */
    mdlRecv(io->mdl,-1,ioUnpackIO,io);
    mdlSetComm(io->mdl,0);

    makeName( io, achOutName, recv->achOutName );

    ioSave(io, achOutName, recv->dTime, recv->dEcosmo,
	   recv->dTimeOld, recv->dUOld,
	   recv->bCheckpoint ? IOHDF5_DOUBLE : IOHDF5_SINGLE );
}

#ifdef USE_PNG
void wrbb( int *cmap, gdImagePtr im ) {
    double slope, offset;
    int i;

    // Shamefully stolen from Tipsy

    slope = 255./20. ;
    for(i = 0 ;i < 21 ;i++){
	cmap[i] = gdImageColorAllocate(im,0,0,(int)(slope * (double)(i) + .5)) ;
    }
    slope = 191./20. ;
    offset = 64. - slope * 21. ;
    for(i = 21 ;i < 42 ;i++){
	cmap[i] = gdImageColorAllocate(im,(int)(slope*(double)(i) + offset + .5),0,255);
    }
    slope = -205./21. ;
    offset = 255. - slope * 41. ;
    for(i = 42 ;i < 63 ;i++){
	cmap[i] = gdImageColorAllocate(im, 255,0,(int)(slope*(double)(i) + offset + .5));
    }
    slope = 205./40. ;
    offset = 50. - slope * 63. ;
    for(i = 63 ;i < 104 ;i++){
	cmap[i] = gdImageColorAllocate(im,255,(int)(slope*(double)(i) + offset + .5),0);
    }
    slope = 255./21. ;
    offset = -slope * 103. ;
    for(i = 104 ;i < 125 ;i++){
	cmap[i] = gdImageColorAllocate(im,255,255,(int)(slope*(double)(i) + offset +.5));
    }

    // The MARK color
    cmap[125] = gdImageColorAllocate(im,255,255,255);
    cmap[126] = gdBrushed;
}

static const int BWIDTH = 2; //!< Width of the brush

static const writePNG( IO io, struct inMakePNG *make, float *limg )
{
    char achOutName[256];
    FILE *png;
    float v, color_slope;
    gdImagePtr ic, brush;
    int cmap[128];
    uint_fast32_t R, i;
    int x, y;

    R = make->iResolution;
    color_slope = 124.0 / (make->maxValue - make->minValue);

    brush = gdImageCreate(BWIDTH,BWIDTH);
    gdImageFilledRectangle(brush,0,0,BWIDTH-1,BWIDTH-1,
			   gdImageColorAllocate(brush,0,255,0));
    ic = gdImageCreate(R,R);
    wrbb(cmap,ic);

    for( x=0; x<R; x++ ) {
	for( y=0; y<R; y++ ) {
	    int clr;
	    v = limg[x+R*y];
	    v = color_slope * ( v - make->minValue ) + 0.5;
	    clr = (int)(v);
	    if ( clr < 0 ) clr = 0;
	    if ( clr > 124 ) clr = 124;

	    assert( clr >= 0 && clr < 125 );
	    gdImageSetPixel(ic,x,y,cmap[clr]);
	}
    }


    makeName( io, achOutName, make->achOutName );
    strcat( achOutName, ".png" );
    png = fopen( achOutName, "wb" );
    assert( png != NULL );
    gdImagePng( ic, png );
    fclose(png);

    gdImageDestroy(ic);
    gdImageDestroy(brush);
}

void ioMakePNG(IO io,void *vin,int nIn,void *vout,int *pnOut)
{
    struct inMakePNG *make = vin;
    float *limg;//, *img;
    uint_fast32_t R, N, i;
    int x, y;

    R = make->iResolution;
    N = R * R;

    //img = malloc( N*sizeof(float) );
    limg = malloc( N*sizeof(float) );
    assert( limg != NULL );


    for( i=0; i<N; i++ ) limg[i] = EPSILON;

    // Project the density onto a grid and find the maximum (ala Tipsy)
    for( i=0; i<io->N; i++ ) {
	x = (io->r[i].v[0]+0.5) * R;
	y = (io->r[i].v[1]+0.5) * R;
	assert( x>=0 && x<R && y>=0 && y<R );
	if ( io->d[i] > limg[x+R*y] )
	    limg[x+R*y] = io->d[i];
    }

    if ( mdlSelf(io->mdl) == 0 ) {
	mdlReduce(io->mdl,MPI_IN_PLACE,limg,N,MPI_FLOAT,MPI_MAX,0);
	writePNG(io,make,limg);
    }
    else {
	mdlReduce(io->mdl,limg,0,N,MPI_FLOAT,MPI_MAX,0);
    }

    free(limg);
    //free(img);
}
#endif
