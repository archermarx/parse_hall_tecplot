//*** includes *** {{{
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
//}}}

//*** defines *** {{{
#define i64 int64_t

//}}}

//*** # String manipulation utilities *** {{{
//*** ## Slices *** {{{
/*
 * These are non-owning views on string data that store a pointer to the underlying data
 * as well as a length. These are convenient for any string manipulation tasks that do not
 * require that the underlying data is changed or any new memory to be allocated.
 * The underlying char data is expected to be null-terminated, as we use libc string functions
 * for many utility functions.
 * 
 * A note on naming conventions -- functions which create slices are all prefixed by `slice_`,
 * while those that operate on slices but return some other type are prefixed by `sl_`.
 * One exception is `reslice`, which creates a slice but has neither of these prefixes.
 * Additionally, it can always be assumed that any functions taking in a slice pointer (slice_t*)
 * will modify their input, so act accordingly.
 */
typedef struct slice_t {
    i64 len;
    const char *buf;
} slice_t;

/*
 * Construct a slice from a raw char* buf.
 * This calls strlen to get the length, so `buf` must be null-terminated.
 * If you have already computed the length or otherwise do want to rely on `strlen`, use `slice_len` below.
 */
#define slice(buf) (slice_t){strlen(buf), buf}

/*
 * Construct a slice from a raw char* buf and a length.
 */
#define slice_n(buf, len) (slice_t){len, buf}

/*
 * Helper function to convert an index i into one that can be used in a slice.
 * This handles negative indices to allow python-style wraparound indexing,
 * Negative indices count backward from the end of the array (-n -> len - abs(n))
 */
i64 get_slice_index(i64 i, i64 len) {
    i64 j = i >= 0 ? i : len + i;
    assert(0 <= j && j <= len);
    return j;
}

/*
 * Obtain the character at index i in a slice.
 * This does not return a pointer to the character, so it cannot be used to modify the underlying memory.
 */
char sl_idx(slice_t s, i64 i) {
    return s.buf[get_slice_index(i, s.len)];
}

/*
 * Construct a slice from an existing slice and a start and stop index
 * We follow python and go conventions here, so the characters obtained are [i, j)
 */
slice_t reslice(slice_t s, i64 i, i64 j) {
    i = get_slice_index(i, s.len);
    j = get_slice_index(j, s.len);
    assert(i <= j);
    return slice_n(s.buf + i, j - i);
}

/*
 * Construct a subslice using the implicit indices [0, i)
 * Equivalent to s[:i] in python
 */
slice_t slice_prefix(slice_t s, i64 i) {
    return slice_n(s.buf, get_slice_index(i, s.len));
}

/*
 * Construct a subslice using the implicit indices [i, len(s))
 * Equivalent to s[i:] in python
 */
slice_t slice_suffix(slice_t s, i64 i) {
    i64 j = get_slice_index(i, s.len);
    return slice_n(s.buf + j, s.len - j);
}

//*** ### Slice utility functions *** {{{

/*
 * Check for literal equivalence between two slices,
 * i.e. whether they point to the same memory and have the same length.
 */
bool sl_eqv(slice_t s1, slice_t s2) {
    return s1.buf == s2.buf && s1.len == s2.len;
}

/*
 * Check if two slices are equal on a byte-by-byte comparison.
 * They may have different addresses.
 */
bool sl_eq(slice_t s1, slice_t s2) {
    if (s1.len != s2.len) return false;
    if (s1.buf == s2.buf) return true;
    return strncmp(s1.buf, s2.buf, s1.len) == 0;
}

/*
 * Check if the data contained in a slice is equal to a char*
 */
bool sl_eqstr(slice_t s, const char *c) {
    return strncmp(s.buf, c, s.len) == 0;
}

/*
 * NOTE: modifies the input slice!!!
 * Remove leading whitespace from a slice.
 * Return index of first char after leading spaces in original slice.
 */
i64 sl_lstrip(slice_t *s) {
    i64 i;
    for (i = 0; i < s->len && isspace(sl_idx(*s, i)); i++);
    *s = slice_suffix(*s, i);
    return i;
}

/*
 * Create a slice by stripping leading whitespace from input slice
 */
slice_t slice_lstrip(slice_t s) {
    sl_lstrip(&s);
    return s;
}

/*
 * NOTE: modifies the input slice!!!
 * Remove trailing whitespace from a slice.
 * Return the index of first trailing space in original slice.
 */
i64 sl_rstrip(slice_t *s) {
    i64 i;
    for (i = s->len - 1; i >= 0 && isspace(sl_idx(*s, i)); i--);
    i++;
    *s = slice_prefix(*s, i);
    return i;
}

/*
 * Create a slice by stripping trailing whitespace from input slice.
 */
slice_t slice_rstrip(slice_t s) {
    sl_rstrip(&s);
    return s;
}

/*
 * NOTE: modifies the input slice!!!
 * Remove leading and trailing whitespace from a slice.
 * Return the number of bytes removed.
 */
i64 sl_strip(slice_t *s) {
    i64 orig_len = s->len;
    sl_lstrip(s);
    sl_rstrip(s);
    return orig_len - s->len;
}

/*
 * Create a slice by stripping leading and trailing whitespace from input slice.
 */
slice_t slice_strip(slice_t s) {
    sl_strip(&s);
    return s;
}

/*
 * Scan a slice, looking for first occurrance of any bytes that are part of `reject`.
 * Return the number of bytes read before the first occurance.
 * Return the length of the string if no bytes in `reject` is not found. 
 */
i64 sl_cspan(slice_t s, const char *reject) {
    i64 idx = strcspn(s.buf, reject);
    if (idx > s.len) idx = s.len;
    return idx;
}

