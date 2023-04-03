#define _GNU_SOURCE
#include <stddef.h>
#include <stdbool.h>
#include <immintrin.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

// number of parenthesis pairs
#define SIZE 20

// number of parenthesis characters in output
#define PSIZE (SIZE * 2)

// bitmask of parenthesis
#define PMASK ((1ULL << PSIZE) - 1)

// End state
#define FIN (((1ULL << SIZE) - 1) << (SIZE))

// upper size of the buffer, actual size will be smaller
// right now this is as big as it can be
#define CACHESIZE ((1 << 10) << 10)
#define BUFSIZE ((1 << 10) << 12)

// number of lines that fit in a pipe - additional byte for LF
#define PIPECNT ((int) (CACHESIZE) / (PSIZE + 1))

// how many iterations needed to print output in _N bit batches
#define BATCH_64 (1 + (PSIZE / 8))
#define BATCH_128 (1 + (PSIZE / 16))
#define BATCH_256 (1 + (PSIZE / 32))

// https://stackoverflow.com/a/1898487
#define is_aligned(ptr, align) \
    (((uintptr_t)(const void *)(ptr)) % (align) == 0)

// TODO: is valloc better here?
static char buf[BUFSIZE];
static char bufalt[BUFSIZE];
static char *currbuf = buf;
static char *cursor = buf;

static void
print_paren_bitmask(uint64_t paren)
{
	int i;
	// size_t off;
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
	mask = 0x0101010101010101;
	i = 0;
	for (; i < BATCH_64; i++) {
		// deposit instr writes the contiguous low bits of arg0 in the
		// pos of each set bit of arg1. Here, I set the lowest bit of
		// each byte in the u64 to the corresponding bits in the lowest
		// byte of paren.

		// TODO: is doing SIMD store faster?
		res = base | _pdep_u64(paren, mask);
		*((uint64_t *)cursor) = res;
		cursor += 8;
		paren >>= 8;
	}

	// set cursor based on initial pos because it probably overshot
	cursor = init_cur + PSIZE;
	*cursor++ = '\n';

	// // flush buffer if next iter could overflow it
	// off = (uintptr_t) cursor - (uintptr_t) buf;
	// if (off >= CACHESIZE - ((1 + BATCH_64) * 8)) {
	// 	write(STDOUT_FILENO, buf, off);
	// 	cursor = buf;
	// }
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
 * This is optimized by adding a 1 that's shifted to start of group, which is
 * effectively a swap and clear simultaniously
 */

static uint64_t
next_paren_bitmask(uint64_t curr)
{
	// first set bit
	const uint64_t first = _tzcnt_u64(curr);

	// number of contig bits grouped with first
	const uint64_t contig = _tzcnt_u64(~(curr >> first));

	// original bit positions
	const uint64_t orig = 0xAAAAAAAAAAAAAAAA; // 0b1010...

	// the bits that are to be reset deposited to their original positions
	// Both methods here seem to have identical speed. The bextr operation
	// itself is slower but the reduced setup makes up for it
	const uint64_t rst = _pdep_u64((1 << (contig - 1)) - 1, orig);
	// const uint64_t rst = _bextr_u64(orig, 0, contig * 2 - 2);

	return (curr + (1 << first)) | rst;
}

__attribute__((cold, noreturn)) static void 
exit_fail(void) 
{
	printf("error: %i\n", errno);
	exit(1);
}

static void
flush_buf(int lcnt) 
{
	ssize_t rem, amt;

	struct iovec iov = {
		.iov_base = currbuf,
		.iov_len = 0,
	};


	// using vmsplice to reduce write(2) overhead
	rem = lcnt * (PSIZE + 1);
	amt = 0;
	do {
		rem -= amt;
		iov.iov_len = rem;
		iov.iov_base += amt;

		amt = vmsplice(STDOUT_FILENO, &iov, 1, 0);
		// amt = write(STDOUT_FILENO, iov.iov_base, iov.iov_len);
		if (__builtin_expect(amt == -1, 0)) {
			exit_fail();
		}

	} while (rem > 0);

	// swap out other buffer
	// we do this to be sure the previous pipe is drained
	if (currbuf == buf) {
		currbuf = bufalt;
	} else {
		currbuf = buf;
	}
	cursor = currbuf;
}


int 
main(void)
{
	uint64_t paren;
	int i;

	if (fcntl(STDOUT_FILENO, F_SETPIPE_SZ, CACHESIZE) != CACHESIZE) {
		exit_fail();
	}

	paren = PMASK & 0xAAAAAAAAAAAAAAAA;

	print_paren_bitmask(paren);
	i = 1;
        do {
		for (; i < PIPECNT; i++) {
			paren = next_paren_bitmask(paren);
			print_paren_bitmask(paren);

			if (paren == FIN) {
				i++;
				break;
			}
		}
		flush_buf(i);
		i = 0;
        } while (paren != FIN);
	close(STDOUT_FILENO);
}
