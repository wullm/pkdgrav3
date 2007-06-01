/******************************************************************************
 *  iohdf5.c - Read and Write particle data in HDF5 format.
 *
 *  - Call H5Fcreate or H5Fopen to create or open an HDF5 file
 *  - Call ioHDF5Initialize
 *
 *  Write:
 *    - Call ioHDF5AddDark (or Gas or Star) for each particle
 *  Read:
 *    - Call ioHDF5GetDark (or Gas or Star) for each particle
 *
 *  - Call ioHDF5Finish.
 *  - Call H5Fclose
 *
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <malloc.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#define LOCAL_ALLOC(s) alloca(s)
#define LOCAL_FREE(p)
#else
#define LOCAL_ALLOC(s) malloc(s)
#define LOCAL_FREE(p) free(p)
#endif

#include "iohdf5.h"

#define GROUP_PARAMETERS "parameters"
#define GROUP_DARK "dark"
#define GROUP_GAS  "gas"
#define GROUP_STAR "star"

#define FIELD_POSITION "position"
#define FIELD_VELOCITY "velocity"
#define FIELD_ORDER "order"
#define FIELD_CLASS "class"
#define FIELD_CLASSES "classes"

/* Create a data group: dark, gas, star, etc. */
static hid_t CreateGroup( hid_t fileID, const char *groupName ) {
    hid_t groupID;

    groupID = H5Gcreate( fileID, groupName, 0 ); H5assert(groupID);

    return groupID;
}

static hid_t openSet(hid_t fileID, const char *name )
{
    hid_t dataSet;
    dataSet = H5Dopen( fileID, name );
    return dataSet;
}

static hsize_t getSetSize(hid_t setID) {
    hid_t spaceID;
    hsize_t dims[2], maxs[2];

    spaceID = H5Dget_space(setID); H5assert(spaceID);
    assert( H5Sis_simple(spaceID) > 0 );
    assert( H5Sget_simple_extent_ndims(spaceID) <= 3 );
    H5Sget_simple_extent_dims(spaceID,dims,maxs);
    H5Sclose(spaceID);
    return dims[0];
}

/* Create a dataset inside a group (positions, velocities, etc. */
static hid_t newSet(hid_t locID, const char *name, uint64_t chunk,
		    uint64_t count, int nDims, hid_t dataType )
{
    hid_t dataProperties, dataSpace, dataSet;
    hsize_t iDims[2], iMax[2];

    /* Create a dataset property so we can set the chunk size */
    dataProperties = H5Pcreate( H5P_DATASET_CREATE );
    H5assert( dataProperties );
    iDims[0] = chunk;
    iDims[1] = 1;
    H5assert( H5Pset_chunk( dataProperties, nDims>1?2:1, iDims ));

    /* Also request the FLETCHER checksum */
    H5assert( H5Pset_filter(dataProperties,H5Z_FILTER_FLETCHER32,0,0,NULL) );

    /* And the dataspace */
    iDims[0] = count;
    iDims[1] = nDims;
    iMax[0] = H5S_UNLIMITED;
    iMax[1] = nDims;

    dataSpace = H5Screate_simple( nDims>1?2:1, iDims, iMax );
    H5assert( dataSpace );

    /* Create the data set */
    dataSet = H5Dcreate( locID, name, 
                         dataType, dataSpace, dataProperties );
    H5assert( dataSet );
    H5assert( H5Pclose( dataProperties ) );
    H5assert( H5Sclose( dataSpace ) );

    return dataSet;
}

/* Read part of a set from disk into memory */
static void readSet(
    hid_t set_id,           /* set from which to read the data */
    void *pBuffer,          /* Buffer for the data */
    hid_t memType,          /* Data type in memory */
    hsize_t iOffset,        /* Offset into the set */
    hsize_t nRows,          /* Number of rows to read */
    hsize_t nCols )         /* Number of columns (normally 1 or 3) */
{
    hid_t memSpace, diskSpace;
    hsize_t dims[2], start[2];
    dims[0] = nRows;
    dims[1] = nCols;
    memSpace = H5Screate_simple(nCols>1?2:1,dims,0);
    diskSpace = H5Dget_space(set_id);
    start[0] = iOffset;
    start[1] = 0;

    H5Sselect_hyperslab(diskSpace,H5S_SELECT_SET,start,0,dims,0);
    H5Dread(set_id,memType,memSpace,diskSpace,H5P_DEFAULT,pBuffer);
    H5Sclose(memSpace);
    H5Sclose(diskSpace);
}


