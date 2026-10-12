
#include <stdint.h>
#include <stddef.h>

extern const char *symtab_find_symbol_name(uintptr_t addr, uint32_t *offset);

static size_t my_strlen(const char *s) {
    size_t l = 0; while (s[l]) l++; return l;
}

int lookup_symbol(uintptr_t addr, char *output_buffer, size_t capacity, uint32_t *offset_out) {
    if (capacity == 0 || output_buffer == NULL) return -1;
    uint32_t off = 0;
    const char *name = symtab_find_symbol_name(addr, &off);
    if (offset_out != NULL) *offset_out = off;
    
    if (name[0] == '?' && name[1] == '\0') {
        if (capacity >= 2) {
            output_buffer[0] = '?'; output_buffer[1] = '\0';
        } else {
            output_buffer[0] = '\0';
        }
        return 1;
    }
    
    size_t name_len = my_strlen(name);
    if (capacity >= name_len + 1) {
        for (size_t i = 0; i <= name_len; i++) output_buffer[i] = name[i];
        return 0;
    } else if (capacity == 1) {
        output_buffer[0] = '\0';
        return 2;
    } else {
        for (size_t i = 0; i < capacity - 1; i++) output_buffer[i] = name[i];
        output_buffer[capacity - 1] = '\0';
        return 2;
    }
}

__attribute__((used, noinline))
void zephyr_fault_symbol_logger(uintptr_t fault_pc, char *log_buf, size_t buf_cap, uint32_t *offset_out) {
    lookup_symbol(fault_pc, log_buf, buf_cap, offset_out);
}
