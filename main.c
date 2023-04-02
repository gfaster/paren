#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <immintrin.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define SIZE 20
#define PSIZE (SIZE * 2)
#define PMASK ((1ULL << PSIZE) - 1)
#define FIN (((1ULL << SIZE) - 1) << SIZE)
#define CACHESIZE 1024

#define BUFCNT (CACHESIZE / (PSIZE + 1))

#define BATCH_64 (1 + (PSIZE / 8))
#define BATCH_128 (1 + (PSIZE / 16))
#define BATCH_256 (1 + (PSIZE / 32))

// https://stackoverflow.com/a/1898487
#define is_aligned(ptr, align) \
    (((uintptr_t)(const void *)(ptr)) % (align) == 0)

static char buf[CACHESIZE];
static char* cursor = buf;

static void
print_paren_bitmask(uint64_t paren)
{
	int i;
	size_t off;
	char c;
	uint64_t res;
	char *init_cur;
	uint64_t base, mask;

	// set all indicies in arr to '(' and others to ')'
	// '(' is hex 28 and ')' is hex 29
	// this means that we can just OR with 0x1 to make close paren

	// set all elements of line to close paren
	// XOR with 0x1 to swap paren open/close
	memset(cursor, 0x28, PSIZE);
	init_cur = cursor;

	while(!is_aligned(cursor, 4)) {
		*cursor++ = 0x28 | (paren & 1);
		paren >>= 1;
	}

	if(!is_aligned(cursor, 8)) {
		res = 0x28282828 | _pdep_u32(paren, 0x01010101);
		*((uint32_t *)cursor) = res;
		cursor += 4;
		paren >>= 4;
	}

	base = 0x2828282828282828;
	mask = 0x0101010101010101;
	i = 0;
	for (; i < BATCH_64; i++) {
		res = base | _pdep_u64(paren, mask);
		*((uint64_t *)cursor) = res;
		cursor += 8;
		paren >>= 8;
	}
	cursor = init_cur + PSIZE;
	*cursor++ = '\n';

	// flush buffer if full
	off = (uintptr_t) cursor - (uintptr_t) buf;
	if (off >= CACHESIZE - ((1 + BATCH_64) * 8)) {
		write(STDOUT_FILENO, buf, off);
		cursor = buf;
	}
}

/*
 * properties:
 *
 * swapping adjacent (clp, opp) -> (opp, clp) is always valid
 */


/*
 * general strategy:
 *
 * curr is a bitmask of all the close parentheses (clp) with LSb the start of
 * the output string. 
 *
 * We find the rightmost contiguous bit, and reset all but the MSb of the
 * contiguous group that it's in to the original position. We then take that
 * remaining bit and swap it with the next most significant bit
 */

static uint64_t
next_paren_bitmask(uint64_t curr)
{
	// first set bit
	const uint64_t first = _tzcnt_u64(curr);

	// number of contig bits grouped with first
	const uint64_t contig = _tzcnt_u64(~(curr >> first));

	// first unset bit after first group
	const uint64_t head = first + contig;

	// XOR with curr to move leading bit of group
	const uint64_t swp = 0b11 << (head - 1);

	// mask of the bits to be reset to starting pos
	const uint64_t mask = (1 << (head - 1)) - 1;

	const uint64_t orig = 0xAAAAAAAAAAAAAAAA; // 0b1010...
	return swp ^ (curr & ~mask) | (orig & (mask >> 1));
}

int 
main(void)
{
	uint64_t paren;

	paren = PMASK & 0xAAAAAAAAAAAAAAAA;

        do {
		print_paren_bitmask(paren);
		paren = next_paren_bitmask(paren);
        } while (paren != FIN);
}