/* Write part of a set from memory */
static void writeSet(
    hid_t set_id,           /* set into which to write the data */
    const void *pBuffer,    /* Buffer containing the data */
    hid_t memType,          /* Data type in memory */
    hsize_t iOffset,        /* Offset into the set */
    hsize_t nRows,          /* Number of rows to write */
    hsize_t nCols )         /* Number of columns (normally 1 or 3) */
{
    hid_t memSpace, diskSpace;
    hsize_t dims[2], start[2], size[2];

    dims[0] = nRows;
    dims[1] = nCols;
    memSpace = H5Screate_simple(nCols>1?2:1,dims,0);
    size[0] = iOffset + nRows;
    size[1] = nCols;
    H5Dextend(set_id,size);
    diskSpace = H5Dget_space(set_id);
    start[0] = iOffset;
    start[1] = 0;
    H5Sselect_hyperslab(diskSpace,H5S_SELECT_SET,start,0,dims,0);
    H5Dwrite(set_id,memType,memSpace,diskSpace,H5P_DEFAULT,pBuffer);
    H5Sclose(memSpace);
    H5Sclose(diskSpace);
}

/* Writes the fields common to all particles */
static void flushBase( IOHDF5 io, IOBASE *Base,
		       void (*flushExtra)( IOHDF5 io, IOBASE *Base ) )
{
    assert( io != NULL );
    if ( Base->nBuffered ) {
	if ( Base->group_id == H5I_INVALID_HID )
	    Base->group_id = CreateGroup(io->fileID,Base->szGroupName);
	if ( Base->setR_id == H5I_INVALID_HID ) {
	    Base->setR_id = newSet(
		Base->group_id, FIELD_POSITION,
		io->iChunkSize, 0, 3, io->diskFloat );
	    Base->setV_id = newSet(
		Base->group_id, FIELD_VELOCITY,
		io->iChunkSize, 0, 3, io->diskFloat );
	    if ( Base->Order.iOrder != NULL ) {
		assert( sizeof(PINDEX) == 4 );
		Base->Order.setOrder_id = newSet(
		    Base->group_id, FIELD_ORDER,
		    io->iChunkSize, 0, 1, H5T_NATIVE_UINT32);
	    }
	    if ( Base->Class.iClass != NULL ) {
		Base->Class.setClass_id = newSet(
		    Base->group_id, FIELD_CLASS,
		    io->iChunkSize, 0, 1, H5T_NATIVE_UINT8);
	    }
	}

	writeSet( Base->setR_id, Base->R, io->memFloat,
		  Base->iOffset, Base->nBuffered, 3 );
	writeSet( Base->setV_id, Base->V, io->memFloat,
		  Base->iOffset, Base->nBuffered, 3 );

	if ( Base->Order.iOrder != NULL ) {
	    writeSet( Base->Order.setOrder_id, Base->Order.iOrder,
		      H5T_NATIVE_UINT32, Base->iOffset, Base->nBuffered, 1 );
	}
	if ( Base->Class.iClass != NULL ) {
	    writeSet( Base->Class.setClass_id, Base->Class.iClass,
		      H5T_NATIVE_UINT8, Base->iOffset, Base->nBuffered, 1 );
	}

	if ( flushExtra != NULL ) flushExtra(io,Base);
	Base->iOffset += Base->nBuffered;
	Base->nBuffered = 0;
    }
}

/* Write any accumulated dark particles */
static void flushDark( IOHDF5 io )
{
    flushBase( io, &io->darkBase, 0 );
}

/* Write any accumulated gas particles */
static void flushGas( IOHDF5 io )
{
    flushBase( io, &io->gasBase, 0 );
}

/* Write any accumulated star particles */
static void flushStar( IOHDF5 io )
{
    flushBase( io, &io->starBase, 0 );
}

static hid_t makeClassType(hid_t floatType, int bStart) {
    hid_t tid;

    tid = H5Tcreate (H5T_COMPOUND, sizeof(classEntry));
    H5Tinsert(tid,"class",HOFFSET(classEntry,iClass), H5T_NATIVE_UINT8);
    H5Tinsert(tid,"mass", HOFFSET(classEntry,fMass), floatType);
    H5Tinsert(tid,"soft", HOFFSET(classEntry,fSoft), floatType);
    if ( bStart )
	H5Tinsert(tid,"start",HOFFSET(classEntry,iOrderStart), H5T_NATIVE_UINT32);
    return tid;
}

