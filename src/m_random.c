// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2012-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 2022-2023 by tertu marybig.
// Copyright (C) 1999-2023 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_random.c
/// \brief RNG for client effects and PRNG for game actions

#include "doomdef.h"
#include "doomtype.h"
#include "i_system.h" // I_GetRandomBytes
#include "time.h"

#include "m_random.h"
#include "m_fixed.h"

// SFC32 random number generator implementation


/** Generate a raw uniform random number using a particular state.
  *
  * \param state The RNG state to use.
  * \return A random UINT32.
  */
static inline UINT32 RandomState_Get32(rnstate_t *state) {
	UINT32 result, b, c;

	b = state->data[1];
	c = state->data[2];
	result = state->data[0] + b + state->counter++;

	state->data[0] = b ^ (b >> 9);
	state->data[1] = c * 9;
	state->data[2] = ((c << 21) | (c >> 11)) + result;

	return result;
}

/** Returns the next raw uniform random number that would be output,
  * but does not advance the RNG state.
  *
  * \param state The RNG state to use.
  * \return A 'random' UINT32.
  */
static inline UINT32 RandomState_Peek32(const rnstate_t *state)
{
	UINT32 result;
	result = state->data[0] + state->data[1] + state->counter;
	return result;
}

/** Seed an SFC32 RNG state with up to 96 bits of seed data.
  * The seed is always 96 bits total and will be padded with 0.
  *
  * \param state The RNG state to seed.
  * \param seeds A pointer to up to 3 UINT32s to use as seed data.
  * \param seed_count The number of seed words.
  */
static inline void RandomState_Seed(rnstate_t *state, UINT32 *seeds, size_t seed_count)
{
	size_t i;

	state->counter = 1;

	for(i = 0; i < 3; i++)
	{
		UINT32 seed_word;

		if(i < seed_count)
			seed_word = seeds[i];
		else
			seed_word = 0;

		// For SFC32, seed data should be stored in the state in reverse order.
		state->data[2-i] = seed_word;
	}

	for(i = 0; i < 16; i++)
		RandomState_Get32(state);
}

/** Gets a uniform number in the range [0, limit).
  * Technique is based on a combination of scaling and rejection sampling
  * and is adapted from Daniel Lemire.
  *
  * \note Any UINT32 is a valid argument for limit.
  *
  * \param state The RNG state to use.
  * \param limit The upper limit of the range.
  * \return A UINT32 in the range [0, limit).
  */
static inline UINT32 RandomState_GetKeyU32(rnstate_t *state, const UINT32 limit)
{
	UINT32 raw_random, scaled_lower_word;
	UINT64 scaled_random;

	// This algorithm won't work correctly if passed a 0.
	if (limit == 0) return 0;

	raw_random = RandomState_Get32(state);
	scaled_random = (UINT64)raw_random * (UINT64)limit;

	/*The high bits of scaled_random now contain the number we want, but it is
	possible, depending on the number we generated and the value of limit,
	that there is bias in the result. The rest of this code is for ensuring
	that does not happen.
	*/
	scaled_lower_word = (UINT32)scaled_random;

	// If we're lucky, we can bail out now and avoid the division
	if (scaled_lower_word < limit)
	{
		// Scale the limit to improve the chance of success.
		// After this, the first result might turn out to be good enough.
		UINT32 scaled_limit;
		// An explanation for this trick: scaled_limit should be
		// (UINT32_MAX+1)%range, but if that was computed directly the result
		// would need to be computed as a UINT64. This trick allows it to be
		// computed using 32-bit arithmetic.
		scaled_limit = (-limit) % limit;

		while (scaled_lower_word < scaled_limit)
		{
			raw_random = RandomState_Get32(state);
			scaled_random = (UINT64)raw_random * (UINT64)limit;
			scaled_lower_word = (UINT32)scaled_random;
		}
	}

	return scaled_random >> 32;
}

/** Attempts to seed a SFC32 RNG from a good random number source
  * provided by the operating system.
  * \return true on success, false on failure.
  */
static boolean RandomState_TrySeedFromOS(rnstate_t *state)
{
	UINT32 complete_word_count;

	union {
		UINT32 words[3];
		char bytes[sizeof(UINT32[3])];
	} seed_data;

	complete_word_count = I_GetRandomBytes((char *)&seed_data.bytes, sizeof(seed_data)) / sizeof(UINT32);

	// If we get even 1 word of seed, it's fine, but any less probably is not fine.
	if (complete_word_count == 0)
		return false;

	RandomState_Seed(state, seed_data.words, complete_word_count);

	return true;
}

#if ((__STDC_VERSION__ >= 201112L) || \
(((__GNUC__ >= 5) || (__clang_major__ >= 4)) && !defined(__STRICT_ANSI__)))
#define HAS_TIMESPEC
#endif

