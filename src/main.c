#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#define SIZE 20

static void
print_paren(unsigned int *arr, size_t size, char* buf)
{
	size_t arr_idx;
	size_t i;

	arr_idx = 0;
	i = 0;
	for(; i < size * 2; i++) {
		if (arr_idx >= size || arr[arr_idx] > i) {
			buf[i] = ')';
		} else {
			buf[i] = '(';
			arr_idx += 1;
		}
	}
}

static bool
next_paren(unsigned int *arr, size_t size)
{
	size_t i;

	i = size - 1;
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
	unsigned int arr[SIZE];
	char buf[SIZE + 1];

	buf[SIZE] = '\0';
	i = 0;
	for (; i < SIZE; i++) {
		arr[i] = i * 2;
	}

	i = 0;
        do {
		if (i++ % 100000000 == 0) {
			print_paren(arr, SIZE, buf);
			puts(buf);
		}
        } while (next_paren(arr, SIZE));
}