static void writeClassTable(IOHDF5 io, IOBASE *Base ) {
    hid_t tid, set;

    if ( Base->nTotal > 0 ) {
	tid = makeClassType( io->memFloat, Base->Class.iClass==NULL );

	set = newSet(Base->group_id, FIELD_CLASSES,
		     io->iChunkSize, Base->Class.nClasses, 1, tid );

	writeSet( set, Base->Class.class, tid,
		  0, Base->Class.nClasses, 1 );

	H5Dclose(set);
	H5Tclose(tid);
    }
}

static void readClassTable( IOHDF5 io, IOBASE *Base ) {
    hid_t tid, set;

    set = openSet( Base->group_id, FIELD_CLASSES );
    if ( set != H5I_INVALID_HID ) {
	if ( Base->Class.iClass == NULL ) {
	    Base->Class.iClass = malloc( io->iChunkSize * sizeof(uint8_t) );
	    assert(Base->Class.iClass != NULL );
	}
	tid = makeClassType( io->memFloat, Base->Class.iClass==NULL );
	Base->Class.nClasses = getSetSize(set);
	readSet( set, Base->Class.class, tid,
		 0, Base->Class.nClasses, 1 );
	H5Dclose(set);
	H5Tclose(tid);
    }

}



/* Add an attribute to a group */
void ioHDF5WriteAttribute( IOHDF5 io, const char *name,
			   hid_t dataType, void *data ) {
    hid_t dataSpace, attrID;
    herr_t rc;
    hsize_t dims = 1;

    dataSpace = H5Screate_simple( 1, &dims, NULL ); H5assert(dataSpace);
    attrID = H5Acreate( io->parametersID,name,dataType,dataSpace,H5P_DEFAULT );
    H5assert(attrID);
    rc = H5Awrite( attrID, dataType, data ); H5assert(rc);
    H5assert(H5Aclose( attrID ));
    H5assert(H5Sclose(dataSpace));
}




void ioHDF5Flush( IOHDF5 io )
{
    flushDark(io);
    flushGas(io);
    flushStar(io);
    writeClassTable(io,&io->darkBase);
    writeClassTable(io,&io->gasBase);
    writeClassTable(io,&io->starBase);
}


static void baseInitialize( IOHDF5 io, IOBASE *Base,
			    hid_t locID, const char *group)
{
    Base->iOffset = 0;
    Base->iIndex = 0;
    Base->nBuffered = 0;

    Base->group_id = H5Gopen( locID, group );

    /* Oh.  We are reading this file. */
    if ( Base->group_id != H5I_INVALID_HID ) {
	Base->setR_id = openSet(Base->group_id,FIELD_POSITION);
	H5assert(Base->setR_id);
	Base->nTotal = getSetSize(Base->setR_id);
	Base->setV_id = openSet(Base->group_id,FIELD_VELOCITY);
	H5assert(Base->setV_id);
	assert( Base->nTotal == getSetSize(Base->setV_id) );
	Base->Order.setOrder_id = openSet(Base->group_id,FIELD_ORDER);
	Base->Class.setClass_id = openSet(Base->group_id,FIELD_CLASS);
	if ( Base->Class.setClass_id != H5I_INVALID_HID ) {
	    readClassTable( io, Base );
	}
    }

    /* No group: we have to create this later.  We delay until later because
       it is possible that we won't have any of this type of particle. */
    else {
	/*Base->group_id = H5I_INVALID_HID;*/
	Base->setR_id = Base->setV_id = H5I_INVALID_HID;
	Base->Order.setOrder_id = H5I_INVALID_HID;
	Base->Class.setClass_id = H5I_INVALID_HID;
    }

	Base->Order.iStart = Base->Order.iNext = 0;
	Base->Order.iOrder = NULL;
	Base->R = Base->V = NULL;
	Base->Class.fMass = Base->Class.fSoft = NULL;
	Base->Class.iClass = NULL;
	Base->Class.nClasses=0;

    assert( strlen(group) < sizeof(Base->szGroupName) );
    strcpy( Base->szGroupName, group );

}

