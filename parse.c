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
#include <time.h>

#define USING_TAM_STRINGS
#define TAM_STRINGS_IMPLEMENTATION
#define TAM_STRINGS_TEST
#define TAM_MEMORY_IMPLEMENTATION
#include <tam/memory.h>
#include <tam/strings.h>

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

i64 get_date(char *buf, size_t size, char *fmt) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    return strftime(buf, size, fmt, tm);
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

void makedir(const char * dir) {
    int mode = S_IRWXU | S_IRWXG | S_IRWXO;
    if (mkdir(dir, mode) == -1) {
        printf("Error creating directory: %s", strerror(errno));
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
    buffer = tam_allocate(char, *len+1);	
     
    /* copy all the text into the buffer */
    int items_read = fread(buffer, sizeof(char), *len, f);
    if (items_read < 1) {
        errorf("fread of file %s failed (%d items read)", filename, items_read);
    }
    close_file(f);

    return buffer;
}

// # end file utilities }}}

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
    tam_deallocate(d->node_data);
    tam_deallocate(d->cell_data);
    tam_deallocate(d->itp_data);
    tam_deallocate(d->cell_inds);
    for (int i = 0; i < d->num_node_vars; i++) {
        tam_deallocate(d->node_vars[i]);
    }
    for (int i = 0; i < d->num_cell_vars; i++) {
        tam_deallocate(d->cell_vars[i]);
    }
    for (int i = 0; i < d->num_itp_vars; i++) {
        tam_deallocate(d->itp_vars[i]);
    }
    tam_deallocate(d->node_vars);
    tam_deallocate(d->cell_vars);
    tam_deallocate(d->itp_vars);
    d->num_nodes = 0;
    d->num_cells = 0;
    d->num_node_vars = 0;
    d->num_cell_vars = 0;
    d->num_itp_vars = 0;
}

