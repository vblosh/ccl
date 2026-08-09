/*
The bloom filter uses k hash functions to store k bits in a bit string that
represent a stored value. It can return false positives, but if it says that
an element is NOT there, it is definitely not there.

The size of the bloom filter and the number of hash functions you should be
using depending on your application can be calculated using the formulas on
the Wikipedia page:

m = -n*ln(p)/(ln(2)^2)

This will tell you the number of bits m to use for your filter, given the
number n of elements in your filter and the false positive probability p you
want to achieve. All that for the ideal number of hash functions k which you
can calculate like this:

k = 0.7*m/n

*/
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "containers.h"
struct tagBloomFilter {
	size_t count; /* Elements stored already */
	size_t MaxNbOfElements;
	size_t HashFunctions;
	size_t nbOfBits;
	ContainerAllocator *Allocator;
	unsigned char *bits;
	unsigned Seeds[1];
};

/*-----------------------------------------------------------------------------
   MurmurHash2, by Austin Appleby
   Note - This code makes a few assumptions about how your machine behaves -
   1. We can read a 4-byte value from any address without crashing
   2. sizeof(int) == 4
   And it has a few limitations -
   1. It will not work incrementally.
   2. It will not produce the same results on little-endian and big-endian
      machines.
*/
static size_t Hash(const void * key, size_t len, unsigned int seed )
{
	/* 'm' and 'r' are mixing constants generated offline.
	   They're not really 'magic', they just happen to work well.
	*/

	const unsigned int m = 0x5bd1e995;
	const int r = 24;
	/* Initialize the hash to a 'random' value */
	size_t h = seed ^ len;
	/* Mix 4 bytes at a time into the hash */
	const unsigned char * data = key;
	while(len >= 4)	{
		uint32_t k;

		/* MurmurHash2 specifies little-endian words.  Assemble the word
		   explicitly so that arbitrary (including unaligned) byte strings
		   are valid input on every target. */
		k = ((uint32_t)data[0]) |
		    ((uint32_t)data[1] << 8) |
		    ((uint32_t)data[2] << 16) |
		    ((uint32_t)data[3] << 24);

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;	len -= 4;
	}
	/* Handle the last few bytes of the input array */
	switch(len)
	{
	case 3: h ^= (size_t)data[2] << 16;
	case 2: h ^= (size_t)data[1] << 8;
	case 1: h ^= (size_t)data[0];
	        h *= m;
	};
	/* Do a few final mixes of the hash to ensure the last few
	   bytes are well-incorporated. */
	h ^= h >> 13;	h *= m; h ^= h >> 15;
	return h;
}
#define LOG_2_SQUARED 0.4804530139182014246671025263266214588214L

enum BloomDimensionStatus {
	BLOOM_DIMENSIONS_OK,
	BLOOM_DIMENSIONS_BADARG,
	BLOOM_DIMENSIONS_OVERFLOW
};

/* Compute all dimensions and the exact total allocation size in one place.
   Keeping Create and CalculateSpace on the same path prevents the two public
   operations from disagreeing about the seed tail or bitset size. */
static enum BloomDimensionStatus CalculateDimensions(size_t nbOfElements,
																						 double Probability,
																						 size_t *nbOfBits,
																						 size_t *hashFunctions,
																						 size_t *space)
{
	long double bits_value;
	long double hashes_value;
	size_t bit_bytes;
	size_t seed_bytes;
	size_t object_bytes;
	size_t total_bytes;

	if (nbOfElements == 0 || !isfinite(Probability) ||
		Probability <= 0.0 || Probability >= 1.0)
		return BLOOM_DIMENSIONS_BADARG;

	/* Use long double for the intermediate product.  In particular, do not
	   multiply in size_t or double and then cast an overflowing result. */
	bits_value = roundl(-(long double)nbOfElements *
																 logl((long double)Probability) /
																 LOG_2_SQUARED);
	if (!isfinite(bits_value) || bits_value >= (long double)SIZE_MAX)
		return BLOOM_DIMENSIONS_OVERFLOW;
	if (bits_value < 1.0L)
		bits_value = 1.0L;
	*nbOfBits = (size_t)bits_value;

	hashes_value = roundl(0.7L * (long double)*nbOfBits /
																(long double)nbOfElements);
	if (!isfinite(hashes_value) || hashes_value >= (long double)SIZE_MAX)
		return BLOOM_DIMENSIONS_OVERFLOW;
	if (hashes_value < 1.0L)
		hashes_value = 1.0L;
	*hashFunctions = (size_t)hashes_value;

	/* ceil(nbOfBits / 8), written without nbOfBits + 7 overflow. */
	bit_bytes = *nbOfBits / 8;
	if (*nbOfBits % 8 != 0)
		++bit_bytes;

	/* Seeds[1] is part of sizeof(BloomFilter); allocate only the tail. */
	seed_bytes = 0;
	if (*hashFunctions > 1) {
		if (*hashFunctions - 1 >
																(SIZE_MAX - sizeof(BloomFilter)) /
																 sizeof(unsigned))
			return BLOOM_DIMENSIONS_OVERFLOW;
		seed_bytes = (*hashFunctions - 1) * sizeof(unsigned);
	}
	object_bytes = sizeof(BloomFilter) + seed_bytes;
	if (bit_bytes > SIZE_MAX - object_bytes)
		return BLOOM_DIMENSIONS_OVERFLOW;
	total_bytes = object_bytes + bit_bytes;
	if (space != NULL)
		*space = total_bytes;
	return BLOOM_DIMENSIONS_OK;
}

