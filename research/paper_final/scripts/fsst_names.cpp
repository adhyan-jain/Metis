// Thin driver around the reference FSST library (github.com/cwida/fsst, pinned commit recorded in
// results/fsst_provenance.txt).  Reads one symbol name per line on stdin, trains an FSST symbol table on exactly those
// names (fsst_create, zeroTerminated=0), compresses every name separately with fsst_compress, round-trips every name
// through the library's own fsst_decompress and aborts on any mismatch, then prints:
//   S <len> <hex>     one line per symbol-table entry, in code order (code 0..nsym-1)
//   N <hexcodes>      one line per input name, the FSST code bytes (code 255 = escape, followed by the literal byte)
// Build: g++ -O2 -std=c++17 -I<fsst_src> fsst_names.cpp <fsst_src>/libfsst.cpp -o fsst_names
#include "fsst.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> names; std::string line;
    while (std::getline(std::cin, line)) names.push_back(line);
    size_t n = names.size();
    std::vector<size_t> len(n); std::vector<const unsigned char*> ptr(n);
    for (size_t i = 0; i < n; i++) { len[i] = names[i].size(); ptr[i] = (const unsigned char*)names[i].data(); }
    fsst_encoder_t* enc = fsst_create(n, len.data(), ptr.data(), 0);
    fsst_decoder_t dec = fsst_decoder(enc);
    std::vector<unsigned char> out(7 + 2 * (1 << 20));
    std::vector<size_t> lenOut(n); std::vector<unsigned char*> strOut(n);
    size_t done = fsst_compress(enc, n, len.data(), ptr.data(), out.size(), out.data(), lenOut.data(), strOut.data());
    if (done != n) { fprintf(stderr, "fsst_compress compressed %zu of %zu\n", done, n); return 2; }
    int nsym = 0;
    for (int c = 0; c < 255; c++) if (dec.len[c] > 0) nsym = c + 1;
    for (int c = 0; c < nsym; c++) {
        printf("S %d ", (int)dec.len[c]);
        for (int k = 0; k < dec.len[c]; k++) printf("%02x", (unsigned)((dec.symbol[c] >> (8 * k)) & 0xff));
        printf("\n");
    }
    for (size_t i = 0; i < n; i++) {
        std::vector<unsigned char> back(len[i] + 16);
        size_t got = fsst_decompress(&dec, lenOut[i], strOut[i], back.size(), back.data());
        if (got != len[i] || memcmp(back.data(), names[i].data(), got) != 0) { fprintf(stderr, "FSST roundtrip mismatch at %zu\n", i); return 3; }
        printf("N ");
        for (size_t k = 0; k < lenOut[i]; k++) printf("%02x", strOut[i][k]);
        printf("\n");
    }
    fsst_destroy(enc);
    return 0;
}
