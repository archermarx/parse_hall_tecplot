#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void errorf(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    fprintf(stderr,  "\x1b[1;31m" "Error: " "\x1b[0m");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
    va_end(va);
    exit(1);
}

FILE *tam_fopen(const char *filename, const char *mode) {
    FILE *f = fopen(filename, mode);
    if (f == NULL) {
        errorf("file `%s` not found", filename);
    }
    return f;
}

void tam_fclose(FILE *f) {
    int stat = fclose(f);
    if (stat == EOF) {
        errorf("could not close file");
    }
}

ssize_t readline(FILE *f, char *buf, size_t bufsize) {
    char *result = fgets(buf, bufsize, f);
    if (result == NULL) {
        return -1;
    } 
    size_t len = strlen(buf);
    // strip newline and carriage return, if present
    if (buf[len-1] == '\n' || buf[len-1] == '\r') {
        buf[len-1] = '\0';
        len --;
        if (buf[len-1] == '\r') {
            buf[len-1] = '\0';
            len--;
        }
    }

    return len;
}

typedef struct Slice {
    char *data;
    int start;
    int len;
} Slice;

Slice slice(char *s, int start, int end) {
    return (Slice){.data = s, .start = start, .len = end-start};
}

Slice reslice(Slice s, int start, int end) {
    return (Slice){.data = s.data, .start = start + s.start, .len = end-start};
}

Slice strip_brackets(Slice s, char left, char right) {
    Slice s2 = (Slice){.data = s.data, .start = s.start, .len = s.len};
    int i0 = s2.start;

    // find left dlm 
    int i1;
    for (i1 = 0; i1 < s.len && s2.data[i0 + i1] != left; i1++) {}
    if (i1 == s.len) return s2; // no dlm found
    i1 ++;
    //printf("i1 = %d\n", i1);

    // find right dlm
    int i2;
    for (i2 = s.len-1; i2 >= i1 && s2.data[i0 + i2] != right; i2--) {}
    //printf("i2 = %d\n", i2);
    if (i2 == i1) return s2; // only one dlm found or wrong order

    s2.start = i0 + i1;
    s2.len = i2 - i1;
    return s2;
}

bool startswith(char *str, char *test) {
    size_t str_len = strlen(str);
    size_t test_len = strlen(test);
    if (str_len < test_len) {
        return false;
    }
    bool found = true;
    for (size_t i = 0; i < strlen(test); i++) {
        if (test[i] != str[i]) {
            found = false;
            break;
        }
    }
    return found;
}

int find(char *str, char c, int start, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if (str[start + i] == c) {
            break;
        }
    }
    return i;
}

int findslice(Slice str, char c) {
   return find(str.data, c, str.start, str.len);
}

void trim_l(Slice *str) {
    int end = str->start + str->len;
    while (str->len > 0 && isspace(str->data[str->start])) {
        str->start++;
    }
    str->len = end - str->start;
}  

int consume_key_val(Slice *str, char *key, char *val) {
    // find comma or end of string
    Slice head = *str;
    int i;
    for (i = 0; i < head.len; i++) {
        if (head.data[head.start+i] == ',') break;
    }
    head.len = i;
    if (head.len == 0) {
        return -1;
    }
    
    int end = str->start + str->len;
    str->start = str->start + head.len + 1;
    str->len = end - str->start;
    // advance past whitespace
    trim_l(str);
   
    Slice k = head, v = head;
    int eq = findslice(head, '=');
    
    k.len = eq;
    end = v.start + v.len;
    v.start = v.start+eq+1; 
    v.len = end - v.start;

    memcpy(key, k.data+k.start, k.len);
    key[k.len] = '\0';
    memcpy(val, v.data+v.start, v.len);
    val[v.len] = '\0';

    return 0;
}