static void RandomState_Initialize(rnstate_t *state)
{
	if (!RandomState_TrySeedFromOS(state))
	{
		// Use the system time as a fallback for seeding.
		// If timespec is available, there will be a "nanosecond counter".
		// Regardless of its actual precision, it will be more precise than the
		// second value provided by the conventional system timer, so
		// it is worth trying to use if available.

		UINT64 time_seconds;
		UINT32 seeds[3];
		size_t copy_start;

		#ifdef HAS_TIMESPEC
		struct timespec ts;
		#endif // HAS_TIMESPEC

		memset(&seeds, 0, sizeof(seeds));

		#ifdef HAS_TIMESPEC
			timespec_get(&ts, TIME_UTC);
			// The number of nanoseconds will always fit into a UINT32 whatever
			// the underlying type is.
			seeds[0] = (UINT32)ts.tv_nsec;
			time_seconds = (UINT64)ts.tv_sec;
			copy_start = 1;
		#else
			time_seconds = (UINT64)time(NULL);
			copy_start = 0;
		#endif // HAS_TIMESPEC

		// Copy the seconds value into the seed array, accounting for the
		// different data types.
		memcpy(seeds + copy_start, &time_seconds, 2 * sizeof(UINT32));
		RandomState_Seed(state, seeds, 3);
	}
}

#undef HAS_TIMESPEC

/** Provides a random fixed point number. Distribution is uniform.
  *
  * \return A random fixed point number from [0,1).
  */
static inline fixed_t RandomState_GetFixed(rnstate_t *state)
{
	return RandomState_Get32(state) >> (32-FRACBITS);
}

static inline INT32 RandomState_GetRange(rnstate_t *state, INT32 a, INT32 b)
{
	if (b < a)
	{
		INT32 temp;

		temp = a;
		a = b;
		b = temp;
	}

	const UINT32 spread = b-a+1;
	return (INT32)((INT64)RandomState_GetKeyU32(state, spread) + a);
}

static inline INT32 RandomState_GetKeyI32(rnstate_t *state, const INT32 a) {
	boolean range_is_negative;
	INT64 range;
	INT32 random_result;

	range = a;
	range_is_negative = range < 0;

	if(range_is_negative)
		range = -range;

	random_result = RandomState_GetKeyU32(state, (UINT32)range);

	if(range_is_negative)
		random_result = -random_result;

	return random_result;
}


// ---------------------------
// RNG functions (not synched)
// ---------------------------

// The default seed is the hexadecimal digits of pi, though it will be overwritten.
static rnstate_t m_randomstate = {
	.data = {0x4A3B6035U, 0x99555606U, 0x6F603421U},
	.counter = 16
};

/** Provides a random fixed point number. Distribution is uniform.
  * As with all M_Random functions, not synched in netgames.
  *
  * \return A random fixed point number from [0,1).
  */
fixed_t M_RandomFixed(void)
{
	return RandomState_GetFixed(&m_randomstate);
}

/** Provides a random byte. Distribution is uniform.
  * As with all M_Random functions, not synched in netgames.
  *
  * \return A random integer from [0, 255].
  */
UINT8 M_RandomByte(void)
{
	return RandomState_Get32(&m_randomstate) >> 24;
}

/** Provides a random integer for picking random elements from an array.
  * Distribution is uniform.
  * As with all M_Random functions, not synched in netgames.
  * M_RandomKey is somewhat more complicated than P_RandomKey because
  * it supports negative integers.
  *
  * \param a Number of items in array.
  * \return A random integer from [0,a).
  */
INT32 M_RandomKey(INT32 a)
{
	return RandomState_GetKeyI32(&m_randomstate, a);
}

/** Provides a random integer in a given range.
  * Distribution is uniform.
  * As with all M_Random functions, not synched in netgames.
  *
  * \param a Lower bound.
  * \param b Upper bound.
  * \return A random integer from [a,b].
  */
INT32 M_RandomRange(INT32 a, INT32 b)
{
	return RandomState_GetRange(&m_randomstate, a, b);
}

/** Initializes the M_Random PRNG using a random seed.
  */
void M_RandomInitialize(void)
{
	RandomState_Initialize(&m_randomstate);
}

// ------------------------
// PRNG functions (synched)
// ------------------------

// Holds the current state.
static rnstate_t p_randomstate = {
	.data = {0x4A3B6035U, 0x99555606U, 0x6F603421U},
	.counter = 16
};

// Holds the INITIAL state value.  Used for demos, possibly also for debugging.
static rnstate_t p_initialstate = {
	.data = {0x4A3B6035U, 0x99555606U, 0x6F603421U},
	.counter = 16
};

