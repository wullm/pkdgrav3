#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
const char *ilp_module_id = "$Id$";

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ilp.h"

/*
** Private: Create a new tile
*/
static ILPTILE newTile(ILPTILE prev) {
    ILPTILE tile = SIMD_malloc(sizeof(struct ilpTile));
    assert( tile != NULL );
    assert(ILP_PART_PER_TILE%4 == 0 );

    tile->next = NULL;
    tile->prev = prev;
    tile->nMaxPart = ILP_PART_PER_TILE;
    tile->nPart = 0;
    return tile;
    }

/*
** If the current tile is full (nPart == nMaxPart), then
** this function is called to get a new, empty tile.
*/
ILPTILE ilpExtend(ILP ilp) {
    assert( ilp != NULL );
    assert( ilp->tile != NULL );
    assert( ilp->first != NULL );
    assert( ilp->tile->nPart == ilp->tile->nMaxPart );

    ilp->nPrevious += ilp->tile->nPart;

    /* Use the next tile if it exists, or create a new one */
    if ( ilp->tile->next != NULL ) {
	ilp->tile = ilp->tile->next;
	ilp->tile->nPart = 0;
	}
    else {
	ilp->tile = ilp->tile->next = newTile(ilp->tile);
	}

    return ilp->tile;
    }

/*
** Empty the list of particles (go back to the first tile)
*/
ILPTILE ilpClear(ILP ilp) {
    assert( ilp != NULL );
    ilp->tile = ilp->first;
    ilp->nPrevious = 0;
    assert( ilp->tile != NULL );
    ilp->tile->nPart = 0;
    return ilp->tile;
    }

void ilpInitialize(ILP *ilp) {
    *ilp = malloc(sizeof(struct ilpContext));
    assert( *ilp != NULL );
    (*ilp)->first = (*ilp)->tile = newTile(NULL);
    (*ilp)->nPrevious = 0;
    }

void ilpFinish(ILP ilp) {
    ILPTILE tile, next;

    assert( ilp != NULL );

    /* Free all allocated tiles first */
    for ( tile=ilp->first; tile!=NULL; tile=next ) {
	next = tile->next;
	SIMD_free(tile);
	}

    free(ilp);
    }

static float Swap(ILPTILE t1, size_t i1, ILPTILE t2, size_t i2) {
    float v;

    v = t1->s.m.f[i1];
    t1->s.m.f[i1] = t2->s.m.f[i2];
    t2->s.m.f[i2] = v;

    v = t1->s.d2.f[i1];
    t1->s.d2.f[i1] = t2->s.d2.f[i2];
    t2->s.d2.f[i2] = v;

    v = t1->s.fourh2.f[i1];
    t1->s.fourh2.f[i1] = t2->s.fourh2.f[i2];
    t2->s.fourh2.f[i2] = v;

    return v;
    }