int main(int argc, char *argv[]) {
    char *filename = NULL;
    if (argc > 1) {
        filename = argv[1];
    } else {
        return 1;           
    }

    // read file
    #define BUFSIZE 256
    char buffer[BUFSIZE];
    int len;
    FILE *f = tam_fopen(filename, "r");
    
    // strip title;
    readline(f, buffer, BUFSIZE);

    // strip "VARIABLES="
    len = readline(f, buffer, BUFSIZE);
    int i;
    for (i = 0; buffer[i] != '=' && i < len; i++) {}
    if (i >= len) errorf("expected not find `=` in second line (variables)");
    i++;

    // strip quotation marks
    Slice s = strip_brackets(slice(buffer, i, len), '"', '"');

    // read variables
    int num_vars = 1;
    int capacity = 8;
    char **variables = malloc(capacity * sizeof(char*));
    char *var = calloc(s.len+1, 1);
    memcpy(var, s.data+s.start, s.len);
    variables[0] = var;

    while (1) {
        len = readline(f, buffer, BUFSIZE);
        if (startswith(buffer, "ZONE")) {
            break;
        }

        s = strip_brackets(slice(buffer, 0, len), '"', '"');

        var = calloc(s.len+1, 1);
        memcpy(var, s.data+s.start, s.len);

        if (num_vars >= capacity) {
            capacity *= 2;
            variables = realloc(variables, capacity * sizeof(char*));
        }

        variables[num_vars] = var;  
        num_vars++;
    }

    // read zone info line
    int num_nodes;
    int num_edges;
    int cell_start;
    int cell_end;

    int start = 5; // start after NODE
    
    // read key-value pairs
    s = slice(buffer, start, len);
    int stat;
    char key[64], val[64];
    do {
        stat = consume_key_val(&s, key, val);
        if (stat != 0) break;

        if (strcmp(key, "N") == 0) {
            num_nodes = atoi(val);
        } else if (strcmp(key, "E") == 0) {
            num_edges = atoi(val);
        } else if (strcmp(key, "VARLOCATION") == 0) {
            Slice v = slice(val, 0, strlen(val));
            v = strip_brackets(v, '(', ')');
            v = reslice(v, 0, findslice(v, '='));
            v = strip_brackets(v, '[', ']');
            int dash = findslice(v, '-');
            char l[3], r[3];
            Slice left = reslice(v, 0, dash);
            Slice right = reslice(v, dash+1, v.len);
            memcpy(l, left.data + left.start, left.len);
            memcpy(r, right.data + right.start, right.len);
            cell_start = atoi(l);
            cell_end = atoi(r);
        }
    } while (stat == 0);

    printf("nodes = %d\n", num_nodes);
    printf("edges = %d\n", num_edges);
    printf("cell start = %d\n", cell_start);
    printf("cell end = %d\n", cell_end);

    // read nodal variables
    // size: num_nodes x num_nodal
    int num_nodal = cell_start - 1;
    bool *nodal_all_zeros = calloc(num_nodal, sizeof(bool));
    double *nodal_data = calloc(num_nodes * num_nodal, sizeof(double*));
    for (int j = 0; j < num_nodal; j++) {
        double max = -1e30, min = 1e30;
        for (int i = 0; i < num_nodes; i++) {
            len = readline(f, buffer, BUFSIZE);
            double val = strtof(buffer, NULL);
            nodal_data[i + num_nodes*j] = val;
            if (val > max) max = val;
            if (val < min) min = val;
        }
        if (max == 0 && min == 0) {
            nodal_all_zeros[j] = true;
        } 
    }

    // record current position and advance until we find the cell connectivity
    
    // return to prev position and read cell data
    int num_cellcentered = cell_end - cell_start + 1;
    int num_cells = 0;
    bool *cell_all_zeros = calloc(num_cellcentered, sizeof(bool));
    double *cell_data = calloc(num_cells * num_cellcentered, sizeof(double*));
    for (int j = 0; j < num_cellcentered; j++) {
        double max = -1e30, min = 1e30;
        for (int i = 0; i < num_cells; i++) {
            len = readline(f, buffer, BUFSIZE);
            double val = strtof(buffer, NULL);
            cell_data[i + num_cells*j] = val;
            if (val > max) max = val;
            if (val < min) min = val;
        }
        if (max == 0 && min == 0) {
            cell_all_zeros[j] = true;
        } 
    }

    // close file
    tam_fclose(f);

    // write nodal data to file
    f = tam_fopen("nodal.csv", "w");
    // write headers
    for (int j = 0; j < num_nodal; j++) {
        fprintf(f, "\"%s\"", variables[j]);
        if (j == num_nodal - 1) {
            fprintf(f, "\n");
        } else {
            fprintf(f, "\t");
        }
    }
    // write data
    for (int i = 0; i < num_nodes; i++) {
        for (int j = 0; j < num_nodal; j++) {
            if (nodal_all_zeros[j]) continue;
            fprintf(f, "%.12e", nodal_data[i + num_nodal*j]);
            if (j == num_nodal - 1) {
                fprintf(f, "\n");
            } else {
                fprintf(f, "\t");
            }
        }
    }
    tam_fclose(f);

    // write cell-centered data to file
    f = tam_fopen("cellcentered.csv", "w");

    tam_fclose(f);


    // free memory
    for (int i = 0; i < num_vars; i++) {
        free(variables[i]);
    }
    free(variables);
    free(nodal_data);
    free(nodal_all_zeros);
    free(cell_data);
    free(cell_all_zeros);

    return 0;
}