IOHDF5 ioHDF5Initialize( hid_t fileID, hid_t iChunkSize, int bSingle )
{
    IOHDF5 io;
    H5E_auto_t save_func;
    void *     save_data;

    io = malloc( sizeof(struct ioHDF5) ); assert(io!=NULL);
    io->fileID = fileID;
    io->iChunkSize = iChunkSize;

    /* This is the native type of FLOAT values - normally double */
    io->memFloat = sizeof(FLOAT)==sizeof(float)
	? H5T_NATIVE_FLOAT : H5T_NATIVE_DOUBLE;
    io->diskFloat = bSingle ? H5T_NATIVE_FLOAT : io->memFloat;

    /* The group might already exist */
    H5Eget_auto(&save_func,&save_data);
    H5Eset_auto(0,0);

    io->parametersID = H5Gopen( fileID, GROUP_PARAMETERS );
    if ( io->parametersID == H5I_INVALID_HID ) {
	io->parametersID = CreateGroup( fileID, GROUP_PARAMETERS );
    }

    baseInitialize( io, &io->darkBase, fileID, GROUP_DARK );
    baseInitialize( io, &io->gasBase,  fileID, GROUP_GAS  );
    baseInitialize( io, &io->starBase, fileID, GROUP_STAR );

    H5Eset_auto(save_func,save_data);

    return io;
}

static void baseFinish( IOBASE *Base )
{
    if ( Base->group_id != H5I_INVALID_HID ) H5Gclose(Base->group_id);
    if ( Base->setR_id != H5I_INVALID_HID ) H5Dclose(Base->setR_id);
    if ( Base->setV_id != H5I_INVALID_HID ) H5Dclose(Base->setV_id);
    if ( Base->Order.setOrder_id != H5I_INVALID_HID )
	H5Dclose(Base->Order.setOrder_id);
    if ( Base->Class.setClass_id != H5I_INVALID_HID )
	H5Dclose(Base->Class.setClass_id);

    if ( Base->Order.iOrder != NULL ) free( Base->Order.iOrder );
    if ( Base->R != NULL ) free( Base->R );
    if ( Base->V != NULL ) free( Base->V );
    if ( Base->Class.fMass != NULL ) free( Base->Class.fMass );
    if ( Base->Class.fSoft != NULL ) free( Base->Class.fSoft );
}

void ioHDF5Finish( IOHDF5 io )
{
    assert( io != NULL );

    ioHDF5Flush(io);

    baseFinish( &io->darkBase );
    baseFinish( &io->gasBase );
    baseFinish( &io->starBase );

    free( io );
}


/* Create the class set if it doesn't already exist and flush out the table. */
static void createClass(IOHDF5 io, IOBASE *Base)
{
    int i, j, n;
    IOCLASS *Class = &Base->Class;

    /* We already created the set */
    if ( Class->iClass != NULL ) return;

    Class->iClass = malloc( io->iChunkSize * sizeof(uint8_t) );

    /* If the group exists, we will have to write */
    if ( Class->setClass_id == H5I_INVALID_HID 
	 && Base->group_id != H5I_INVALID_HID ) {
	Class->setClass_id = newSet(
	    Base->group_id, FIELD_CLASS,
	    io->iChunkSize, 0, 1, H5T_NATIVE_UINT8);
    }

    for( i=n=0; i<Class->nClasses; i++ ) {
	int s, e;

	s = Class->class[i].iOrderStart;
	e = i==Class->nClasses-1 ? Base->nTotal : Class->class[i+1].iOrderStart;

	for( j=s; j<e; j++ ) {
	    Class->iClass[n++] = i;
	}
    }

//    writeSet( Class->setClass_id, Class->iClass,
//	      H5T_NATIVE_ULONG,iOffset,n,1);


}



static void addClass( IOHDF5 io, IOBASE *Base,
		      PINDEX iOrder, FLOAT fMass, FLOAT fSoft )
{
    IOCLASS *Class = &Base->Class;
    int i;

    /* See if we already have this class: Mass/Softening pair */
    for( i=0; i<Class->nClasses; i++ ) {
	if ( Class->class[i].fMass == fMass && Class->class[i].fSoft == fSoft )
	    break;
    }

    /* Case 1: This is a new class */
    if ( i == Class->nClasses ) {
	assert( Class->nClasses < 256 ); /*TODO: handle this case */
	Class->class[i].iClass = i;
	Class->class[i].iOrderStart = iOrder;
	Class->class[i].fMass = fMass;
	Class->class[i].fSoft = fSoft;
	Class->nClasses++;
	if ( Class->iClass != NULL )
	    Class->iClass[Base->nBuffered] = i;
    }

    /* Case 2: This was the last class, and we might be compressing */
    else if ( i == Class->nClasses - 1 && Class->iClass==NULL ) {
    }

    /* Case 3: A match, but a prior class */
    else {
	createClass(io,Base);
	Class->iClass[Base->nBuffered] = i;
    }
}

