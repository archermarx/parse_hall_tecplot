//*** includes *** {{{
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

//}}}

//*** defines *** {{{
#define i64 int64_t

//}}}

//*** timing *** {{{

i64 get_time_us() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_usec + now.tv_sec * 1e6;
}

//}}}

//*** # Error handling *** {{{
void errorf(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    fprintf(stderr,  "\x1b[1;31m" "Error: " "\x1b[0m");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
    va_end(va);
    exit(1);
}
//}}}

//*** # File utilities *** {{{

/*
 * Same as fopen, but with error checking.
 */
FILE *open_file(const char *filename, const char *mode) {
    FILE *f = fopen(filename, mode);
    if (f == NULL) {
        errorf("file `%s` not found", filename);
    }
    return f;
}

/*
 * Same as fclose, but with error checking.
 */
void close_file(FILE *f) {
    int stat = fclose(f);
    if (stat == EOF) {
        errorf("could not close file");
    }
}

void makedir(const char * dir, bool exist_ok) {
    int mode = S_IRWXU | S_IRWXUG | S_IRWXO;
    if (mkdir(dir, mode) == -1) {
        printf("Error creating directory", strerror(errno));
    }
}

/*
 * Read a file into a string, allocating memory as needed.
 * Retuns the string, and sets len to the length of that string.
 * NOTE: only works on text files, or files that otherwise might not contain the '\0' byte.
 */
char* read_file(const char *filename, size_t *len) {
    FILE *f = open_file(filename, "r");
    char *buffer = NULL;
     
    // Get the number of bytes
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
     
    // Return to beginning of file
    rewind(f);
     
    // Allocate memory, including one extra for a NULL terminator
    buffer = calloc(*len+1, sizeof(char));	
     
    /* copy all the text into the buffer */
    fread(buffer, sizeof(char), *len, f);
    close_file(f);

    return buffer;
}

// # end file utilities }}}

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


//*** ### Slice construction *** {{{

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
 * Helper functions to get members (occasionally useful)
 */
i64 sl_len(slice_t sl) {
    return sl.len;
}

const char *sl_buf(slice_t sl) {
    return sl.buf;
}

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

// ### end slice construction }}}

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

/*
 * Return true if a slice starts with a given string, and false otherwise
 */
bool sl_startswith(slice_t s, slice_t x) {
    return strncmp(x.buf, s.buf, x.len) == 0;
}

/*
 * Return true if a slice starts with a given string, and false otherwise
 */
bool sl_startswithstr(slice_t s, const char *str) {
    return strncmp(str, s.buf, strlen(str)) == 0;
}

/*
 * Find index of first occurrance of slice `needle` in slice `haystack`
 * Returns length of `haystack` if `needle` not found
 */
i64 sl_find(slice_t haystack, slice_t needle) {
    i64 pos;
    // haystack:     "Hello, world!"
    // haystack inds: 0123456789ABC 
    // haystack.len: 13
    // if needle is "world!", then we need to advance the start pos up until 7
    // the needle length is 6, so we want to start the search at 7 = haystack.len - needle.len 
    for (pos = 0; pos <= (haystack.len - needle.len); pos++) {
        int i;
        for (i = 0; i < needle.len; i++) {
            if (sl_idx(needle, i) != sl_idx(haystack, pos+i)) {
                break;
            }
        }
        if (i == needle.len) return pos;
    }
    return haystack.len;
}

/*
 * Find index of first occurrance of string `needle` in slice `haystack`
 * Returns length of `haystack` if `needle` not found.
 * NOTE: `sl_findstr` first calls strlen on `needle`. To avoid this, use
 * `sl_findstrn`
 */
#define sl_findstr(haystack, needle) (sl_find(haystack, slice(needle)))
#define sl_findstrn(haystack, needle, needle_len) (sl_find(haystack, slice_n(needle, needle_len)))

/*
 * NOTE: modifies the input slice!!!
 * Read a line from the input slice, stripping newlines
 */