TecplotData read_tecplot_frame(Slice *file_contents) {
    Slice s = *file_contents;

    // strip first line (contains TITLE)
    slice_getline(&s);

    // static array of variables
    #define MAX_VARS 1024
    Slice variables[MAX_VARS];
    size_t num_vars = 0;

    // read first variable (begins after "VARIABLES=")
    Slice line = slice_getline(&s);
    slice_tok(&line, "=");               // line contains everything after '='
    variables[num_vars++] = reslice(line, 1, -1); // strip quotation marks

    // read remaining variables
    Slice zone = slice("ZONE");
    while (!sl_startswith((line = slice_getline(&s)), zone)) {
        variables[num_vars++] = reslice(slice_strip(line), 1, -1);
    }

    // parse zone string
    int num_nodes = -1, num_cells = -1, first_cell = -1, last_cell = -1;

    line = slice_strip(slice_suffix(line, zone.len));
    
    Slice pair;
    Slice sl_n = slice("N"), sl_e = slice("E"), sl_varloc = slice("VARLOCATION");
    // split into comma-separated key-value pairs and check against known keys
    while ((pair = slice_tok(&line, ", ")).len > 0) {
        Slice key = slice_tok(&pair, "="), val = pair;
        if (sl_eq(key, sl_n)) {
            num_nodes = atoi(val.buf);
        } else if (sl_eq(key, sl_e)) {
            num_cells = atoi(val.buf);
        } else if (sl_eq(key, sl_varloc)) {
            Slice varloc_str = reslice(val, 1, -1);
            varloc_str = reslice(slice_tok(&varloc_str, "="), 1, -1);
            Slice firstcell_str = slice_tok(&varloc_str, "-");
            first_cell = atoi(firstcell_str.buf) - 1;
            last_cell = atoi(varloc_str.buf) - 1;
        }
    }

    // allocate memory for data and cell connectivity info
    int num_node_vars = first_cell;
    int num_cell_vars = last_cell - first_cell + 1 + 2; // add 2 for z and r at start
    int cell_size = 4;
    double *node_data = tam_allocate(double, num_nodes * num_node_vars);
    double *cell_data = tam_allocate(double, num_cells * num_cell_vars);
    i64 *cell_connectivity = tam_allocate(i64, cell_size * num_cells);

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
    double *weights = tam_allocate(double, num_cells * cell_size);
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
            Slice tok = slice_tok(&line, " ");
            i64 node_ind = atoi(tok.buf) - 1; // indices are 1-indexed in tecplot files
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
    char **node_vars = tam_allocate(char*, num_node_vars);
    char **cell_vars = tam_allocate(char*, num_cell_vars);
    char **itp_vars  = tam_allocate(char*, num_itp_vars);

    // create list of nodal vars
    for (int i = 0; i < num_node_vars; i++) {
        Slice var = variables[i];
        node_vars[i] = tam_allocate(char, var.len+1);
        itp_vars[i]  = tam_allocate(char, var.len+1);
        memcpy(node_vars[i], var.buf, var.len);
        memcpy(itp_vars[i], var.buf, var.len);
    }

    // add z and r to front of array of cell-centerd vars
    Slice z = variables[z_ind];
    Slice r = variables[r_ind];
    cell_vars[0] = tam_allocate(char, z.len+1);
    cell_vars[1] = tam_allocate(char, r.len+1);
    memcpy(cell_vars[0], z.buf, z.len);
    memcpy(cell_vars[1], r.buf, r.len);

    // copy remaining cell-centered variables
    for (int i = 2; i < num_cell_vars; i++) {
        Slice var = variables[(i-2) + first_cell];
        cell_vars[i] = tam_allocate(char, var.len+1);
        memcpy(cell_vars[i], var.buf, var.len);

        int itp_ind = num_node_vars + i - 2;
        itp_vars[itp_ind] = tam_allocate(char, var.len+1);
        memcpy(itp_vars[itp_ind], var.buf, var.len);
    }

    // interpolate variables to cell centers
    // variable order is <node vars>, <cell vars>, skipping z and r in cell vars
    double *interp_data = tam_allocate(double, num_itp_vars * num_cells);
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
    tam_deallocate(weights);
    
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

void save_tecplot_data(
    TecplotData d,
    const char *output_dir,
    int frame,
    const char *original_path,
    int num_params,
    char **params
) {
    // assemble string using StringBuilder
    StringBuilder sb = sb_new();

    // create header: 
    char date_str[64];
    get_date(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S");

    sb_appendf(&sb,
        "# original file: %s\n"
        "# date generated : %s\n" 
        "# data kind = interpolated (all variables, interpolated to cell centers)\n"
        , original_path, date_str
    );

    if (num_params > 0) {
        sb_appendchars(&sb, "# parameters:\n");
        for (int i = 0; i < num_params; i++) {
            Slice val = slice(params[i]);
            Slice key = slice_tok(&val, "=");
            sb_appendf(&sb, "#    %.s: %.s\n", key.len, key.buf, val.len, val.buf);
        }
    }

    // TODO: allow specifying nodal or cell-centered instead
    i64 num_vars = d.num_itp_vars;
    char** vars = d.itp_vars;
    i64 num_pts = d.num_cells;
    double *data = d.itp_data;
    
    // write variable names
    for (int i = 0; i < num_vars; i++) {
        char dlm = i < num_vars-1 ? '\t' : '\n';
        sb_appendf(&sb, "%s%c", vars[i], dlm);
    }

    // write data
    for (int pt = 0; pt < num_pts; pt++) {
        for (int i = 0; i < num_vars; i++) {
            char dlm = i < num_vars - 1 ? '\t' : '\n';
            i64 index = i*num_pts + pt;
            sb_appendf(&sb, "%.5e%c", data[index], dlm);
        }
    }

    // build string
    char *contents = sb_tochars(sb);

    // cleanup stuff used to generate contents
    sb_deallocate(&sb);

    // write to file
    sb_appendf(&sb, "%s/output_%04d.txt", output_dir, frame);
    char *filename = sb_tochars(sb);
    printf("%s\n", filename);
    sb_deallocate(&sb);
    
    FILE *f = open_file(filename, "w");
    fputs(contents, f);
    close_file(f);

    tam_deallocate(filename);
    tam_deallocate(contents);
}

i64 process_tecplot_data(const char *path, const char *output_dir, int num_params, char **params) {
    size_t len;
    char *contents = read_file(path, &len);
    Slice str = slice_n(contents, len);

    int i = 0;
    while(str.len > 0) {
        TecplotData data = read_tecplot_frame(&str);
        save_tecplot_data(data, output_dir, i, path, num_params, params);
        free_tecplot_data(&data);
        i++;
    }

    tam_deallocate(contents);
    return i;
}

// # end tecplot parsing }}}

int main(int argc, char *argv[]) {
    const char *filename;
    if (argc > 1) {
        filename = argv[1];
    } else {
        return tam_test_strings();
    }

    const char *output_dir = ".";
    int first_param = 2;
    if (argc > 2) {
        // check for explicit output file specification
        if (strcmp(argv[2], "-o") == 0 || strcmp(argv[2], "--output") == 0) {
            if (argc > 3) {
                output_dir = argv[3];
                first_param += 2;
            } else {
                errorf("Missing argument after `-o` or `--output`");
            }
        }
    }
    int num_params = argc - first_param;
    
    i64 start_time = get_time_us();
    
    int frames = process_tecplot_data(filename, output_dir, num_params, argv + first_param);

    double elapsed_s = 1e-6 * (get_time_us() - start_time);

    printf("read %d frames in %.3e seconds\n", frames, elapsed_s);
}
