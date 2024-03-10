def make_macro(name, body):
    body = body.replace('\n', '\\\n')
    return f"#define {name.upper()} {body}\n"

def update_paren():
    return """
        paren = next_paren_bitmask(paren);
    """

def set_curr(lsize, voff, prev_start):
    ret = ""
    prev_end = prev_start + lsize

    if prev_end > 64 and prev_start < 32:
        ret += """
        // previous started at {} and ended at {}
        curr |= (paren >> {}) << {};
        """.format(prev_start, prev_end, 32 - prev_start, 0)


    if voff < 32:
        ret += update_paren()
        ret += """
        // goes up to {}
        curr |= paren << {};
        """.format(voff + lsize, voff)
        voff += lsize
    else:
        assert lsize >= voff

    # ret += "\n\t\tprint_bits(curr);"

    voff -= 32

    return (ret, voff)


def broadcast_and_write(lsize, voff, unroll_idx):
    comment = f"// unroll idx of {unroll_idx} with voff {voff}"
    vals = ", ".join(["'\\n'" if (x + 1) == voff else "0x28" for x in reversed(range(32))])
    bcv = f"_mm256_set_epi8({vals})"

    return """
        {}
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
		resv = _mm256_sub_epi8({}, resv);

		_mm256_store_si256((__m256i *) cursor, resv);
		cursor += 32;
        curr >>= 32;
    """.format(comment, bcv)

def loop_tail(unroll):
    return ("""
		if (i >= PIPECNT || paren == FIN) {
			currbuf = flush_buf(cursor - currbuf, currbuf);
			cursor = currbuf;
			i = 0;
            if (paren == FIN) {
                return;
            }
		}
        """
		f"i += {unroll};"
    )

def loop_pre():
    return """
	#if (PSIZE < 32)
	#error "batch is for lines longer than 32"
	#endif

	int i;
	__m256i resv;
	uint64_t curr;
	char *cursor, *currbuf;

	const __m256i shufmask = _mm256_set_epi64x(
					0x0303030303030303,
					0x0202020202020202,
					0x0101010101010101,
					0x0000000000000000);
	const __m256i andmask = _mm256_set1_epi64x(0x8040201008040201);

	cursor = buf;
	currbuf = buf;

	i = 0;
	curr = paren;
    """

def function_body(lsize):
    ret = loop_pre()
    prev_voff = 0
    voff = lsize
    ret += "do {"
    for idx in range(lsize):
        advance_paren, next_voff = set_curr(lsize, voff, prev_voff)
        ret += advance_paren
        ret += broadcast_and_write(lsize, voff, idx)
        ret += loop_tail(1)
        prev_voff = voff
        voff = next_voff

    ret += update_paren();
    ret += "\ncurr = paren;\n"
    ret += "} while (paren != FIN);\n"
    return ret

def function(lsize):
    return ("""
    static void
    do_batch_unrolled(uint64_t paren) {
    """
        f"{function_body(lsize)}"
    """
    }
    \n""")

def main():
    with open("unrolled.generated.h", "w") as fd:
        fd.write(function(41))

main()
