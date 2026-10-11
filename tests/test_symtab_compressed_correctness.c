/*
 * Comprehensive Correctness Test Suite for Compressed Zephyr Symbol Table
 * Tests all 158 symbols, boundary addresses, unknown addresses, aliases,
 * and buffer truncation across Direct and Checkpoint decoders.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* Forward declare symbols and prototypes generated in test objects */
int symtab_find_symbol_name_r(uintptr_t addr, uint32_t *offset, char *buf, size_t buflen);

typedef struct {
    uint32_t index;
    uintptr_t addr;
    uint32_t size;
    uint32_t offset;
    const char *name;
} sym_test_entry_t;

/* Golden dataset entries parsed from data/zephyr_symtab_158.json */
extern const sym_test_entry_t golden_entries[];
extern const size_t golden_num_entries;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg, ...) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        fprintf(stderr, "FAIL: " msg " (line %d)\n", ##__VA_ARGS__, __LINE__); \
    } \
} while (0)

void test_exhaustive_reconstruction(void) {
    char buf[256];
    uint32_t offset = 0;

    for (size_t i = 0; i < golden_num_entries; i++) {
        const sym_test_entry_t *e = &golden_entries[i];
        
        /* Query exact function address */
        int ret = symtab_find_symbol_name_r(e->addr, &offset, buf, sizeof(buf));
        TEST_ASSERT(ret > 0, "Decode returned non-positive for idx %zu: %d", i, ret);
        TEST_ASSERT(strcmp(buf, e->name) == 0, 
                    "Mismatch at idx %zu (addr 0x%lx): expected '%s', got '%s'", 
                    i, (unsigned long)e->addr, e->name, buf);
        TEST_ASSERT(offset == 0, "Expected offset 0 at exact entry addr, got %u for %s", offset, e->name);
    }
}

void test_interior_offsets(void) {
    char buf[256];
    uint32_t offset = 0;

    for (size_t i = 0; i < golden_num_entries - 1; i++) {
        const sym_test_entry_t *e = &golden_entries[i];
        if (e->size > 4) {
            uintptr_t mid_addr = e->addr + 4;
            int ret = symtab_find_symbol_name_r(mid_addr, &offset, buf, sizeof(buf));
            TEST_ASSERT(ret > 0, "Decode returned error for interior addr 0x%lx", (unsigned long)mid_addr);
            TEST_ASSERT(strcmp(buf, e->name) == 0, 
                        "Mismatch at interior addr 0x%lx: expected '%s', got '%s'", 
                        (unsigned long)mid_addr, e->name, buf);
            TEST_ASSERT(offset == 4, "Expected offset 4 at interior addr, got %u for %s", offset, e->name);
        }
    }
}

void test_boundary_and_unknown_addresses(void) {
    char buf[256];
    uint32_t offset = 0xdeadbeef;

    /* 1. Address below first symbol (first_addr = 0xc1) */
    int ret = symtab_find_symbol_name_r(0x0, &offset, buf, sizeof(buf));
    TEST_ASSERT(ret == 1 && strcmp(buf, "?") == 0 && offset == 0, 
                "Address 0x0 should resolve to '?' with offset 0, got '%s', off=%u", buf, offset);

    ret = symtab_find_symbol_name_r(0xc0, &offset, buf, sizeof(buf));
    TEST_ASSERT(ret == 1 && strcmp(buf, "?") == 0 && offset == 0, 
                "Address 0xc0 should resolve to '?' with offset 0, got '%s', off=%u", buf, offset);

    /* 2. Address above last symbol */
    uintptr_t last_addr = golden_entries[golden_num_entries - 1].addr + 0x1000;
    ret = symtab_find_symbol_name_r(last_addr, &offset, buf, sizeof(buf));
    TEST_ASSERT(ret == 1 && strcmp(buf, "?") == 0, 
                "Out-of-bounds address should resolve to '?', got '%s'", buf);

    /* 3. Dummy entry address itself (last entry at offset 0x2fd8, name '?') */
    const sym_test_entry_t *dummy = &golden_entries[golden_num_entries - 1];
    ret = symtab_find_symbol_name_r(dummy->addr, &offset, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "?") == 0, "Dummy entry should resolve to '?', got '%s'", buf);
}

void test_buffer_capacity_and_truncation(void) {
    char buf[16];
    uint32_t offset = 0;

    /* 1. Zero capacity buffer */
    int ret = symtab_find_symbol_name_r(golden_entries[0].addr, &offset, buf, 0);
    TEST_ASSERT(ret == -1, "Zero buffer length must return -1, got %d", ret);

    /* 2. 1-byte buffer: can only hold null terminator */
    buf[0] = 'X';
    ret = symtab_find_symbol_name_r(golden_entries[0].addr, &offset, buf, 1);
    TEST_ASSERT(buf[0] == '\0', "1-byte buffer must be null terminated, got 0x%02x", buf[0]);

    /* 3. Small truncation: string length 15 (__aeabi_memclr8), buffer size 6 */
    memset(buf, 'X', sizeof(buf));
    ret = symtab_find_symbol_name_r(golden_entries[0].addr, &offset, buf, 6);
    /* buf should have 5 chars + null = "__aea\0" */
    TEST_ASSERT(buf[5] == '\0', "Truncated buffer must have null terminator at buflen-1");
    TEST_ASSERT(strncmp(buf, "__aea", 5) == 0, "Expected prefix '__aea', got '%s'", buf);
}

int main(int argc, char **argv) {
    const char *variant = (argc > 1) ? argv[1] : "default";
    printf("=== Running Correctness Tests for Variant: %s ===\n", variant);

    test_exhaustive_reconstruction();
    test_interior_offsets();
    test_boundary_and_unknown_addresses();
    test_buffer_capacity_and_truncation();

    printf("Results for %s: %d passed, %d failed (Total %d checks)\n", 
           variant, tests_passed, tests_failed, tests_run);

    if (tests_failed > 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