/*
 * Scan a slice, looking for first occurance of any bytes not in `accept`.
 * Return the number of bytes read before the first occurrance.
 * Return the length of the string if all bytes are found in `accept`.
 */
i64 sl_span(slice_t s, const char *accept) {
    i64 idx = strspn(s.buf, accept);
    if (idx > s.len) idx = s.len;
    return idx;
}

/*
 * NOTE: modifies the input slice!!!
 * Scan a slice until the first byte contained in `delimiters` is reached.
 * Then, return a slice of all bytes read up until that point.
 * Finally, strip any delimiter bytes from the front of the input slice.
 */
slice_t slice_tok(slice_t *s, const char *delimiters) {
    i64 prefix_size = sl_cspan(*s, delimiters);
    slice_t token = slice_prefix(*s, prefix_size);
    slice_t suffix = slice_suffix(*s, prefix_size);
    
    assert(token.len + suffix.len == s->len);

    i64 dlm_size = sl_span(suffix, delimiters);
    *s = slice_suffix(suffix, dlm_size);
    return token;
}

// ### end slice utility functions }}}

//*** ### Slice tests *** {{{
int test_slices() {
    {
        slice_t s1 = slice("Hello, world!");
        assert(sl_idx(s1, 0) == 'H');
        assert(sl_idx(s1, 1) == 'e');
        assert(sl_idx(s1, -1) == '!');
        assert(sl_idx(s1, -2) == 'd');
        assert(sl_eqstr(s1, "Hello, world!"));

        slice_t hello = slice_prefix(s1, 5);
        assert(sl_idx(hello, 0) == 'H');
        assert(sl_idx(hello, 4) == 'o');
        assert(sl_idx(hello, -1) == 'o');
        assert(sl_idx(hello, -2) == 'l');
        assert(hello.len == 5);
        assert(sl_eqstr(hello, "Hello"));

        slice_t world = slice_suffix(s1, 7);
        assert(sl_idx(world, 0) == 'w');
        assert(sl_idx(world, -1) == '!');
        assert(world.len == 6);
        assert(sl_eqstr(world, "world!"));

        slice_t llo = reslice(s1, 2, 5);
        assert(sl_idx(llo, 0) == 'l');
        assert(sl_idx(llo, -1) == 'o');
        assert(llo.len == 3);

        slice_t llo2 = slice_suffix(hello, 2);
        assert(llo.len == llo2.len);
        assert(llo.buf == llo2.buf);
        assert(sl_eqv(llo2, llo));
        assert(sl_eq(llo2, llo));
        
        // equality
        slice_t llo3 = slice("llo");
        assert(!sl_eqv(llo, llo3));
        assert(sl_eq(llo, llo3));
        assert(!sl_eq(llo3, hello));
        assert(!sl_eq(llo3, slice("ll")));
        assert(!sl_eq(llo3, slice("llo3")));

        // finding chars and tokenizing
        assert(sl_cspan(s1, ",") == 5);
        assert(sl_cspan(s1, "0") == s1.len);
        assert(sl_cspan(s1, " ") == 6);
        assert(sl_cspan(hello, "w") == hello.len);
    }
    {
        // stripping whitespace
        slice_t sl = slice("    a string with spaces\t ");
        slice_t sl2 = sl, sl3 = sl, sl4 = sl;
        assert(sl_eqv(sl, sl2) && sl_eq(sl, sl2));
        i64 leading = sl_lstrip(&sl2);
        assert(leading == 4);
        assert(sl_eq(sl2, slice_suffix(sl, leading)));
        assert(sl_eq(sl2, slice_lstrip(sl)));
        assert(sl_eq(sl2, reslice(sl, leading, sl.len)));
        assert(sl_eq(sl2, slice_lstrip(sl2)));

        i64 trailing = sl_rstrip(&sl3);
        assert(trailing == 24);
        assert(sl_eq(sl3, slice_prefix(sl, trailing)));
        assert(sl_eq(sl3, slice_rstrip(sl)));
        assert(sl_eq(sl3, reslice(sl, 0, trailing)));
        assert(sl_eq(sl3, slice_rstrip(sl3)));

        i64 stripped = sl_strip(&sl4);
        assert(stripped == 6);
        assert(sl_eq(sl4, reslice(sl, leading, trailing)));
        assert(sl_eq(sl4, slice_strip(sl)));
        assert(sl_eq(sl4, slice_strip(sl4)));
        assert(sl_eq(sl4, slice_lstrip(sl3)));
        assert(sl_eq(sl4, slice_rstrip(sl2)));
    }
    {
        // Tokenizing
        const char *sentence = "a few words to check, with punctuation.";
        slice_t words = slice(sentence);
        const char *dlm = ",. ";
        assert(sl_eqstr(slice_tok(&words, dlm), "a"));
        assert(sl_eqstr(slice_tok(&words, dlm), "few"));
        assert(sl_eqstr(slice_tok(&words, dlm), "words"));
        assert(sl_eqstr(slice_tok(&words, dlm), "to"));
        assert(sl_eqstr(slice_tok(&words, dlm), "check"));
        assert(sl_eqstr(slice_tok(&words, dlm), "with"));
        assert(sl_eqstr(slice_tok(&words, dlm), "punctuation"));
        assert(sl_eqstr(slice_tok(&words, dlm), ""));
        assert(words.len == 0);
        assert(words.buf == sentence + strlen(sentence));
        assert(*words.buf == '\0');



    }
    printf("\x1b[1;32m" "Tests passed!" "\x1b[0m" "\n");
    return 0;
}
// ### end slice tests }}}
// ## end slices }}}
// # end string manipulation utilities }}}






int main() {
    return test_slices();
}
