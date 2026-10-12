def sections(sz):
    r = {}
    for line in sz.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].startswith(".") and p[1].isdigit(): r[p[0]] = int(p[1])
    return r
