#!/bin/bash
# usage: pgo.sh <treedir> <out> [PARSER=..] ; trains on SEED=2 with parse and lex modes
set -e
tree=$1; out=$2; shift 2
cd $tree
SRC="src/arena.c ${LEXER:-src/lexer.c} src/names.c ${PARSER:-src/parser.c} src/str.c vendor/xxhash/xxhash.c"
cc -std=c23 -Wno-unused-result -O3 -flto -arch arm64 -DNDEBUG -Isrc -fprofile-instr-generate -o build/$out-train bench/prof.c test/programs.c $SRC
rm -f build/$out.profraw
LLVM_PROFILE_FILE=build/$out-%p.profraw SEED=2 ./build/$out-train both 0.3 > /dev/null
LLVM_PROFILE_FILE=build/$out-%p.profraw SEED=2 ./build/$out-train parse 0.3 > /dev/null
xcrun llvm-profdata merge -o build/$out.profdata build/$out-*.profraw
rm -f build/$out-*.profraw
cc -std=c23 -Wno-unused-result -O3 -flto -arch arm64 -DNDEBUG -g -Isrc -fprofile-instr-use=build/$out.profdata -Wno-profile-instr-unprofiled -o build/$out bench/prof.c test/programs.c $SRC
