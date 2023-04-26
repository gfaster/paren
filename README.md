## Parens

### Method
I calculate the next permutation as a 64-bit unsigned integer (least-significant
bit is the first byte of the line) with set bits representing close parentheses.
I then shift and broadcast the bits to an AVX2 (my laptop does not have AVX-512)
vector and write that to the buffer. The buffer is one of two, and uses
`vmsplice(2)` to output. On output, the buffer is swapped so that the first is
(supposed to be) fully consumed before it is overwritten.

### Performance
I haven't had fully valid output for quite some time, but my best somewhat
correct output was version 5.2 at 7.39 GiB/s and the best fully correct output
was 5.1 at 7.37 GiB/s. Tests were run with SIZE=20 on my Debian 11 Thinkpad P1
Gen 3 with an i7-10750H.

### Known Bottlenecks
I ran perf on it and it seems like the broadcast and shuffle instructions
account for ~30% of the total runtime, but I'm not quite sure if there would be
a better way.

