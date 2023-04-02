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

#define BUFCNT (CACHESIZE / (2 * SIZE + 1))

#define BATCH_64 (1 + (PSIZE / 8))
#define BATCH_128 (1 + (PSIZE / 16))
#define BATCH_256 (1 + (PSIZE / 32))

static char buf[CACHESIZE];
static int off = 0;

static void
print_paren(unsigned int *arr)
{
	int i;
	char c;

	// set all indicies in arr to '(' and others to ')'
	// '(' is hex 28 and ')' is hex 29
	// this means that we can just OR with 0x1 to make close paren

	// set all elements of line to close paren
	// XOR with 0x1 to make open paren (0x28)
	memset(&buf[off], 0x29, PSIZE);

	i = 0;
	for (; i < SIZE; i++) {
		buf[off + arr[i]] ^= 1;
	}
	off += PSIZE;
	buf[off++] = '\n';

	// flush buffer if full
	if (off >= CACHESIZE - (PSIZE + 1)) {
		write(STDOUT_FILENO, buf, off);
		off = 0;
	}
}

static void
print_paren_bitmask(uint64_t paren)
{
	int i;
	char c;

	// set all indicies in arr to '(' and others to ')'
	// '(' is hex 28 and ')' is hex 29
	// this means that we can just OR with 0x1 to make close paren

	// set all elements of line to close paren
	// XOR with 0x1 to swap paren open/close
	memset(&buf[off], 0x28, PSIZE);

	i = 0;
	for (; i < PSIZE; i++) {
		buf[off++] |= paren & 1;
		paren >>= 1;
	}
	buf[off++] = '\n';

	// flush buffer if full
	if (off >= CACHESIZE - (PSIZE + 1)) {
		write(STDOUT_FILENO, buf, off);
		off = 0;
	}
}

/*
 * properties:
 *
 * swapping adjacent (cpp, opp) -> (opp, cpp) is always valid
 */


/*
 * general strategy:
 *
 * arr stores the position of every open parenthesis.
 * an open parenthesis (opp) can be moved left, but should never occupy the same
 * space as another opp. At the end, all opps will be packed left.
 *
 * By representing opps as a bitmask, we can XOR the mask with a rshift by one
 * to get zeros where the next bit is the same
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

static bool
next_paren(unsigned int *arr)
{
	// Naive implementation:

	// i = SIZE - 1;
	// for (; i > 0; i--) {
	// 	arr[i] -= 1;
	//
	// 	if (arr[i] >= i && arr[i] > arr[i - 1])
	// 		break;
	// 	if (i == 1) 
	// 		return false;
	// 		
	// 	arr[i] = i * 2;
	// }

	return true;
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
