#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <immintrin.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SIZE 20
#define PSIZE (SIZE * 2)
#define CACHESIZE 1024

#define BUFCNT (CACHESIZE / (2 * SIZE + 1))

#define BATCH_64 (1 + (2 * SIZE / 8))
#define BATCH_128 (1 + (2 * SIZE / 16))
#define BATCH_256 (1 + (2 * SIZE / 32))

static char buf[CACHESIZE];
static int off = 0;

// set 8 chars of buf at off parens
// always 8 chars, no fewer
inline static void
prnt8(__m256i mask)
{
	/* 
	 * _mm256_shuffle_epi8 to set chars
	 *
	 */

	// set all indicies in arr to '(' and others to ')'
	// '(' is hex 28 and ')' is hex 29
	// this means that we can just OR with 0x1 to make close paren

	__m256i close;
	__m256i open;
	__m256i dst;

	open = _mm256_set1_epi8('(');
	close = _mm256_set1_epi8(')');
	dst = _mm256_blendv_epi8(close, open, mask);
	_mm256_storeu_si256((__m256i *) &buf[off], dst);
}

/*
static void
print_paren_simd(unsigned int *arr)
{
	size_t arr_idx;
	size_t i;

	__m128i open, close;


	__m256i tbuf[BATCH_256];

	__m256i intvl, accintvl;
	__m256i zero, minusone;
	__m256i dst, dstmask;

	close = _mm_set1_epi8(1);
	open = _mm_set1_epi8(0x28);
	intvl = _mm256_set1_epi32(8);
	accintvl = intvl;
	zero = _mm256_set1_epi32(0);
	minusone = _mm256_set1_epi32(-1);

	i = 0;
	arr_idx = 0;
	for (; i < BATCH_64; i++) {
		// load the component of the vector
		// Using lddqu since probably not aligned
		dst = _mm256_lddqu_si256(&arr[i]);

		// decrease so useful range is 0 < x < 8
		dst = _mm256_sub_epi32(dst, accintval);

		// create a mask of only valid indices
		dstmask = _mm256_cmplt_epi32(intvl, dst);
		dstmask = _mm256_cmpgt_epi32(zero, dstmask);
		dstmask = _mm256_cmpeq_epi32(dstmask, minusone);
		dstmask = _mm256_xor_si256(dstmask, minusone);

	}
	
	_mm_storeu_si128((_m128i *) &buf[off], dst);


	/1* arr_idx = 0;
	i = 0;
	for(; i < SIZE * 2; i++) {
		if (arr_idx >= SIZE || arr[arr_idx] > i) {
			putchar(')');
		} else {
			putchar('(');
			arr_idx += 1;
		}
	}
	putchar('\n'); *1/
}
*/

static void
print_paren(unsigned int *arr)
{
	int i;
	char c;

	// set all elements of line to close paren
	// XOR with 0x1 to make open paren (0x28)
	memset(&buf[off], 0x29, PSIZE);

	i = 0;
	for (; i < SIZE; i++) {
		buf[off + arr[i]] ^= 1;
	}
	off += PSIZE;
	buf[off++] = '\n';

	if (off >= CACHESIZE - (PSIZE + 1)) {
		write(STDOUT_FILENO, buf, off);
		off = 0;
	}
}

static bool
next_paren(unsigned int *arr)
{
	size_t i;

	i = SIZE - 1;
	/* yes, not including 0 - always start with open*/
	for (; i > 0; i--) {
		arr[i] -= 1;

		if (arr[i] >= i && arr[i] > arr[i - 1])
			break;
		if (i == 1) 
			return false;
			
		arr[i] = i * 2;
	}

	return true;
}

int 
main(void)
{
	size_t i;
	// __m256i mask;
	// __m256i open, close;
	// __m256i dst;

	// open = _mm256_set1_epi8('(');
	// close = _mm256_set1_epi8(')');
	unsigned int arr[SIZE];

	i = 0;
	for (; i < SIZE; i++) {
		arr[i] = i * 2;
	}

	// mask = 0;
        do {
		// i = 0;
		// for(; i < SIZE * 2; i++) {
			// 
		// }
		
		// i = 0;
		// for(; i < NPRNTBATCH; i++) {
			// dst = _mm256_blendv_epi8(close, open, mask);
			// _mm256_storeu_si256((_m256i *) &buf[off], dst);
		// }

		print_paren(arr);
        } while (next_paren(arr));
	// printf("%lu\n", i);
}
