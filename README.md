## Parens

Outputs sets of well-formed parentheses to a Linux pipe. Inspired by [Leetcode
22](https://leetcode.com/problems/generate-parentheses/) and
[htfizzbuzz](https://github.com/orent/htfizzbuzz).

Starts from `()()()()...()` and ends with `((((...))))`.

### Running
Requires Linux >= 2.6.17 and glibc >= 2.5. Other Unix-like OS's do not work
since `vmsplice(2)` is Linux-specific. Because it exclusively outputs to a pipe,
it is necessary to always read the output throught a pipe (`./paren` alone will
fail, instead use `./paren | cat`). Additonally, if the first program it is
piped to uses `splice(2)` for input and `vmsplice(2)` for output, then anything
further will likely recieve corrupted output (For example, `./paren | pv |
./validate` will fail, but `./paren | cat | ./validate` is fine).

Alternatively, run using `make`:
- `make tpseed` does a 15 second speed test.
- `make tvalid` does validation testing.
- `make tperf` does a performance profile.
- `make tperfstat` runs `perf stat`

### Method
I calculate the next permutation as a 64-bit unsigned integer (least-significant
bit is the first byte of the line) with set bits representing close parentheses.
I then shift and broadcast the bits to an AVX2 (my laptop does not have AVX-512)
vector and write that to the buffer. The buffer is one of two, and uses
`vmsplice(2)` to output. On output, the buffer is swapped so that the first is
(supposed to be) fully consumed before it is overwritten.

### Performance

The current commit runs at 11.3GiB/s of valid output. Tests and benchmarks
were run with SIZE=20 on my Linux 6.1.0 Thinkpad P1 Gen 3 with an i7-10750H.
