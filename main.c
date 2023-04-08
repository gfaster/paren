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
#define PIPECNT ((int) (CACHESIZE) / (BATCH_256 * 32))

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
	uint64_t res;
	char *init_cur;
	__m256i resv;

	const uint64_t base = 0x2828282828282828;
	const uint64_t mask = 0x0101010101010101;
	const __m256i basev = _mm256_set1_epi64x(base);

	init_cur = cursor;

	// map all unset bits in paren to '(' and set bits to ')'
	// '(' is hex 28 and ')' is hex 29
	// this means that we can just OR with 0x1 to make close paren

	// TODO: is adding the base before or after moving to resv faster?
	i = 0;
	// This loop should ALWAYS be unrolled
	for (; i < BATCH_256; i++) {
		// deposit instr writes the contiguous low bits of arg0 in the
		// pos of each set bit of arg1. Here, I set the lowest bit of
		// each byte in the u64 to the corresponding bits in the lowest
		// byte of paren.

		// TODO: is combining these instructions faster?
		if (i * 4 + 0 < BATCH_64) {
			res = _pdep_u64(paren >> 0, mask);
			resv = _mm256_insert_epi64(resv, res, 0);
		}
		if (i * 4 + 1 < BATCH_64) {
			res = _pdep_u64(paren >> 8, mask);
			resv = _mm256_insert_epi64(resv, res, 1);
		}
		if (i * 4 + 2 < BATCH_64) {
			res = _pdep_u64(paren >> 16, mask);
			resv = _mm256_insert_epi64(resv, res, 2);
		}
		if (i * 4 + 3 < BATCH_64) {
			res = _pdep_u64(paren >> 24, mask);
			resv = _mm256_insert_epi64(resv, res, 3);
		}
		if (i == BATCH_256 - 1) {
			// when it's xor'd again it become 0x0a (\n)
			resv = _mm256_insert_epi8(resv, '\n' ^ 0x28,
					  PSIZE - ((BATCH_256 - 1) * 32));
		}
		// TODO: is doing the extra work to align this faster?
		resv = _mm256_xor_si256(resv, basev);
		_mm256_storeu_si256((__m256i *) cursor, resv);
		cursor += 32;
		paren >>= 32;
	}

	// set cursor based on initial pos because it probably overshot
	cursor = init_cur + PSIZE + 1;
	// *cursor++ = '\n';
}

static void
print_paren_bitmask_batched(uint64_t paren, __m128i *v, int *idx)
{
	/*
	 * The challenge here is that there is that in general, a line is not
	 * aligned with vector registers, so they can't be used to store the
	 * output. We can solve that by packing paren bitstrings - but now our
	 * advancement code is much harder to manage. It would require, among
	 * other things, carries accross lane boundaries and shifts before
	 * leading zero calculations.
	 *
	 * My first shot at a solution here is combining them after generation.
	 * 
	 *
	 * Check out _mm_alignr instructions
	 *
	 * Whatever solution I settle on should be branchless in the compiled
	 * code - the behavior here should be only dependant on constant values
	 *
	 * THIS DOES NOT WORK YET
	 *
	 * may need to use -ftree-loop-ivcanon and/or -funroll-loops
	 */
	#if (PSIZE <= 8)
	#error "batch store assumes larger than 8"
	#endif

	int i;
	uint64_t hi, lo;
	__m128i vhi, vlo, vlf;

	const uint64_t depmask = 0x1010101010101010;
	const uint64_t base    = 0x2828282828282828;

	const __m128i basev = _mm_set1_epi64x(base);
	const __m128i zero128 = _mm_setzero_si128();

	i = 0;
	for (; i < BATCH_64; i++) {
		// deposit instr writes the contiguous low bits of arg0 in the
		// pos of each set bit of arg1. Here, I set the lowest bit of
		// each byte in the u64 to the corresponding bits in the lowest
		// byte of paren.

		*idx %= 16;

		lo = _pdep_u64(paren >> (8 * i), depmask);
		hi = lo;
		lo <<= 8 * (*idx % 4); 
		hi >>= 8 * ((*idx + 3) % 4);

		// needed because _mm_insert has a constant index
		if (*idx / 8 == 0) {
			// when hi is on the right and it 
			vhi = _mm_set_epi64x(0, hi);
			vlo = _mm_set_epi64x(lo, 0);
		} else if (*idx / 8 == 1) {
			vhi = _mm_set_epi64x(hi, 0);
			vlo = _mm_set_epi64x(0, lo);
		} else {
			__builtin_unreachable();
		}

		// set newline
		if (i == BATCH_64 - 1) {
			// when it's xor'd again it become 0x0a (\n)
			// TODO: clean up this
			vlf = zero128;
			vlf = _mm_insert_epi8(vlf, '\n' ^ 0x28, 0);
			vlf = _mm_slli_si128(vlf, 
			         8 * ((PSIZE - (4 * i) + *idx) % 4));
		}

		*v = _mm_or_si128(*v, vlo);

		if (*idx / 8 == 1) {
			// TODO: add another variable to parallelize
			*v = _mm_or_si128(*v, basev);
			*v = _mm_xor_si128(*v, vlf);
			_mm_store_si128((__m128i *) cursor, *v);
			*v = zero128;
			cursor += 16;
		} 

		*v = _mm_or_si128(*v, vhi);

		paren >>= 8;
		*idx += min(4, 1 + PSIZE - (4 * i));
	}
	// newline was added
	*idx += 1;
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

		// amt = vmsplice(STDOUT_FILENO, &iov, 1, 0);
		amt = write(STDOUT_FILENO, iov.iov_base, iov.iov_len);
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
	__m128i v;
	int idx;
	int i;

	if (fcntl(STDOUT_FILENO, F_SETPIPE_SZ, CACHESIZE) != CACHESIZE) {
		exit_fail();
	}

	idx = 0;

	// 9 at the end to initialize into correct place
	paren = PMASK & 0xAAAAAAAAAAAAAAA9;

        do {
		i = 0;
		for (; i < PIPECNT; i++) {
			paren = next_paren_bitmask(paren);
			print_paren_bitmask_batched(paren, &v, &idx);

			if (paren == FIN) {
				i++;
				break;
			}
		}
		flush_buf(i);
        } while (paren != FIN);
	close(STDOUT_FILENO);
}
