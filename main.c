#include <stddef.h>
#include <stdbool.h>
#include <immintrin.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

// number of parenthesis pairs
#define SIZE 20

// number of parenthesis characters in output
#define PSIZE (SIZE * 2)

// bitmask of parenthesis
#define PMASK ((1ULL << PSIZE) - 1)

// End state
#define FIN (((1ULL << SIZE) - 1) << SIZE)
#define CACHESIZE 4096

// how many iterations needed to print output in _N bit batches
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
	uint64_t res;
	char *init_cur;
	uint64_t base, mask;

	init_cur = cursor;

	// map all unset bits in paren to '(' and set bits to ')'
	// '(' is hex 28 and ')' is hex 29
	// this means that we can just OR with 0x1 to make close paren

	// byte-wise write until 4-byte aligned
	while(!is_aligned(cursor, 4)) {
		*cursor++ = 0x28 | (paren & 1);
		paren >>= 1;
	}

	// deposit u32 to get up to 8-byte alignment
	if(!is_aligned(cursor, 8)) {
		res = 0x28282828 | _pdep_u32(paren, 0x01010101);
		*((uint32_t *)cursor) = res;
		cursor += 4;
		paren >>= 4;
	}

	base = 0x2828282828282828;
	// base = 0x3030303030303030;
	mask = 0x0101010101010101;
	i = 0;
	for (; i < BATCH_64; i++) {
		// deposit instr writes the contiguous low bits of arg0 in the
		// pos of each set bit of arg1. Here, I set the lowest bit of
		// each byte in the u64 to the corresponding bits in the lowest
		// byte of paren.
		res = base | _pdep_u64(paren, mask);
		*((uint64_t *)cursor) = res;
		cursor += 8;
		paren >>= 8;
	}

	// set cursor based on initial pos because it probably overshot
	cursor = init_cur + PSIZE;
	*cursor++ = '\n';

	// flush buffer if next iter could overflow it
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
 *
 * This is optimized by adding a bit that's shifted to start of group, which is
 * effectively a swap and clear at once
 */

static uint64_t
next_paren_bitmask(uint64_t curr)
{
	// first set bit
	const uint64_t first = _tzcnt_u64(curr);

	// number of contig bits grouped with first
	const uint64_t contig = _tzcnt_u64(~(curr >> first));

	// mask of the bits to be reset to starting pos
	const uint64_t mask = (1 << ((contig - 1) * 2)) - 1;

	const uint64_t orig = 0xAAAAAAAAAAAAAAAA; // 0b1010...
	return (curr + (1 << first)) | (orig & mask);
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
