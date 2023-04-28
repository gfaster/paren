#define _GNU_SOURCE

#include <stddef.h>
#include <stdbool.h>
#include <immintrin.h>
#include <x86intrin.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>


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

// bytes per line - one more fore LF
#define LSIZE (PSIZE + 1)

// bitmask of parenthesis
#define PMASK ((1ULL << PSIZE) - 1)

// End state
#define FIN (((1ULL << SIZE) - 1) << (SIZE))

// upper size of the buffer, actual size will be smaller
// right now this is as big as it can be
#define BUFSIZE ((1 << 10) << 12)
#define DUAL_BUFSIZE (2 * BUFSIZE)
#define BUFALIGN (1 << 22)

// I am consistantly surprised at how big it's practical for the cache size to
// be. Doing tperf tests as of v6.0 with 1<<19 (512kiB) gives cache miss rate of
// 0.009% but 1<<20 (1MiB) has only 0.011% cache misses. It's not enough to make
// the smaller cache faster (actually it's slower by ~100MiB)
#define CACHESIZE (1l << 20)

// how many iterations needed to print output in _N bit batches
#define BATCH_32 (1 + (LSIZE / 4))
#define BATCH_64 (1 + (LSIZE / 8))
#define BATCH_128 (1 + (LSIZE / 16))
#define BATCH_256 (1 + (LSIZE / 32))

// SIMD batch size (in bytes) such that AVX2 stores can be used
// additional byte per line for LF
// this is the size of the SIMD REGISTER
#define BATCH_SIZE 32

#if (LSIZE < 32)
#error "need code for shorter lines"
#endif
#if ((LSIZE / 2) * 2 == LSIZE)
#error "LSIZE should be odd how tf did this happen"
#endif

// number of store operations in a batch
// we need to make sure BATCH_SIZE has no cycles within a batch
// (LSIZE*n % BATCH_SIZE = is not zero between n=0 and n=BATCH_LINES, exclusive)
// this is because batched calculations rely on a single batch store having at
// most two lines a part of it
// Luckily, since LSIZE is odd and BATCH_SIZE is a power of 2, they're always
// relatively prime, so their LCM is LSIZE * BATCH_SIZE
#define BATCH_STORE (LSIZE * BATCH_SIZE)

// number of bytes in a series of vector stores such that LF is at the end
// At size=20, batch_size=32 this is 41984
#define BATCH_BYTES (BATCH_STORE * BATCH_SIZE)

// lines in a batch
#define BATCH_LINES (BATCH_BYTES / LSIZE)


// number of batch writes that fit in a pipe
#define PIPECNT ((CACHESIZE / BATCH_SIZE))


// https://stackoverflow.com/a/1898487
#define is_aligned(ptr, align) \
    (((uintptr_t)(const void *)(ptr)) % (align) == 0)

#ifdef DEBUG
#define ERROR(msg) err(1, "%s", msg)
#else 
#define ERROR(msg) exit_fail()
// #define ERROR(msg) err(1, "%s", msg)
#endif

#define unlikely(x) __builtin_expect((x), 0)

// io buffers need to be 32-byte alligned because otherwise 
// _mm256_store_si256 generates a general protection fault
// TODO: is valloc better here?
static char __attribute__ ((aligned(BUFALIGN))) buf[DUAL_BUFSIZE];
static char *currbuf = buf;
static char *cursor = buf;

// bytecode here is just doing as much pre-computation as possible. All the hot
// loop needs to do is distribute bits to low pos, then add with bytecode
static char __attribute__ ((aligned(32))) bytecode[BATCH_BYTES];


#if (PSIZE < 32)
#error "bytecode and batch cannot handle smaller than 32"
#endif

__attribute__((cold)) static void
gen_bytecode(uint64_t paren)
{
	// If I can make sure that two ints always have what I need, maybe
	// barrel shifted, I can have a bytecode permute. That would work even
	// if I can't get it perfect with the alignment - the shift can take
	// care of that for me

	int i;

	i = 0;
	for (; i < BATCH_BYTES; i++) {
		if ((i + 1) % LSIZE == 0) {
			bytecode[i] = '\n';
		} else if ((i + 0) % LSIZE >= 32) {
			// the upper bits are changed less often, so we can
			// precompute one for a few tens of thousands of lines
			// bytecode[i] = 0x28 + ((paren >> ((i % LSIZE) - 32)) & 0x1);
			// bytecode[i] = '0';
			bytecode[i] = 0x28;
		} else {
			bytecode[i] = 0x28;
		}
	}
}

__attribute__((cold, noreturn)) static void 
exit_fail(void) 
{
	err(1, "error: %i", errno);
}

