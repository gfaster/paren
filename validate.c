#include <unistd.h>
#include <stdint.h>
#include <immintrin.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int linesz = 0;

#define LEN (64 + 1)

__attribute__((cold)) static void
print_paren(char* line)
{
	do {
		putchar(*line);
	} while (*line++ != '\n');
}

int
main(void)
{
	puts("starting validation...");
	char buf[LEN];
	ssize_t rem, amt;
	uint64_t prev, curr;
	uint64_t lineno;
	int i, stk;
	bool valid;

	prev = 0;
	lineno = 0;
	valid = true;
	while(true) {
		memset (buf, '-', LEN);
		lineno += 1;
		if (linesz == 0) {
			i = 0;
			do {
				amt = read(STDIN_FILENO, &buf[i], 1);
			} while (amt == -1 || buf[i++] != '\n');
			linesz = i - 1;
		} else {
			rem = linesz + 1;
			amt = 0;
			do {
				amt = read(STDIN_FILENO, &buf[linesz + 1 - rem], rem);
				if (amt == -1) {
					exit(errno);
				} 
				rem -= amt;
			} while (rem > 0);
		}

		i = 0;
		curr = 0;
		stk = 0;
		for (; i < linesz; i++) {
			if (buf[i] == 0x28) {
				stk += 1;
			} else if (buf[i] == 0x29) {
				stk -= 1;
				curr |= (1ull << i);
			} else {
				printf("invalid character: 0x%02x\n", buf[i]);
				valid = false;
			}

			if (stk < 0 && valid) {
				puts("invalid paren");
				valid = false;
			}
		}
		if (stk != 0 && valid) {
			puts("invalid paren");
			valid = false;
		}
		if (!valid) {
			close(STDIN_FILENO);
			printf("output line: %lu\n", lineno);
			printf("linesz: %i\n", linesz);
			printf("prev: %16lx\ncurr: %16lx\ncurr: ", prev, curr);
			print_paren(buf);
			exit(1);
		}


		if (prev >= curr) {
			close(STDIN_FILENO);
			puts("out of order");
			printf("output line: %lu\n", lineno);
			printf("prev: %16lx\ncurr: %16lx\ncurr: ", prev, curr);
			print_paren(buf);
			exit(2);
		}
		prev = curr;
		// printf("completed: %16lX\n", ((1ull << linesz) - 1) << linesz);

		if (lineno % (1ull << 20) == 0) {
			printf("completed: 0x%16lX\r", lineno);
			fflush(NULL);
		}

		if (curr == ((1ull << (linesz/2)) - 1) << (linesz/2)) {
			printf("completed: %16lX\n", lineno);
			puts("PASS");
			break;
		}

	}
}