float ilpSelect(ILP ilp,uint32_t n, float *rMax) {
    ILPTILE  tile, last, lower, upper;
    size_t tile_i, last_i, lower_i, upper_i, m;
    size_t i, j;
    float v;
    float cmp;

    assert( n > 0 );

#ifdef USE_SIMD_PP
    ILP_LOOP(ilp,tile) {
	uint32_t n = (tile->nPart+ILP_ALIGN_MASK) >> ILP_ALIGN_BITS;
	for ( j=0; j<n; j++ ) {
	    tile->s.d2.p[j] = tile->d.d2.p[j];
	    tile->s.m.p[j] = tile->d.m.p[j];
	    tile->s.fourh2.p[j] = tile->d.fourh2.p[j];
	    }
	}
#else
    ILP_LOOP(ilp,tile) {
	for ( j=0; j<tile->nPart; j++ ) {
	    tile->s.d2.f[j] = tile->d.d2.f[j];
	    tile->s.m.f[j] = tile->d.m.f[j];
	    tile->s.fourh2.f[j] = tile->d.fourh2.f[j];
	    }
	}
#endif

    /* If we have less than n particles, then there isn't any sense in partitioning. */
    if ( ilpCount(ilp) <= n ) {
	v = 0.0;
	i = 0;
	for (j=0;j<ilp->first->nPart;j++)
	    if ( ilp->first->s.d2.f[j] > v )
		v = ilp->first->s.d2.f[(i=j)];
	Swap(ilp->first,i,ilp->first,ilp->first->nPart-1);
	return v;
	}

    tile = lower = ilp->first;
    tile_i = lower_i = 0;
    last = upper = ilp->tile;
    last_i = upper_i = last->nPart-1;

    cmp = *rMax;
    //m = tile->nPart <= n*2 ? tile->nPart-1 : n*2;
    //if ( last == ilp->first && m > last_i ) m = last_i;
    //cmp = tile->s.d2.f[m];
    for (;;) {
	for (;;) { /* Partition loop */
	    for (;;) { /* Find a large value */
		m = (tile != last) ? tile->nPart-1 : last_i;
		while (tile_i<=m && tile->s.d2.f[tile_i] < cmp )
		    tile_i++;
		if ( tile != last && tile_i == tile->nPart ) {
		    tile = tile->next;
		    tile_i = 0;
		    }
		else break;
		}
	    for (;;) { /* Find a small value */
		m = ( last != tile ) ? 0 : tile_i;
		while (last_i > m && last->s.d2.f[last_i] > cmp )
		    --last_i;
		if ( last != tile && last_i == 0 && last->s.d2.f[0] > cmp ) {
		    last = last->prev;
		    last_i = last->nPart-1;
		    }
		else
		    break;
		}

	    /* Swap the large and small values */
	    if ( tile!=last || tile_i<last_i ) {
		Swap(last,last_i,tile,tile_i);
		if ( tile != last || tile_i < last_i ) {
		    if ( ++tile_i == tile->nPart ) {
			tile = tile->next;
			tile_i = 0;
			}
		    if ( tile == last && tile_i == last_i ) break;
		    if ( last_i-- == 0 ) {
			last = last->prev;
			last_i = last->nPart-1;
			}
		    }
		}
	    else break;
	    }

	/* Too many in the first partition */
	if ( tile != ilp->first || tile_i > n ) {
	    upper_i = tile_i;
	    upper = tile;
	    if ( upper_i-- == 0 ) {
		upper = upper->prev;
		upper_i = upper->nPart-1;
		}
	    }
	/* Too few in this partition */
	else if ( tile_i < n ) {
	    lower = tile;
	    lower_i = tile_i;
	    }
	else break;

	if ( upper == lower && upper_i <= lower_i ) break;

	tile = lower;
	tile_i = lower_i;

	last = upper;
	last_i = upper_i;

	m = tile->nPart <= n*2 ? tile->nPart-1 : n*2;
	if ( last == ilp->first && m > last_i ) m = last_i;
	cmp = tile->s.d2.f[m];
	}

    v = 0.0;
    i = 0;
    for (j=0;j<n;j++)
	if ( ilp->first->s.d2.f[j] > v )
	    v = ilp->first->s.d2.f[(i=j)];

    Swap(ilp->first,i,ilp->first,n-1);

#if 0
    // Check
    float mn,mx;
    mn = ilp->first->s.d2.f[0];
    mx = ilp->first->s.d2.f[n];

    ILP_LOOP(ilp,tile) {
	for ( j=0; j<tile->nPart; j++ ) {
	    assert(tile->s.m.f[j] > 0.0);
	    if ( tile == ilp->first && j<n ) {
		if ( tile->s.d2.f[j] > mn ) mn = tile->s.d2.f[j];
		}
	    else {
		if ( tile->s.d2.f[j] < mx ) mx = tile->s.d2.f[j];
		}
	    }
	}
    assert( mn<=mx);
#endif

    /* Estimate a sensible rMax based on this rMax */
    *rMax = v * 1.05;

    return v;
    }


float ilpSelectMass(ILP ilp,uint32_t n, uint32_t N) {
    ILPTILE  tile;
    size_t lower_i, upper_i, tile_i, last_i;
    size_t i, j, m;
    float v, cmp;

    assert( n > 0 );
    if ( N > ilpCount(ilp) ) N = ilpCount(ilp);

    tile = ilp->first;
    assert( N <= tile->nPart );

    /*
    ** If we have less than n particles, then there isn't any sense in partitioning.
    ** In this case, we fine the largest mass and return the corresponding softening.
    */
    if ( N <= n ) {
	v = tile->s.m.f[0];
	i = 0;
	for (j=0;j<N;j++)
	    if ( tile->s.m.f[j] > v )
		v = tile->s.m.f[(i=j)];
	return Swap(tile,i,tile,N-1);
	}

    tile_i = lower_i = 0;
    last_i = upper_i = N-1;

    m = N <= n*2 ? N-1 : n*2;
    if ( m > last_i ) m = last_i;
    cmp = tile->s.m.f[m];

    for (;;) {
	for (;;) { /* Partition loop */
	    while (tile_i<=last_i && tile->s.m.f[tile_i] < cmp )
		tile_i++;
	    while (last_i > tile_i && tile->s.m.f[last_i] > cmp )
		--last_i;

	    /* Swap the large and small values */
	    if ( tile_i<last_i ) {
		Swap(tile,last_i,tile,tile_i);
		tile_i++;
		if ( tile_i == last_i ) break;
		last_i--;
		}
	    else break;
	    }

	if ( tile_i >= n ) upper_i = tile_i-1;   /* Too many in the first partition */
	else if ( tile_i < n ) lower_i = tile_i; /* Too few in this partition */
	else break;                              /* Exactly the right number */

	if ( upper_i <= lower_i ) break;

	tile_i = lower_i;
	last_i = upper_i;

	m = N <= n*2 ? N-1 : n*2;
	if ( m > last_i ) m = last_i;
	cmp = tile->s.m.f[m];
	}

    v = tile->s.m.f[0];
    i = 0;
    for (j=0;j<n;j++)
	if ( tile->s.m.f[j] > v )
	    v = tile->s.m.f[(i=j)];

    v = Swap(tile,i,tile,n-1);

/* Turn this off for normal operation */
#if 0
    // Check
    float mn,mx;
    mn = tile->s.m.f[0];
    mx = tile->s.m.f[n];
    for ( j=0; j<N; j++ ) {
	if ( j<n ) {
	    if ( tile->s.m.f[j] > mn ) mn = tile->s.m.f[j];
	    }
	else {
	    if ( tile->s.m.f[j] < mx ) mx = tile->s.m.f[j];
	    }
	}
    assert( mn<=mx);
#endif

    return v;
    }