/** Provides a random fixed point number. Distribution is uniform.
  *
  * \return A random fixed point number from [0,1).
  */
#ifndef DEBUGRANDOM
fixed_t P_RandomFixed(void)
{
#else
fixed_t P_RandomFixedD(const char *rfile, INT32 rline)
{
	CONS_Printf("P_RandomFixed() at: %sp %d\n", rfile, rline);
#endif
	return RandomState_GetFixed(&p_randomstate);
}

/** Provides a random byte. Distribution is uniform.
  * If you're curious, (&0xFF00) >> 8 gives the same result
  * as a fixed point multiplication by 256.
  *
  * \return Random integer from [0, 255].
  * \sa __internal_prng__
  */
#ifndef DEBUGRANDOM
UINT8 P_RandomByte(void)
{
#else
UINT8 P_RandomByteD(const char *rfile, INT32 rline)
{
	CONS_Printf("P_RandomByte() at: %sp %d\n", rfile, rline);
#endif
	return RandomState_Get32(&p_randomstate) >> 24;
}

/** Provides a random integer for picking random elements from an array.
  * Distribution is uniform.
  * NOTE: Maximum range is 65536.
  *
  * \param a Number of items in array.
  * \return A random integer from [0,a).
  * \sa __internal_prng__
  */
#ifndef DEBUGRANDOM
INT32 P_RandomKey(INT32 a)
{
#else
INT32 P_RandomKeyD(const char *rfile, INT32 rline, INT32 a)
{
	CONS_Printf("P_RandomKey() at: %sp %d\n", rfile, rline);
#endif
	return RandomState_GetKeyI32(&p_randomstate, a);
}

/** Provides a random integer in a given range.
  * Distribution is uniform.
  * NOTE: Maximum range is 65536.
  *
  * \param a Lower bound.
  * \param b Upper bound.
  * \return A random integer from [a,b].
  * \sa __internal_prng__
  */
#ifndef DEBUGRANDOM
INT32 P_RandomRange(INT32 a, INT32 b)
{
#else
INT32 P_RandomRangeD(const char *rfile, INT32 rline, INT32 a, INT32 b)
{
	CONS_Printf("P_RandomRange() at: %sp %d\n", rfile, rline);
#endif
	return RandomState_GetRange(&p_randomstate, a, b);
}

#ifndef DEBUGRANDOM
void P_RandomInitialize(void)
{
#else
UINT32 P_RandomInitializeD(const char *rfile, INT32 rline)
{
	CONS_Printf("P_RandomInitialize() at: %sp %d\n", rfile, rline);
#endif
	RandomState_Initialize(&p_randomstate);
	p_initialstate = p_randomstate;
}

// ----------------------
// PRNG seeds & debugging
// ----------------------

/** Peeks to see what the next result from the PRNG will be.
  * Used for debugging.
  *
  * \return A 'random' fixed point number from [0,1).
  */
UINT32 P_RandomPeek(void)
{
	return RandomState_Peek32(&p_randomstate);
}

/** Gets the current value of the counter part of the PRNG state.
  * Perhaps useful for debugging; the counter value diverging would indicate
  * that the PRNG value has diverged.
  *
  * \return The current value of the SFC32 counter.
  */
UINT32 P_GetRandCounter(void)
{
	return p_randomstate.counter;
}

/** Gets the current random state. Used by netgames.
  *
  * \return Current random state.
  * \sa P_SetRandState
  */
#ifndef DEBUGRANDOM
rnstate_t P_GetRandState(void)
{
#else
rnstate_t P_GetRandStateD(const char *rfile, INT32 rline)
{
	CONS_Printf("P_GetRandState() at: %sp %d\n", rfile, rline);
#endif
	return p_randomstate;
}

/** Gets the initial random state.
  * Used by demos and as part of netgame joining.
  *
  * \return Initial random state.
  * \sa P_SetRandState
  */
#ifndef DEBUGRANDOM
rnstate_t P_GetInitState(void)
{
#else
rnstate_t P_GetInitStateD(const char *rfile, INT32 rline)
{
	CONS_Printf("P_GetInitState() at: %sp %d\n", rfile, rline);
#endif
	return p_initialstate;
}

/** Sets the random state.
  * Used for demo playback and netgames.
  *
  * \param rindex New random state.
  */
#ifndef DEBUGRANDOM
void P_SetRandState(const rnstate_t *state)
{
#else
void P_SetRandStateD(const char *rfile, INT32 rline, const rnstate_t *state)
{
	CONS_Printf("P_SetRandState() at: %sp %d\n", rfile, rline);
#endif
	p_randomstate = *state;
	p_initialstate = *state;
}
