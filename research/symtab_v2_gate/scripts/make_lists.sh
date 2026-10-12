#!/usr/bin/env bash
# Builds TU lists (file|clang-args) for each C corpus. Flags documented in REPORT.
C=/home/adhyan/Desktop/Compiler/corpora; RD=/usr/lib/clang/22; B="-x c -std=gnu11 -resource-dir $RD -w -ferror-limit=0"
mkdir -p lists
ls $C/lua/*.c | grep -v -E '/(lua|luac)\.c$' | sed "s#$#|$B -I$C/lua#" > lists/lua.txt
ls $C/cjson/cJSON.c $C/cjson/cJSON_Utils.c $C/cjson/test.c | sed "s#$#|$B -I$C/cjson#" > lists/cjson.txt
ls $C/mbedtls/library/*.c | sed "s#$#|$B -I$C/mbedtls/include -I$C/mbedtls/library -I$C/mbedtls/tf-psa-crypto/include -I$C/mbedtls/tf-psa-crypto/core -I$C/mbedtls/tf-psa-crypto/drivers/builtin/include -I$C/mbedtls/tf-psa-crypto/drivers/builtin/src#" > lists/mbedtls.txt
ls $C/redis/src/*.c | sed "s#$#|$B -I$C/redis/src -I$C/redis/deps/hiredis -I$C/redis/deps/lua/src -I$C/redis/deps/linenoise -I$C/redis/deps/hdr_histogram -I$C/redis/deps/fpconv#" > lists/redis.txt
ls $C/freertos/*.c | sed "s#$#|$B -I$C/freertos/include -I$C/freertos/examples/template_configuration -I$C/freertos/portable/ThirdParty/GCC/Posix -I$C/freertos/portable/ThirdParty/GCC/Posix/utils#" > lists/freertos.txt
ls $C/protobuf-c/protobuf-c/*.c | sed "s#$#|$B -I$C/protobuf-c -I$C/protobuf-c/protobuf-c#" > lists/protobufc.txt
wc -l lists/*.txt
