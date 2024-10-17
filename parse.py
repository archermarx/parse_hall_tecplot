import numpy as np

def strip_brackets(s, l, r = None):
    if r is None:
        r = l
    inside = False
    acc = ""
    for c in s:
        if inside:
            if c == r:
                break
            acc += c;
        else:
            if c == l:
                inside = True
    return acc

def parse(filename):
    # read file and skip first line
    f = open(filename, "r")
    f.readline()

    # read variables
    variables = []
    line = f.readline()
    while not line.startswith("ZONE"):
        variables.append(strip_brackets(line, '"'))
        line = f.readline()

    # strip ZONE off of zone line and parse info
    _, zoneinfo_str = line.split(" ", 1)
    zoneinfo = {}
    for field in (s.strip() for s in zoneinfo_str.split(",")):
        key, value = field.split("=", 1)
        zoneinfo[key] = value

    # get info from zone dict - num nodes and cell-centered locations
    num_nodes = int(zoneinfo["N"])
    cell_lo, cell_hi = map(int,
        strip_brackets(
            strip_brackets(zoneinfo["VARLOCATION"], "(", ")").split("=")[0],
            "[", "]").split("-")
    )

    num_nodal = cell_lo - 1
    num_cellcentered = len(variables) - num_nodal

    # read nodal data
    def read_data(n):   
        data = np.zeros(n)
        for i in range(n):
            data[i] = float(f.readline().strip())
        return data
    
    nodal_data = {}
    for i in range(num_nodal):
        nodal_data[variables[i]] = read_data(num_nodes)

    # record position - next line will be start of cell-centered data
    cell_pos = f.tell()

    # jump ahead to cell connectivity info and read it
    cells = []
    for line in f:
        spl = line.strip().split()
        if len(spl) == 1:
            continue
        cells.append(np.array([int(s) for s in spl]))

    num_cells = len(cells)
    cells = np.array(cells)

    # return to start of cell-centered data
    f.seek(cell_pos)

    # read cell-centered data
    cell_data = {}
    for i in range(cell_lo-1, cell_hi):
        cell_data[variables[i]] = read_data(num_cells)

    # get cell-centered coords
    # NOTE: this is not a true centroid, but is good enough for now
    z = nodal_data["z(m)"]
    r = nodal_data["r(m)"]

    z_c = np.zeros(num_cells)
    r_c = np.zeros(num_cells)

    for i in range(4):
        inds = cells[:, i] - 1
        cell_data[f"i{i}"] = inds
        z_c += 0.25 * z[inds]
        r_c += 0.25 * r[inds]

    cell_data["z(m)"] = z_c
    cell_data["r(m)"] = r_c

    f.close()
    return nodal_data, cell_data