static size_t CalculateSpace(size_t nbOfElements,double Probability)
{
	size_t nbOfBits;
	size_t hashFunctions;
	size_t result;
	enum BloomDimensionStatus status;

	status = CalculateDimensions(nbOfElements, Probability, &nbOfBits,
																		 &hashFunctions, &result);
	(void)nbOfBits;
	(void)hashFunctions;
	if (status == BLOOM_DIMENSIONS_BADARG) {
		iError.RaiseError("BloomFilter.CalculateSpace",CONTAINER_ERROR_BADARG);
		return 0;
	}
	if (status == BLOOM_DIMENSIONS_OVERFLOW) {
		iError.RaiseError("BloomFilter.CalculateSpace",CONTAINER_ERROR_NOMEMORY);
		return 0;
	}
	return result;
}

static BloomFilter *Create(size_t nbOfElements,double Probability)
{
	size_t nbOfBits;
	size_t hashFunctions;
	size_t object_bytes;
	size_t bit_bytes;
	size_t space;
	enum BloomDimensionStatus status;
	ContainerAllocator *allocator;
	BloomFilter *result;

	status = CalculateDimensions(nbOfElements, Probability, &nbOfBits,
																		 &hashFunctions, &space);
	if (status == BLOOM_DIMENSIONS_BADARG) {
		iError.RaiseError("BloomFilter.Create", CONTAINER_ERROR_BADARG);
		return NULL;
	}
	if (status == BLOOM_DIMENSIONS_OVERFLOW) {
		iError.RaiseError("BloomFilter.Create", CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	bit_bytes = nbOfBits / 8 + (nbOfBits % 8 != 0);
	object_bytes = space - bit_bytes;
	allocator = CurrentAllocator;
	result = allocator->malloc(object_bytes);
	if (result == NULL) {
		iError.RaiseError("BloomFilter.Create",CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	memset(result,0,sizeof(*result));
	result->bits = allocator->malloc(bit_bytes);
	if (result->bits == NULL) {
		allocator->free(result);
		iError.RaiseError("BloomFilter.Create",CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	memset(result->bits,0,bit_bytes);
	result->nbOfBits = nbOfBits;
	result->MaxNbOfElements = nbOfElements;
	result->HashFunctions = hashFunctions;
	while (hashFunctions > 0) {
		hashFunctions--;
		result->Seeds[hashFunctions] = rand();
	}
	result->Allocator = allocator;
	return result;
}

static size_t Add(BloomFilter *b, const void *key, size_t keylen)
{
	size_t hash;
	size_t i;

	if (b == NULL || key == NULL || keylen == 0) {
		iError.RaiseError("BloomFilter.Add", CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	if (b->MaxNbOfElements <= b->count) {
		iError.RaiseError("BloomFilter.Add",CONTAINER_FULL);
		return 0;
	}
	for (i=0; i<b->HashFunctions;i++) {
		hash = Hash(key,keylen,b->Seeds[i]);
		hash %= b->nbOfBits;
		b->bits[hash >> 3] |= 1 << (hash&7);
	}
	return ++b->count;
}

static int Find(BloomFilter *b, const void *key, size_t keylen)
{
	size_t hash;
	size_t i;

	if (b == NULL || key== NULL || keylen == 0) {
		iError.RaiseError("iBloomFilter.Find",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	for (i=0; i<b->HashFunctions;i++) {
		hash = Hash(key,keylen,b->Seeds[i]);
		hash %= b->nbOfBits;
		if ((b->bits[hash >> 3] & (1 << (hash&7))) == 0)
			return 0;
	}
	return 1;
}

static int Clear(BloomFilter *b)
{
	if (b == NULL) {
		iError.RaiseError("iBloomFilter.Clear",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	memset(b->bits,0,b->nbOfBits / 8 + (b->nbOfBits % 8 != 0));
	b->count = 0;
	return 1;
}

static int Finalize(BloomFilter *b)
{
	if (b == NULL) {
		iError.RaiseError("iBloomFilter.Finalize", CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	b->Allocator->free(b->bits);
	b->Allocator->free(b);
	return 1;

}


BloomFilterInterface iBloomFilter = {
CalculateSpace,
Create,
Add,
Find,
Clear,
Finalize,
};
