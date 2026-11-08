/*
 * spl_algo0x11.cpp - QPDL compression algorithm 0x11
 *
 * Derived from SpliX's algo0x11.cpp, (C) 2006-2008 Aurelien Croc (AP2C).
 * Distributed under the GNU General Public License, version 2.
 *
 * The scheme is a small LZ77 variant.  Instead of arbitrary back-references
 * the stream carries a table of 64 distances; every match token then names
 * one of those distances and a length.  Layout of a compressed band:
 *
 *   uint32  number of leading literal bytes (N, at most 128)
 *   uint16  distance table, 64 entries
 *   N raw bytes
 *   tokens:
 *     0lllllll  + (l + 1) literal bytes                       (1..128 bytes)
 *     1lllllll hhpppppp   copy (hh:lllllll) + 3 bytes         (3..514 bytes)
 *                         from distance table[pppppp]
 *
 * Multi-byte values are written little-endian here; the band sub-header
 * signature (see spl_encoder.cpp) tells the printer which byte order is used.
 *
 * Differences from SpliX, none of which change the output it produces on a
 * little-endian glibc system:
 *   - the distance table is chosen with an explicit, stable selection rather
 *     than qsort(), whose order for equal keys depends on the C library;
 *   - integers are serialised byte by byte instead of through pointer casts.
 */
#include "spl.h"

#include <string.h>

namespace spl {


static const uint32_t kSampleRate = 0x800;
static const uint32_t kTableSize = 0x40;
static const uint32_t kMaxLiteralRun = 0x80;
static const uint32_t kMaxMatch = 0x202;
static const uint32_t kMinMatch = 0x2;		// matches must be longer than this
static const uint8_t kMatchFlag = 0x80;


/*!	Picks the 64 distances that are most likely to produce matches.

	Every kSampleRate-th byte is compared with its 2047 predecessors; the
	distances with the most hits win.  Ties go to the smaller distance, which
	is what a stable sort of SpliX's table gives.  Distance 1 is forced into
	the table because it is what makes runs of identical bytes cheap.
*/
static void
BuildDistanceTable(const uint8_t* data, size_t size, uint32_t* table)
{
	static const uint32_t kUsed = 0xffffffff;
	uint32_t hits[kSampleRate];
	memset(hits, 0, sizeof(hits));

	for (size_t i = kSampleRate; i < size; i += kSampleRate) {
		const uint8_t sample = data[i];
		for (uint32_t distance = 1; distance < kSampleRate; distance++) {
			if (data[i - distance] == sample)
				hits[distance - 1]++;
		}
	}

	bool haveDistanceOne = false;
	for (uint32_t slot = 0; slot < kTableSize; slot++) {
		uint32_t best = 0;
		bool found = false;
		for (uint32_t i = 0; i < kSampleRate; i++) {
			if (hits[i] == kUsed)
				continue;
			if (!found || hits[i] > hits[best]) {
				best = i;
				found = true;
			}
		}
		hits[best] = kUsed;
		table[slot] = best + 1;
		if (table[slot] == 1)
			haveDistanceOne = true;
	}

	if (!haveDistanceOne)
		table[kTableSize - 1] = 1;
}


size_t
CompressAlgo0x11(const uint8_t* data, size_t size, uint8_t* out)
{
	// The header alone needs 4 + 128 bytes plus up to 128 literals.
	if (data == NULL || out == NULL || size < 4 + 2 * kTableSize + kMaxLiteralRun)
		return 0;

	uint32_t table[kTableSize];
	BuildDistanceTable(data, size, table);

	const size_t maxOutput = size;
	size_t w = 4;
	size_t r = 0;

	// Distance table
	uint32_t literalStart = 0;
	for (uint32_t i = 0; i < kTableSize; i++, w += 2) {
		out[w] = (uint8_t)table[i];
		out[w + 1] = (uint8_t)(table[i] >> 8);
		if (table[i] > literalStart)
			literalStart = table[i];
	}

	// The first bytes cannot refer back to anything: send them raw.
	if (literalStart > kMaxLiteralRun)
		literalStart = kMaxLiteralRun;
	out[0] = (uint8_t)literalStart;
	out[1] = (uint8_t)(literalStart >> 8);
	out[2] = (uint8_t)(literalStart >> 16);
	out[3] = (uint8_t)(literalStart >> 24);
	for (; r < literalStart; r++, w++)
		out[w] = data[r];

	uint32_t literalCount = 0;	// length of the literal run being built
	size_t literalHeader = 0;	// where its length byte goes

	do {
		const size_t remaining = size - r;
		const size_t maxMatch = remaining > kMaxMatch ? kMaxMatch : remaining;

		if (maxMatch == 0) {
			// End of data: close the open literal run, if any.
			if (literalCount > 0)
				out[literalHeader] = (uint8_t)(literalCount - 1);
			break;
		}

		if (maxMatch >= 2) {
			if (w + 2 >= maxOutput) {
				w += 2;
				break;
			}

			// Longest match over all table distances; first one wins ties.
			size_t bestLength = 0;
			uint32_t bestIndex = 0;
			for (uint32_t i = 0; i < kTableSize; i++) {
				if (table[i] > r)
					continue;
				const uint8_t* current = data + r;
				const uint8_t* previous = current - table[i];
				size_t length = 0;
				while (length < maxMatch && current[length] == previous[length])
					length++;
				if (length > bestLength) {
					bestLength = length;
					bestIndex = i;
				}
			}

			if (bestLength > kMinMatch) {
				r += bestLength;
				bestLength -= 3;
				out[w] = (uint8_t)(kMatchFlag | (bestLength & 0x7f));
				out[w + 1] = (uint8_t)(((bestLength >> 1) & 0xc0)
					| (bestIndex & 0x3f));
				w += 2;
				if (literalCount > 0) {
					out[literalHeader] = (uint8_t)(literalCount - 1);
					literalCount = 0;
				}
				continue;
			}
		}

		// No usable match: emit the byte as a literal.
		literalCount++;
		if (literalCount == 1) {
			if (w + 2 >= maxOutput) {
				w += 2;
				break;
			}
			literalHeader = w;
			w++;
		} else if (literalCount == kMaxLiteralRun) {
			out[literalHeader] = 0x7f;
			literalCount = 0;
		}
		out[w] = data[r];
		w++;
		r++;
	} while (w < maxOutput);

	if (w >= maxOutput) {
		// Compressed form is not smaller than the input.
		return 0;
	}

	return w;
}


}	// namespace spl