#define slice_getline(s) (slice_tok(s, "\r\n"))
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

        assert(sl_startswithstr(s1, "Hel"));
        assert(sl_startswithstr(s1, "Hello"));
        assert(!sl_startswithstr(s1, "Hello, world!!!!"));
        assert(!sl_startswithstr(s1, "hello"));

        assert(sl_startswith(s1, slice("Hel")));
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

        const char *paragraph = "Here's a sentence.\n"
                                "Here's another.\r\n"
                                "And here's one more!\r\n";
        slice_t par = slice(paragraph);
        assert(sl_eqstr(slice_getline(&par), "Here's a sentence."));
        assert(sl_eqstr(slice_getline(&par), "Here's another."));
        assert(sl_eqstr(slice_getline(&par), "And here's one more!"));
        assert(sl_eqstr(slice_getline(&par), ""));

    }
    {
        // finding
        const char *str = "word1 word2 word3 word4 wor5 word6";
        slice_t sl = slice(str);
        assert(sl_findstr(sl, "word") == 0);
        assert(sl_findstr(sl, "word1") == 0);
        assert(sl_findstr(sl, "word2") == 6);
        assert(sl_findstr(sl, "word3") == 12);
        assert(sl_findstr(sl, "word4") == 18);
        assert(sl_findstr(sl, "wor5") == 24);
        assert(sl_findstr(sl, "word5") == sl.len);
        assert(sl_findstr(sl, "word6") == 29);
        assert(sl_findstr(sl, "\0") == 0);
        assert(sl_findstr(sl, "") == 0);
    }
    printf("\x1b[1;32m" "Tests passed!" "\x1b[0m" "\n");
    return 0;
}
// ### end slice tests }}}
// ## end slices }}}

//*** ## String builders *** {{{
/*
 * These are linked lists of slices, used when constructing a string from many smaller parts.
 * Their methods are prefixed by `sb_`;
 * String builders are constructed from some initial slice or string using `sb_fromslice` or `sb_fromchars`, 
 * or from scratch using `sb_new`. Note that these creation methods return pointers.
 * They can then can be appended to repeatedly using `sb_append`.
 * A string builder can then be converted to a string (char*) by calling `sb_tochars` on the builder.
 * The returned char* is heap-allocated and must be freed by the user.
 * They do not own the string data they contain, but do allocate memory to build a linked list.
 * The user is responsible for freeing the StringBuilder using sb_free when they are done with it.
 */
typedef struct StringBuilder {
    slice_t slice;
    struct StringBuilder *next;
} StringBuilder;

/*
 * Allocate memory for a StringBuilder
 */
StringBuilder *sb_alloc() {
    return calloc(1, sizeof(StringBuilder));
}

/*
 * Create an empty StringBuilder
 */
StringBuilder *sb_new() {
    StringBuilder sb = sb_alloc();
    *sb = (StringBuilder){.slice = {.buf = NULL, .len = 0}, .next = NULL};
    return sb;
}

/*
 * Create a StringBuilder from a slice 
 */
StringBuilder *sb_fromslice(slice_t sl) {
    StringBuilder *sb = sb_alloc();
    sb->slice = sl;
    sb->next = NULL;
    return sb;
}

/*
 * Create a StringBuilder from a char*
 */
StringBuilder *sb_fromchars(const char *s) {
    StringBuilder *sb = sb_alloc();
    sb->slice = slice(s);
    sb->next = NULL;
    return sb;
}

/*
 * Append a slice to a StringBuilder
 */
StringBuilder *sb_appendslice(StringBuilder *sb, slice_t sl) {
    if (sb->slice.buf == NULL) {
        sb->slice = sl;
    } else {
        sb->next = sb_fromslice(sl);
    }
}

/*
 * Append chars to a StringBuilder
 */
StringBuilder *sb_appendchars(StringBuilder *sb, const char *s) {
    return sb_appendslice(sb, slice(s));
}

/*
 * Free a stringbuilder instance
 */ 
void sb_free(StringBuilder *sb) {
    if (sb == NULL) return;
    if (sb->next != NULL) {
        sb_free(sb->next);
        sb->next = NULL;
    }
    free(sb);
}



// ## end string builders }}}

// # end string manipulation utilities }}}

//*** # Tecplot parsing *** {{{

typedef struct TecplotData {
    i64 num_cell_vars;
    i64 num_node_vars;
    i64 num_itp_vars;
    char **cell_vars;
    char **node_vars;
    char **itp_vars;
    i64 num_nodes;
    i64 num_cells;
    double *node_data;
    double *cell_data;
    double *itp_data;
    i64 cell_size;
    i64 *cell_inds;
} TecplotData;

void free_tecplot_data(TecplotData *d) {
    #define FREE(d) (free(d), (d)=NULL)
    FREE(d->node_data);
    FREE(d->cell_data);
    FREE(d->itp_data);
    FREE(d->cell_inds);
    for (int i = 0; i < d->num_node_vars; i++) {
        FREE(d->node_vars[i]);
    }
    for (int i = 0; i < d->num_cell_vars; i++) {
        FREE(d->cell_vars[i]);
    }
    for (int i = 0; i < d->num_itp_vars; i++) {
        FREE(d->itp_vars[i]);
    }
    FREE(d->node_vars);
    FREE(d->cell_vars);
    FREE(d->itp_vars);
    d->num_nodes = 0;
    d->num_cells = 0;
    d->num_node_vars = 0;
    d->num_cell_vars = 0;
    d->num_itp_vars = 0;
    #undef FREE
}

