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


#define max(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b;       \
})

#define min(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b;       \
})

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


// how many iterations needed to print output in _N bit batches
#define BATCH_32 (1 + (PSIZE / 4))
#define BATCH_64 (1 + (PSIZE / 8))
#define BATCH_128 (1 + (PSIZE / 16))
#define BATCH_256 (1 + (PSIZE / 32))

// SIMD batch size (in bytes) such that AVX2 stores can be used
// additional byte per line for LF
// decreasing this to 16 may allow better use of registers
#define BATCH_SIZE 32
#define BATCH_STORE ((PSIZE + 1) * BATCH_SIZE)

// number of batches that fit in a pipe
#define PIPECNT ((int) (CACHESIZE) / (PSIZE + 1))

// https://stackoverflow.com/a/1898487
#define is_aligned(ptr, align) \
    (((uintptr_t)(const void *)(ptr)) % (align) == 0)

// buffers need to be 32-byte alligned because otherwise 
// _mm256_store_si256 generates a general protection fault
// TODO: is valloc better here?
static char __attribute__ ((aligned(16))) buf[BUFSIZE];
static char __attribute__ ((aligned(16))) bufalt[BUFSIZE];
static char *currbuf = buf;
static char *cursor = buf;

static inline void
print_paren_bitmask(uint64_t paren)
{
	int i;
	// size_t off;
	// uint64_t res;
	char *init_cur;
	__m256i resv;

	const __m256i basev = _mm256_set1_epi8(0x28);
	const __m256i shufmask = _mm256_set_epi64x(
					0x0303030303030303,
					0x0202020202020202,
					0x0101010101010101,
					0x0000000000000000);
	const __m256i andmask = _mm256_set1_epi64x(0x8040201008040201);
	const __m256i onemask = _mm256_set1_epi8(0x01);
	const __m256i newl = _mm256_insert_epi8(_mm256_setzero_si256(),
						'\n' ^ 0x28, 
						       (PSIZE + 0) % 32);
	init_cur = cursor;
	i = 0;
	for (; i < BATCH_256; i++) {
		// trying to find a 256-bit deposit equivallent
		// if we move each byte of the 32-bit paren to the qword it
		// belongs to, we can just AND it with that bit set

		// only need the low 32 bits of each lane set, but this is fine
		resv = _mm256_set1_epi32(paren);

		// move the byte of paren that has the bit in the corresponding
		// position in the vector to that position.
		resv = _mm256_shuffle_epi8(resv, shufmask);

		// only let the correct bit be set
		resv = _mm256_and_si256(resv, andmask);

		// I can either do this or testeq 0
		// TODO: experiment here
		resv = _mm256_cmpeq_epi8(resv, andmask);
		resv = _mm256_and_si256(resv, onemask);

		if (i == BATCH_256 - 1) {
			resv = _mm256_or_si256(resv, newl);
		}

		// resv = _mm256_xor_si256(resv, onemask);

		resv = _mm256_xor_si256(resv, basev);
		_mm256_storeu_si256((__m256i *) cursor, resv);
		cursor += 32;
		paren >>= 32;
	}

	// set cursor based on initial pos because it probably overshot
	cursor = init_cur + PSIZE + 1;
	// *cursor++ = '\n';
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

	// 9 at the end to initialize into correct place
	paren = PMASK & 0xAAAAAAAAAAAAAAA9;

        do {
		i = 0;
		for (; i < PIPECNT; i++) {
			paren = next_paren_bitmask(paren);
			print_paren_bitmask(paren);

			if (paren == FIN) {
				i++;
				break;
			}
		}
		flush_buf(i);
        } while (paren != FIN);
	close(STDOUT_FILENO);
}
