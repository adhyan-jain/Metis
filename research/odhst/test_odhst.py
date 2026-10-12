import json, sys
sys.path.insert(0, "research/odhst")
import odhst
def decode_all(names, B, budget):
    uniq = sorted(set(names)); recs, cps = odhst.front_code(uniq, B)
    toks, seqs = odhst.train_tokens(recs, budget)
    def expand(t): return [t] if t < 0x80 else sum((expand(x) for x in toks[t-0x80]), [])
    maxscan = 0; out = []
    for i, n in enumerate(uniq):          # reconstruct by walking from block start
        j = i - i % B; cur = b""; scans = 0
        for k in range(j, i + 1):
            p, _ = recs[k]; suf = bytes(sum((expand(t) for t in seqs[k]), []))
            cur = cur[:p] + suf; scans += 1
        maxscan = max(maxscan, scans - 1); out.append(cur.decode())
    assert out == uniq, "roundtrip mismatch"
    return maxscan
for f in ("hello_world_158","shell_module_557","echo_server_1381","nrf52840_hello_226"):
    names = [s["name"] for s in json.load(open(f"data/zephyr_symtab_{f}.json"))]
    for B in (4, 8, 16, 32):
        for bud in (0, 64, 128):
            ms = decode_all(names, B, bud); assert ms <= B - 1
    print(f, "ok, max preceding records at B=16:", decode_all(names, 16, 128), "max name len", max(map(len, names)))