TecplotData read_tecplot_frame(slice_t *file_contents) {
    slice_t s = *file_contents;

    // strip first line (contains TITLE)
    slice_getline(&s);

    // static array of variables
    #define MAX_VARS 1024
    slice_t variables[MAX_VARS];
    size_t num_vars = 0;

    // read first variable (begins after "VARIABLES=")
    slice_t line = slice_getline(&s);
    slice_tok(&line, "=");               // line contains everything after '='
    variables[num_vars++] = reslice(line, 1, -1); // strip quotation marks

    // read remaining variables
    slice_t zone = slice("ZONE");
    while (!sl_startswith((line = slice_getline(&s)), zone)) {
        variables[num_vars++] = reslice(slice_strip(line), 1, -1);
    }

    // parse zone string
    int num_nodes = -1, num_cells = -1, first_cell = -1, last_cell = -1;

    line = slice_strip(slice_suffix(line, zone.len));
    
    slice_t pair;
    slice_t sl_n = slice("N"), sl_e = slice("E"), sl_varloc = slice("VARLOCATION");
    // split into comma-separated key-value pairs and check against known keys
    while ((pair = slice_tok(&line, ", ")).len > 0) {
        slice_t key = slice_tok(&pair, "="), val = pair;
        if (sl_eq(key, sl_n)) {
            num_nodes = atoi(val.buf);
        } else if (sl_eq(key, sl_e)) {
            num_cells = atoi(val.buf);
        } else if (sl_eq(key, sl_varloc)) {
            slice_t varloc_str = reslice(val, 1, -1);
            varloc_str = reslice(slice_tok(&varloc_str, "="), 1, -1);
            slice_t firstcell_str = slice_tok(&varloc_str, "-");
            first_cell = atoi(firstcell_str.buf) - 1;
            last_cell = atoi(varloc_str.buf) - 1;
        }
    }

    // allocate memory for data and cell connectivity info
    int num_node_vars = first_cell;
    int num_cell_vars = last_cell - first_cell + 1 + 2; // add 2 for z and r at start
    int cell_size = 4;
    double *node_data = calloc(num_nodes * num_node_vars, sizeof(double));
    double *cell_data = calloc(num_cells * (num_cell_vars), sizeof(double));
    i64 *cell_connectivity = calloc(cell_size * num_cells, sizeof(i64));

    // read nodal variables
    for (int i = 0; i < num_node_vars*num_nodes; i++) {
        line = slice_getline(&s);
        node_data[i] = atof(line.buf);
    }

    // read cell variables, starting from after z and r (we'll add these next)
    for (int i = 2*num_cells; i < num_cell_vars*num_cells; i++) {
        line = slice_getline(&s);
        cell_data[i] = atof(line.buf);
    }
    
    // read cell_connectivity info and compute cell centers and node-to-cell interpolation weights
    double *weights = calloc(num_cells * cell_size, sizeof(double));
    double inv_n = 1.0 / (double)cell_size;
    double wt[4];
    double zn[4];
    double rn[4];
    // NOTE: we are assuming z and r are the first two nodal variables
    int z_ind = 0;
    int r_ind = 1;
    for (int i = 0; i < num_cells; i++) {
        line = slice_lstrip(slice_getline(&s));
        double sumwt = 0.0;
        double z_cell = 0.0;
        double r_cell = 0.0;

        // get connectivity info and compute cell centers
        for (int j = 0; j < cell_size; j++) {
            slice_t tok = slice_tok(&line, " ");
            i64 node_ind = atoi(tok.buf);
            cell_connectivity[j + cell_size*i] = node_ind;
            zn[j] = node_data[z_ind*num_nodes + node_ind];
            rn[j] = node_data[r_ind*num_nodes + node_ind];
            z_cell += zn[j] * inv_n;
            r_cell += rn[j] * inv_n;
        }
        
        // add z_cell and r_cell to cell_data
        cell_data[0*num_cells + i] = z_cell;
        cell_data[1*num_cells + i] = r_cell;

        // compute interpolation weights for later use
        for (int j = 0; j < cell_size; j++) {
            double dz = zn[j] - z_cell;
            double dr = rn[j] - r_cell;
            double dist2 = dz*dz + dr*dr;
            wt[j] = 1.0 / dist2;
            sumwt += wt[j];
        }

        // divide by sum of weights
        for (int j = 0; j < cell_size; j++) {
            wt[j] /= sumwt;
            weights[i*cell_size + j] = wt[j];
        }
    }

    // allocate separate arrays of nodal and cell-centered vars 
    int num_itp_vars = num_node_vars + num_cell_vars - 2;
    char **node_vars = calloc(num_node_vars, sizeof(char*));
    char **cell_vars = calloc(num_cell_vars, sizeof(char*));
    char **itp_vars = calloc(num_itp_vars, sizeof(char*));

    // create list of nodal vars
    for (int i = 0; i < num_node_vars; i++) {
        slice_t var = variables[i];
        node_vars[i] = calloc(var.len+1, sizeof(char));
        itp_vars[i] = calloc(var.len+1, sizeof(char));
        memcpy(node_vars[i], var.buf, var.len);
        memcpy(itp_vars[i], var.buf, var.len);
    }

    // add z and r to front of array of cell-centerd vars
    slice_t z = variables[z_ind];
    slice_t r = variables[r_ind];
    cell_vars[0] = calloc(z.len+1, sizeof(char));
    cell_vars[1] = calloc(r.len+1, sizeof(char));
    memcpy(cell_vars[0], z.buf, z.len);
    memcpy(cell_vars[1], r.buf, r.len);

    // copy remaining cell-centered variables
    for (int i = 2; i < num_cell_vars; i++) {
        slice_t var = variables[(i-2) + first_cell];
        cell_vars[i] = calloc(var.len+1, sizeof(char));
        memcpy(cell_vars[i], var.buf, var.len);

        int itp_ind = num_node_vars + i - 2;
        itp_vars[itp_ind] = calloc(var.len+1, sizeof(char));
        memcpy(itp_vars[itp_ind], var.buf, var.len);
    }

    // interpolate variables to cell centers
    // variable order is <node vars>, <cell vars>, skipping z and r in cell vars
    double *interp_data = calloc(num_itp_vars * num_cells, sizeof(double));
    memcpy(interp_data, cell_data, 2*num_cells*sizeof(double)); // copy z and r
    
    // interpolate nodal data to cell centers
    for (int var = 2; var < num_node_vars; var++) {
        for (int cell = 0; cell < num_cells; cell++) {
            double cell_val = 0.0;
            for (int i = 0; i < cell_size; i++) {
                int node_ind = cell_connectivity[i + cell*cell_size];
                double weight = weights[i + cell*cell_size];
                double node_val = node_data[node_ind + var * num_nodes];
                cell_val += weight * node_val;
            }
            interp_data[cell + var * num_cells] = cell_val;
        }
    }

    // copy remaining cell data
    memcpy(&interp_data[num_node_vars*num_cells], &cell_data[2*num_cells], (num_cell_vars-2)*num_cells * sizeof(double));

    // clean up
    free(weights);
    
    // set output parameter
    *file_contents = s;

    return (TecplotData) {
        .num_node_vars = num_node_vars,
        .node_vars = node_vars,
        .num_cell_vars = num_cell_vars,
        .cell_vars = cell_vars,
        .num_itp_vars = num_itp_vars,
        .itp_vars = itp_vars,
        .cell_size = cell_size,
        .cell_inds = cell_connectivity,
        .num_nodes = num_nodes,
        .node_data = node_data,
        .num_cells = num_cells,
        .cell_data = cell_data,
        .itp_data = interp_data,
    };

    #undef MAX_VARS
}

void save_tecplot_data(TecplotData d, const char *path, int frame) {
   // FILE *f = open_file(path, "w");
   // close_file(f);
}

i64 process_tecplot_data(const char *path) {
    size_t len;
    char *contents = read_file(path, &len);
    slice_t str = slice_n(contents, len);

    int i = 0;
    while(str.len > 0) {
        TecplotData data = read_tecplot_frame(&str);
        save_tecplot_data(data, "output", frame);
        free_tecplot_data(&data);
        i++;
    }

    free(contents);
    return i;
}

// # end tecplot parsing }}}


int main(int argc, char *argv[]) {

    const char *filename;
    if (argc > 1) {
        filename = argv[1];
    } else {
        return test_slices();
    }
    
    i64 start_time = get_time_us();
    int frames = read_tecplot_file(filename);
    double elapsed_s = 1e-6 * (get_time_us() - start_time);

    printf("read %d frames in %.3e seconds\n", frames, elapsed_s);
}
