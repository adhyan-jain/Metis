"""Whole-image link harness: identical recipe to scripts/fairness_audit_master.py (same archives, flags, linker.cmd),
with the symbolization object swapped. Output: flat .bin size (gap-fill, as in all historical numbers), per-section sizes, map."""
import os, subprocess, sys
TC = "/home/adhyan/local-toolchain/bin"
GCC, OBJCOPY, CLANG = f"{TC}/arm-none-eabi-gcc", f"{TC}/arm-none-eabi-objcopy", f"{TC}/arm-none-eabi-clang"
ROOT = "/home/adhyan/Desktop/Compiler"
WL = {"hello_world_mps2": ("/tmp/zephyr_build_symtab_y", "-mcpu=cortex-m3 -mthumb -Os"),
      "shell_module_mps2": ("/tmp/zephyr_build_shell_symtab_y", "-mcpu=cortex-m3 -mthumb -Os"),
      "echo_server_mps2": ("/tmp/zephyr_build_echo_symtab_y", "-mcpu=cortex-m3 -mthumb -Os"),
      "hello_world_nrf52840": ("/tmp/test_build_nrf", "-mcpu=cortex-m4 -mthumb -Os")}
LIBS = """zephyr/arch/common/libarch__common.a zephyr/arch/arch/arm/core/libarch__arm__core.a
zephyr/arch/arch/arm/core/cortex_m/libarch__arm__core__cortex_m.a zephyr/arch/arch/arm/core/mpu/libarch__arm__core__mpu.a
zephyr/lib/libc/picolibc/liblib__libc__picolibc.a zephyr/lib/libc/minimal/liblib__libc__minimal.a zephyr/lib/libc/common/liblib__libc__common.a
zephyr/subsys/debug/symtab/libsubsys__debug__symtab.a zephyr/subsys/portability/posix/c_lib_ext/libsubsys__portability__posix__c_lib_ext.a
zephyr/subsys/timing/libsubsys__timing.a zephyr/subsys/net/ip/libsubsys__net__ip.a zephyr/subsys/net/l2/ethernet/libsubsys__net__l2__ethernet.a
zephyr/subsys/net/lib/config/libsubsys__net__lib__config.a zephyr/subsys/net/conn_mgr/libsubsys__net__conn_mgr.a zephyr/subsys/net/libsubsys__net.a
zephyr/lib/net_buf/liblib__net_buf.a zephyr/lib/os/zvfs/liblib__os__zvfs.a zephyr/subsys/fs/libsubsys__fs.a zephyr/subsys/random/libsubsys__random.a
zephyr/subsys/portability/posix/eventfd/libsubsys__portability__posix__eventfd.a zephyr/subsys/portability/posix/options/libsubsys__portability__posix__options.a
zephyr/drivers/hwinfo/libdrivers__hwinfo.a zephyr/drivers/ethernet/libdrivers__ethernet.a zephyr/soc/soc/nrf52840/libsoc__nordic.a
modules/hal_nordic/nrfx/libnrfx.a zephyr/drivers/clock_control/libdrivers__clock_control.a zephyr/drivers/console/libdrivers__console.a
zephyr/drivers/gpio/libdrivers__gpio.a zephyr/drivers/pinctrl/libdrivers__pinctrl.a zephyr/drivers/serial/libdrivers__serial.a
zephyr/drivers/timer/libdrivers__timer.a zephyr/subsys/shell/libsubsys__shell.a""".split()

def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)

def compile_obj(src, out, cflags): 
    r = sh(f"{CLANG} {cflags} -c {src} -o {out}")
    assert r.returncode == 0, r.stderr[:1500]

def link(wl, obj, tag, outdir):
    bd, cflags = WL[wl]; outdir = os.path.abspath(outdir); os.makedirs(outdir, exist_ok=True)
    elf, binf, mp = (f"{outdir}/{wl}_{tag}.{e}" for e in ("elf", "bin", "map"))
    libs = " ".join(f"`[ -f {l} ] && echo {l}`" for l in LIBS)
    cmd = f"""cd {bd} && {GCC} -Os zephyr/CMakeFiles/zephyr_final.dir/misc/empty_file.c.obj zephyr/CMakeFiles/zephyr_final.dir/isr_tables.c.obj {os.path.abspath(obj)} {ROOT}/build/cost_sweep/libaeabi_helpers.a -o {elf} zephyr/CMakeFiles/offsets.dir/./arch/arm/core/offsets/offsets.c.obj -T zephyr/linker.cmd -Wl,-Map,{mp} -Wl,--whole-archive app/libapp.a zephyr/libzephyr.a {libs} -Wl,--no-whole-archive zephyr/kernel/libkernel.a -L{bd}/zephyr zephyr/arch/common/libisr_tables.a zephyr/subsys/debug/symtab/libsymtab.a -fuse-ld=bfd {cflags} -mabi=aapcs -mfp16-format=ieee -Wl,--gc-sections -Wl,--build-id=none -Wl,--sort-common=descending -Wl,--sort-section=alignment -Wl,-u,_OffsetAbsSyms -Wl,-u,_ConfigAbsSyms -nostdlib -static -Wl,-X -Wl,-N -Wl,--orphan-handling=warn -Wl,--undefined=_sw_isr_table -Wl,--undefined=_irq_vector_table -Wl,-u,zephyr_fault_symbol_logger -L/home/adhyan/local-toolchain/lib/arm-none-eabi `[ -f {bd}/modules/picolibc/libc.a ] && echo {bd}/modules/picolibc/libc.a` -lclang_rt.builtins"""
    r = sh(cmd); assert os.path.exists(elf), r.stderr[:2000]
    sh(f"{OBJCOPY} --gap-fill 0xFF --output-target=binary --remove-section=.comment --remove-section=COMMON {elf} {binf}")
    sz = sh(f"/usr/bin/llvm-size -A {elf}").stdout
    return os.path.getsize(binf), elf, mp, sz

if __name__ == "__main__":   # reproduce historical numbers with the archived closure objects
    exp = {("hello_world_mps2", "kallsyms_m16"): 19288, ("hello_world_mps2", "cand_cp16"): 18136,
           ("hello_world_mps2", "kallsyms_m256"): 19240, ("echo_server_mps2", "kallsyms_m256"): 274216,
           ("echo_server_mps2", "cand_cp16"): 275432, ("hello_world_nrf52840", "cand_cp16"): 28060}
    for (wl, m), e in exp.items():
        sz, *_ = link(wl, f"{ROOT}/build/closure/{wl}_{m}.o", f"repro_{m}", "/tmp/metis_repro")
        print(wl, m, "bin", sz, "historical", e, "OK" if sz == e else "DIFF")
