/* bitset.h - a bitset implementation
 * Mainly intended for handling sets of registers,
 * esp. for dataflow analysis */

#ifndef __bitset_h__
#define __bitset_h__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX2
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN2
#define MIN2(a, b) ((a) > (b) ? (b) : (a))
#endif

typedef struct {
	uint32_t* bits;
	unsigned size;
} bitset_t;

//Creates an empty set
static inline bitset_t bitset_create(unsigned size)
{
	bitset_t ret;
	ret.size = (size + 31) / 32;
	ret.bits = (uint32_t*) calloc(ret.size, sizeof(uint32_t));
	return ret;
}

static inline bitset_t bitset_create_full(unsigned size)
{
	bitset_t ret;
	ret.size = (size + 31) / 32;
	ret.bits = (uint32_t*) calloc(ret.size, sizeof(uint32_t));
	memset(ret.bits, 0xFF, (size / 32) * sizeof(uint32_t));
	if (size % 32 != 0)
		ret.bits[ret.size - 1] = (1 << (size % 32)) - 1;
	return ret;
}

static inline void bitset_copy(bitset_t* dest, bitset_t src)
{
	if (dest->size != src.size)
	{
		dest->bits = (uint32_t*) realloc(dest->bits, src.size);
		dest->size = src.size;
	}
	memcpy(dest->bits, src.bits, src.size * sizeof(uint32_t));
}

//Creates a new set, copying an old one
static inline bitset_t bitset_new(bitset_t old)
{
	bitset_t ret = bitset_create(old.size * 32);
	bitset_copy(&ret, old);
	return ret;
}

static inline void bitset_delete(bitset_t set)
{
	if (set.bits)
		free(set.bits);
}

static inline bool bitset_get(bitset_t set, unsigned elem)
{
	return (set.bits[elem >> 5] >> (elem & 0x1F)) & 1;
}

static inline void bitset_set(bitset_t set, unsigned elem, bool value)
{
	set.bits[elem >> 5] &= ~(1U << (elem & 0x1F));
	set.bits[elem >> 5] |= value << (elem & 0x1F);
}

static inline bool bitset_equal(bitset_t set1, bitset_t set2)
{
	if (set1.size != set2.size)
		return false;
	unsigned i;
	for (i = 0; i < set1.size; i++)
		if (set1.bits[i] != set2.bits[i])
			return false;
	
	return true;
}

static inline bool bitset_empty(bitset_t set)
{
	unsigned i;
	for (i = 0; i < set.size; i++)
		if (set.bits[i])
			return false;
	
	return true;
}

//dest = dest OR src
static inline void bitset_union(bitset_t* dest, bitset_t src)
{
	unsigned i;
	if (src.size > dest->size)
	{
		dest->bits = (uint32_t*) realloc(dest->bits, src.size);
		memset(&dest->bits[dest->size], 0, sizeof(uint32_t) * (src.size - dest->size));
		dest->size = src.size;
	}
	for (i = 0; i < dest->size; i++)
		dest->bits[i] |= src.bits[i];
}

//dest = dest AND src
static inline void bitset_disjunction(bitset_t* dest, bitset_t src)
{
	unsigned i, min_size = MIN2(dest->size, src.size);
	for (i = 0; i < min_size; i++)
		dest->bits[i] &= src.bits[i];
	for (; i < dest->size; i++)
		dest->bits[i] = 0;
}

//dest = dest - src
static inline void bitset_subtract(bitset_t* dest, bitset_t src)
{
	unsigned i, min_size = MIN2(dest->size, src.size);
	for (i = 0; i < min_size; i++)
		dest->bits[i] &= ~src.bits[i];
}


#endif