static void addOrder( IOHDF5 io, IOBASE *Base,
		      PINDEX iOrder )
{
    IOORDER *Order = &Base->Order;
    int i, n;

    /* If we still think that the particles are in order */
    if ( Order->iOrder==NULL ) {
	if ( Base->nTotal == 1 )
	    Order->iStart = Order->iNext = iOrder;
	/* So far, so good.  Bump up the "next expected" value */
	if ( iOrder == Order->iNext )
	    Order->iNext++;
	/* Darn...  we have out of order particles, so write the array as
	   it should be up to this point. */
	else {
	    int iOffset = 0;
	    Order->iOrder = malloc( io->iChunkSize * sizeof(PINDEX) );

	    if ( Order->setOrder_id == H5I_INVALID_HID 
		&& Base->group_id != H5I_INVALID_HID ) {
		assert( sizeof(PINDEX) == 4 );
		Order->setOrder_id = newSet(
		    Base->group_id, FIELD_ORDER,
		    io->iChunkSize, 0, 1, H5T_NATIVE_UINT32);
	    }

	    n = 0;
	    for( i=Order->iStart; i<Order->iNext; i++ ) {
		Order->iOrder[n++] = i;
		if ( n == io->iChunkSize ) {
		    writeSet( Order->setOrder_id, Order->iOrder,
			      H5T_NATIVE_UINT32,iOffset,n,1);
		    iOffset += n;
		    n = 0;
		}
	    }
	    assert( n == Base->nBuffered );
	    Order->iOrder[Base->nBuffered] = iOrder;
	}
    }

    /* Particles are out of order for sure: just buffer iOrder for writing */
    else {
	Order->iOrder[Base->nBuffered] = iOrder;
    }
}

/* If the structures have not been allocated, do so now */
static void allocateBase( IOHDF5 io, IOBASE *Base )
{
    if ( Base->R == NULL ) {
	Base->R    = malloc( io->iChunkSize * sizeof(ioV3) );
	assert( Base->R != NULL );
	Base->V    = malloc( io->iChunkSize * sizeof(ioV3) );
	assert( Base->V != NULL );
	//Base->Mass = malloc( io->iChunkSize * sizeof(FLOAT) );
	//assert( Base->Mass != NULL );
	//Base->Soft = malloc( io->iChunkSize * sizeof(FLOAT) );
	//assert( Base->Soft != NULL );
    }
}


static int getBase( IOHDF5 io, IOBASE *Base, PINDEX *iOrder,
		    void (*flush)(IOHDF5 io),
		    FLOAT *r, FLOAT *v,
		    FLOAT *fMass, FLOAT *fSoft )
{
    assert(Base->setR_id!=H5I_INVALID_HID);
    assert(Base->setV_id!=H5I_INVALID_HID);
    allocateBase(io,Base);

    /* If we have to read more from the file */
    if ( Base->nBuffered == Base->iIndex ) {
	Base->iOffset += Base->iIndex;
	Base->iIndex = Base->nBuffered = 0;
	if ( Base->iOffset >= Base->nTotal ) return 0;

	Base->nBuffered = Base->nTotal - Base->iOffset;
	if ( Base->nBuffered > io->iChunkSize )
	    Base->nBuffered = io->iChunkSize;

	readSet( Base->setR_id, Base->R, io->memFloat,
		 Base->iOffset, Base->nBuffered, 3 );
	readSet( Base->setV_id, Base->V, io->memFloat,
		 Base->iOffset, Base->nBuffered, 3 );

	if ( Base->Order.setOrder_id != H5I_INVALID_HID ) {
	    if ( Base->Order.iOrder == NULL ) {
		Base->Order.iOrder = malloc( io->iChunkSize * sizeof(PINDEX) );
	    }
	    readSet( Base->Order.setOrder_id, Base->Order.iOrder,
		     H5T_NATIVE_UINT32,
		     Base->iOffset, Base->nBuffered, 1 );
	}
	if ( Base->Class.setClass_id != H5I_INVALID_HID ) {
	    if ( Base->Class.iClass == NULL ) {
		Base->Class.iClass=malloc( io->iChunkSize * sizeof(uint8_t) );
	    }
	    readSet( Base->Class.setClass_id, Base->Class.iClass,
		     H5T_NATIVE_UINT8, Base->iOffset, Base->nBuffered, 1 );
	}
    }
    *iOrder = Base->iOffset + Base->iIndex; /*FIXME: */
    r[0] = Base->R[Base->iIndex].v[0];
    r[1] = Base->R[Base->iIndex].v[1];
    r[2] = Base->R[Base->iIndex].v[2];
    v[0] = Base->V[Base->iIndex].v[0];
    v[1] = Base->V[Base->iIndex].v[1];
    v[2] = Base->V[Base->iIndex].v[2];
    *fMass = 0.0;
    *fSoft = 0.0;

    Base->iIndex++;
    return 1;
}

