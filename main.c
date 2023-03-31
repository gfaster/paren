#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#define SIZE 20

static void
print_paren(unsigned int *arr, size_t size)
{
	size_t arr_idx;
	size_t i;

	arr_idx = 0;
	i = 0;
	for(; i < size * 2; i++) {
		if (arr_idx >= size || arr[arr_idx] > i) {
			putchar(')');
		} else {
			putchar('(');
			arr_idx += 1;
		}
	}
	putchar('\n');
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

	i = 0;
	for (; i < SIZE; i++) {
		arr[i] = i * 2;
	}

	i = 0;
        do {
		if (i++ % (1l << 22) == 0)
			print_paren(arr, SIZE);
        } while (next_paren(arr, SIZE));
	printf("%lu\n", i);
}
