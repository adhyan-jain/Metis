#!/usr/bin/env python3
"""Build TU lists (file|clang-args) for each C corpus. Run from research/symtab_v2_gate."""
import glob, os, subprocess
C = "/home/adhyan/Desktop/Compiler/corpora"
RD = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
B = f"-x c -std=gnu11 -resource-dir {RD} -w -ferror-limit=0"
def inc(*d): return " ".join(f"-I{C}/{x}" for x in d)
spec = {
 "lua": (sorted(p for p in glob.glob(f"{C}/lua/*.c") if os.path.basename(p) not in ("lua.c","luac.c")), inc("lua")),
 "cjson": ([f"{C}/cjson/cJSON.c", f"{C}/cjson/cJSON_Utils.c", f"{C}/cjson/test.c"], inc("cjson")),
 "redis": (sorted(glob.glob(f"{C}/redis/src/*.c")), inc("redis/src","redis/deps/hiredis","redis/deps/lua/src","redis/deps/linenoise","redis/deps/hdr_histogram","redis/deps/fpconv")),
 "freertos": (sorted(glob.glob(f"{C}/freertos/*.c"))+[f"{C}/freertos/portable/MemMang/heap_4.c", f"{C}/freertos/portable/ThirdParty/GCC/Posix/port.c"], inc("freertos/include","freertos/examples/template_configuration","freertos/portable/ThirdParty/GCC/Posix","freertos/portable/ThirdParty/GCC/Posix/utils")),
 "redisdeps": (sorted(glob.glob(f"{C}/redis/deps/hiredis/*.c")+glob.glob(f"{C}/redis/deps/linenoise/*.c")+glob.glob(f"{C}/redis/deps/hdr_histogram/*.c")+glob.glob(f"{C}/redis/deps/fpconv/*.c")), inc("redis/deps/hiredis","redis/deps/linenoise","redis/deps/hdr_histogram","redis/deps/fpconv")),
 "protobufc": (sorted(glob.glob(f"{C}/protobuf-c/protobuf-c/*.c")), inc("protobuf-c","protobuf-c/protobuf-c")),
}
os.makedirs("lists", exist_ok=True)
for k,(files,i) in spec.items():
    with open(f"lists/{k}.txt","w") as f:
        for p in files: f.write(f"{p}|{B} {i}\n")
    print(k, len(files))