static void addBase( IOHDF5 io, IOBASE *Base, PINDEX iOrder,
		     void (*flush)(IOHDF5 io),
		     const FLOAT *r, const FLOAT *v,
		     FLOAT fMass, FLOAT fSoft )
{
    int i;

    assert( io != NULL );
    assert( Base != NULL );

    Base->nTotal++;

    allocateBase(io,Base);

    /* If the particles are not in order, then we have to store iOrder */
    addOrder( io, Base, iOrder );

    for( i=0; i<3; i++ ) {
	Base->R[Base->nBuffered].v[i] = r[i];
	Base->V[Base->nBuffered].v[i] = v[i];
    }
    addClass( io, Base, iOrder, fMass, fSoft );

    if ( ++Base->nBuffered == io->iChunkSize )
	(*flush)(io);
}

static void seekBase( IOHDF5 io, IOBASE *Base, PINDEX Offset ) {
    /*TODO: (optional) seek to chunk boundary */
    Base->nBuffered = Base->iIndex = 0;
    Base->iOffset = Offset;
}

void ioHDF5AddDark( IOHDF5 io, PINDEX iOrder,
		    const FLOAT *r, const FLOAT *v,
		    FLOAT fMass, FLOAT fSoft, FLOAT fPot )
{
    addBase( io, &io->darkBase, iOrder, flushDark,
	     r, v, fMass, fSoft );
    /*TODO: Save Potential as well */
}

int  ioHDF5GetDark( IOHDF5 io, PINDEX *iOrder,
		    FLOAT *r, FLOAT *v,
		    FLOAT *fMass, FLOAT *fSoft, FLOAT *fPot )
{
    return getBase( io, &io->darkBase, iOrder, flushDark,
		    r, v, fMass, fSoft );
    *fPot = 0.0;
    /*TODO: Load Potential as well */
}

void ioHDF5SeekDark( IOHDF5 io, PINDEX Offset ) {
    seekBase(io,&io->darkBase,Offset);
}

void ioHDF5AddGas(IOHDF5 io, PINDEX iOrder,
		  const FLOAT *r, const FLOAT *v,
		  FLOAT fMass, FLOAT fSoft, FLOAT fPot,
		  FLOAT fTemp, FLOAT fMetals)
{
    addBase( io, &io->gasBase, iOrder, flushGas,
	     r, v, fMass, fSoft );
    /*TODO: Save fPot, fTemp, fMetals */
}

int ioHDF5GetGas(IOHDF5 io, PINDEX *iOrder,
		  FLOAT *r, FLOAT *v,
		  FLOAT *fMass, FLOAT *fSoft, FLOAT *fPot,
		  FLOAT *fTemp, FLOAT *fMetals)
{
    return getBase( io, &io->gasBase, iOrder, flushGas,
		    r, v, fMass, fSoft );
    *fPot = *fTemp = *fMetals = 0.0;
    /*TODO: Load fPot, fTemp, fMetals */
}

void ioHDF5SeekGas( IOHDF5 io, PINDEX Offset ) {
    seekBase(io,&io->gasBase,Offset);
}

void ioHDF5AddStar(IOHDF5 io, PINDEX iOrder,
	       const FLOAT *r, const FLOAT *v,
	       FLOAT fMass, FLOAT fSoft, FLOAT fPot,
	       FLOAT fMetals, FLOAT fTForm)
{
    addBase( io, &io->starBase, iOrder, flushStar,
	     r, v, fMass, fSoft );
    /*TODO: Save fPot, fMetals, fTForm */
}

int ioHDF5GetStar( IOHDF5 io, PINDEX *iOrder,
		   FLOAT *r, FLOAT *v,
		   FLOAT *fMass, FLOAT *fSoft, FLOAT *fPot,
		   FLOAT *fMetals, FLOAT *fTForm)
{
    return getBase( io, &io->starBase, iOrder, flushStar,
	     r, v, fMass, fSoft );
    *fPot = *fMetals = *fTForm = 0.0;
    /*TODO: Load fPot, fMetals, fTForm */
}

void ioHDF5SeekStar( IOHDF5 io, PINDEX Offset ) {
    seekBase(io,&io->starBase,Offset);
}