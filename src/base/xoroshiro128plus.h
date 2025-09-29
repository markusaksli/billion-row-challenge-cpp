#pragma once

/*  Written in 2016-2018 by David Blackman and Sebastiano Vigna (vigna@acm.org)

To the extent possible under law, the author has dedicated all copyright
and related and neighboring rights to this software to the public domain
worldwide.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <cstdint>
#include <random>

#include "type_macros.h"

/* This is xoroshiro128+ 1.0, our best and fastest small-state generator
   for floating-point numbers, but its state space is large enough only
   for mild parallelism. We suggest to use its upper bits for
   floating-point generation, as it is slightly faster than
   xoroshiro128++/xoroshiro128**. It passes all tests we are aware of
   except for the four lower bits, which might fail linearity tests (and
   just those), so if low linear complexity is not considered an issue (as
   it is usually the case) it can be used to generate 64-bit outputs, too;
   moreover, this generator has a very mild Hamming-weight dependency
   making our test (http://prng.di.unimi.it/hwd.php) fail after 5 TB of
   output; we believe this slight bias cannot affect any application. If
   you are concerned, use xoroshiro128++, xoroshiro128** or xoshiro256+.

   We suggest to use a sign test to extract a random Boolean value, and
   right shifts to extract subsets of bits.

   The state must be seeded so that it is not everywhere zero. If you have
   a 64-bit seed, we suggest to seed a splitmix64 generator and use its
   output to fill s.

   NOTE: the parameters (a=24, b=16, b=37) of this version give slightly
   better results in our test than the 2016 version (a=55, b=14, c=36).
*/

namespace Xoroshiro128Plus
{
	static u64 _rotl(const u64 x, int k) {
		return (x << k) | (x >> (64 - k));
	}

	// Struct for per-thread state
	struct Random
	{
		u64 s[2];

		void seed() {
			std::random_device rd;
			uint64_t seed1 = ((uint64_t)rd() << 32) ^ rd();
			uint64_t seed2 = ((uint64_t)rd() << 32) ^ rd();
			if (seed1 == 0 && seed2 == 0) seed2 = 1; // must not both be zero
			s[0] = seed1;
			s[1] = seed2;
		}

		Random()
		{
			seed();
		}

		u64 Next() {
			const u64 s0 = s[0];
			u64 s1 = s[1];
			const u64 result = s0 + s1;

			s1 ^= s0;
			s[0] = _rotl(s0, 24) ^ s1 ^ (s1 << 16); // a, b
			s[1] = _rotl(s1, 37); // c

			return result;
		}

		/* This is the Jump function for the generator. It is equivalent
		   to 2^64 calls to Next(); it can be used to generate 2^64
		   non-overlapping subsequences for parallel computations. */

		void Jump() {
			static const u64 JUMP[] = { 0xdf900294d8f554a5, 0x170865df4b3201fc };

			u64 s0 = 0;
			u64 s1 = 0;
			for (int i = 0; i < sizeof JUMP / sizeof * JUMP; i++)
				for (int b = 0; b < 64; b++) {
					if (JUMP[i] & UINT64_C(1) << b) {
						s0 ^= s[0];
						s1 ^= s[1];
					}
					Next();
				}

			s[0] = s0;
			s[1] = s1;
		}

		/* This is the long-Jump function for the generator. It is equivalent to
		   2^96 calls to Next(); it can be used to generate 2^32 starting points,
		   from each of which Jump() will generate 2^32 non-overlapping
		   subsequences for parallel distributed computations. */

		void LongJump() {
			static const u64 LONG_JUMP[] = { 0xd2a98b26625eee7b, 0xdddf9b1090aa7ac1 };

			u64 s0 = 0;
			u64 s1 = 0;
			for (int i = 0; i < sizeof LONG_JUMP / sizeof * LONG_JUMP; i++)
				for (int b = 0; b < 64; b++) {
					if (LONG_JUMP[i] & UINT64_C(1) << b) {
						s0 ^= s[0];
						s1 ^= s[1];
					}
					Next();
				}

			s[0] = s0;
			s[1] = s1;
		}

		// Convert Next() output to [0,1)
		double NextDouble() {
			// Use the top 53 bits for double precision in [0,1)
			return (Next() >> 11) * (1.0 / (UINT64_C(1) << 53));
		}

		double NextDouble(double min, double max)
		{
			assert(min < max);
			return min + NextDouble() * (max - min);
		}
	};
}
