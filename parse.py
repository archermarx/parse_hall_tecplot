import numpy as np

def parse_tecplot(filename, startpos = 0):
    """
    Parse a tecplot file from Hall2De, extracting nodal and cell-centered data
    """
    # read file and skip first line containing "title"
    f = open(filename, "rb")
    if (startpos == 0):
        line = f.readline()

    def strip_brackets(s, l, r = None):
        """
        for a string of the form str = "this is a {string}!"
        return "string" is strip_brackets(str, '{', '}')
        """
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

    # read variables
    variables = []
    line = f.readline().decode("utf-8")
    while not line.startswith("ZONE"):
        variables.append(strip_brackets(line, '"'))
        line = f.readline().decode("utf-8")

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

    def read_data(n):   
        data = np.zeros(n)
        for i in range(n):
            data[i] = float(f.readline().strip())

        if np.any(data):
            return data
    
    # read nodal data, omitting columns that are empty/all zero
    nodal_data = {}
    for i in range(num_nodal):
        data = read_data(num_nodes)
        if data is not None:
            nodal_data[variables[i]] = data

    # record position - next line will be start of cell-centered data
    cell_pos = f.tell()

    # jump ahead to cell connectivity info and read it
    nextframe_pos = -1
    cells = []
    for line in f:
        if line.decode("utf-8").startswith("TITLE"):
            # this is a movie file, the next frame starts here
            nextframe_pos = f.tell()
            break
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
        data = read_data(num_cells)
        if data is not None:
            cell_data[variables[i]] = data

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
    
    # reorder cell vars so geometry starts first
    cell_vars = ["z(m)", "r(m)", "i0", "i1", "i2", "i3"]
    cell_vars += list(cell_data.keys())[6:]
    cell_data = {k: cell_data[k] for k in cell_vars}

    f.close()
    return nodal_data, cell_data, nextframe_pos

def interp_to_cells(nodal_data, cell_data):
    """
    Interpolate all nodal variables to cell centers, insert into cell-centered data dictionary
    """
    inds = [cell_data[f"i{i}"] for i in range(4)]
    
    zc = cell_data["z(m)"]
    rc = cell_data["r(m)"]
    
    # compute interpolation weights
    dzs = [(nodal_data["z(m)"][i] - zc)**2 for i in inds]
    drs = [(nodal_data["r(m)"][i] - rc)**2 for i in inds]
    wts = [1 / (dz + dr) for (dz, dr) in zip (dzs, drs)]
    sumwt = sum(wts)
    wts = np.array([w / sumwt for w in wts])

    # create new dict 
    itp = {"z(m)": zc, "r(m)": rc}

    # interpolate nodal vars to cell centers
    for k in nodal_data.keys():
        v0, v1, v2, v3 = [nodal_data[k][i] for i in inds]
        itp[k] = wts[0]*v0 + wts[1]*v1 + wts[2]*v2 + wts[3]*v3 

    # add cell-centered vars at end
    for (i, k) in enumerate(cell_data.keys()):
        # don't re-add z, r, or indices
        if (i >= 6):
            itp[k] = cell_data[k]

    return itp

import os
from datetime import datetime

def extract_to_txt(tecfile, frame = 0, start_pos = 0, kind = "interpolated", dlm = "\t", outfile = "output.txt", params = None):
    nodal, cellcentered, nextframe_pos = parse_tecplot(tecfile, start_pos)

    now = datetime.now()
    dt_str = now.strftime("%Y-%m-%d %H:%M:%S")

    header = f"# original file: {os.path.abspath(tecfile)}\n" 
    header += f"# date generated: {dt_str}\n"
    header += f"# data kind: {kind} "

    if kind == "interpolated":
        interp = interp_to_cells(nodal, cellcentered)
        dataset = interp
        header += "(all variables, interpolated to cell centers)\n"
    elif kind == "nodal":
        dataset = nodal
        header += "(nodal variables only)\n"
    elif kind == "cellcentered":
        dataset = cellcentered
        header += "(cell-centered variables only)\n"
    else:
        raise Exception("Invalid kind '{kind}'. Select either 'nodal', 'cellcentered', or 'interpolated'")

    # write metadata
    if params is not None:
        header += f"# parameters: {len(params.keys())}\n"
        for k in params.keys():
            header += f"# {k}: {params[k]}\n"
    
    # write column headers
    header = header + dlm.join('"' + var + '"' for var in interp.keys())

    # write data
    data = np.array(list(interp.values())).T
    if frame > 0 or nextframe_pos != -1:
        body, ext = os.path.splitext(outfile)
        outfile = body + f"_{frame:06d}" + ext

    print(outfile)
    np.savetxt(outfile, data, header=header, delimiter = dlm, comments='')
    return nextframe_pos

if __name__ == "__main__":
    import sys
    if len(sys.argv) == 1:
        quit(1)

    nextframe_pos = 0
    frame = 0
    os.makedirs("output")
    while True:
        nextframe_pos = extract_to_txt(sys.argv[1], start_pos = nextframe_pos, frame = frame, outfile = "output/output.txt")
        if nextframe_pos == -1:
            break
        frame += 1