static void
flush_buf(size_t bcnt) 
{
	ssize_t rem, amt;

	struct iovec iov = {
		.iov_base = currbuf,
		.iov_len = 0,
	};


	// using vmsplice to reduce write(2) overhead
	rem = bcnt;
	amt = 0;
	do {
		rem -= amt;
		iov.iov_len = rem;
		iov.iov_base += amt;

		#ifndef DEBUG
			amt = vmsplice(STDOUT_FILENO, &iov, 1, 0);
		#else
			amt = write(STDOUT_FILENO, iov.iov_base, iov.iov_len);
		#endif
		if (unlikely(amt == -1)) {
			ERROR("writing error");
		}
	} while (rem > 0);

	// swap out other buffer
	// we do this to be sure the previous pipe is drained
	cursor = buf + ((currbuf - buf) ^ BUFSIZE);
	currbuf = cursor;
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

inline static uint64_t
next_paren_bitmask(uint64_t curr)
{
	// first set bit
	const uint64_t first = _tzcnt_u64(curr);

	// number of contig bits grouped with first
	const uint64_t contig = _tzcnt_u64(~(curr >> first));

	// carry forward
	const uint64_t add = curr + (1 << first);

	// original bit positions
	const uint64_t orig = 0xAAAAAAAAAAAAAAAA; // 0b1010...

	// the bits that are to be reset deposited to their original positions
	// Both methods here seem to have identical speed. The bextr operation
	// itself is slower but the reduced setup makes up for it
	// const uint64_t rst = _pdep_u64((1 << (contig - 1)) - 1, orig);
	// const uint64_t rst = _bextr_u64(orig, 0, contig * 2 - 2);
	const uint64_t rst = orig & ((1 << (contig * 2 - 1)) - 1);

	const uint64_t ret = add | rst;

	// need new bytcode if change in upper dword
	if (unlikely((0xFFFFFFFF00000000 & add) > curr)) {
		gen_bytecode(ret);
	}

	return ret;
}

static void 
do_batch(uint64_t paren)
{
	/*
	 * I have two options here - I can store or recalculate offsets
	 */
	#if (PSIZE < 32)
	#error "batch is for lines longer than 32"
	#endif

	int i;
	uint64_t bcidx;
	int64_t poff, voff;
	__m256i resv, bcv;
	uint32_t curr;

	const __m256i shufmask = _mm256_set_epi64x(
					0x0303030303030303,
					0x0202020202020202,
					0x0101010101010101,
					0x0000000000000000);
	const __m256i andmask = _mm256_set1_epi64x(0x8040201008040201);

	poff = 0;
	voff = PSIZE;
	i = 0;
	bcidx = 0;
	while(paren != FIN) {
		curr = paren >> poff;

		if (voff < 32) {
			// what if voff == 0?
			paren = next_paren_bitmask(paren);
			curr |= paren << (voff + 1); // breaks down at PS = 64
			poff = 32 - (voff + 1);
		} else {
			poff += 32;
		}
		
		voff = PSIZE - poff; // voff idx of LF

		// also try _mm256_lddqu_si256
		bcv = _mm256_load_si256((__m256i *)&bytecode[bcidx]);
		bcidx += BATCH_SIZE;
		if (bcidx == BATCH_STORE) {
			bcidx = 0;
		}

		// trying to find a 256-bit deposit equivallent
		// if we move each byte of the 32-bit paren to the qword it
		// belongs to, we can just AND it with that bit set

		// only need the low 32 bits of each lane set, but this is fine
		resv = _mm256_set1_epi32(curr);

		// move the byte of paren that has the bit in the corresponding
		// position in the vector to that position.
		resv = _mm256_shuffle_epi8(resv, shufmask);

		// only let the correct bit be set
		resv = _mm256_and_si256(resv, andmask);

		// set all nonzero bytes to -1
		// reuse andmask because it's a superset of resv
		resv = _mm256_cmpeq_epi8(resv, andmask);

		// combine with bytecode
		resv = _mm256_sub_epi8(bcv, resv);


		_mm256_store_si256((__m256i *) cursor, resv);
		cursor += 32;


		// for whatever reason, transfers are done 128 bytes less
		// than expected (check with strace)
		//
		// for whatever reason, keeping the `paren == FIN` decreases
		// brach mispredictions by ~3%. Combined with removing the
		// post-loop flush, it decreases by 95%. Only doing the latter
		// brings no benefit. Why? let me grab my grimoire so I can
		// summon the Great Old Ones in search of answers.
		if (i >= PIPECNT || paren == FIN) {
			// flush_buf((i) * BATCH_SIZE);
			flush_buf(cursor - currbuf);
			i = 0;
		}
		i += 1;
	} while(paren != FIN);
	// flush_buf((i) * BATCH_BYTES);
}



void 
_start(void)
{
	uint64_t paren;

	if ( !is_aligned(buf, BUFALIGN)) {
		ERROR("buf is not aligned");
	}

	if (madvise(buf, DUAL_BUFSIZE, MADV_HUGEPAGE) == -1) {
		ERROR("huge page fail");
	}

	#ifndef DEBUG
	if (fcntl(STDOUT_FILENO, F_SETPIPE_SZ, CACHESIZE) != CACHESIZE) {
		ERROR("Pipe could not be resized");
	}
	#endif


	// 9 at the end to initialize into correct place
	// (removed for batching)
	paren = PMASK & 0xAAAAAAAAAAAAAAAA;
	gen_bytecode(paren);
	do_batch(paren);
	close(STDOUT_FILENO);
	exit(0);
}

/*
vim:tw=80
*/
