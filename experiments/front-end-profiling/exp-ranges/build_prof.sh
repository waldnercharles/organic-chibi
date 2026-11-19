#!/bin/bash
# usage: [PGO=1] [LEXER=file] [PARSER=file] build_prof.sh <out> [extra cflags...]
set -e
cd /tmp/chibi-v2-frontend/exp-ranges
out=$1; shift
LEXER=${LEXER:-src/lexer.c}
PARSER=${PARSER:-src/parser.c}
SRC="src/arena.c $LEXER src/names.c $PARSER src/str.c vendor/xxhash/xxhash.c"
EXTRA=""
if [ -n "$PGO" ]; then EXTRA="-fprofile-instr-use=../build/bench.profdata -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"; fi
cc -std=c23 -Wall -Wextra -Wno-unused-result -O3 -flto -arch arm64 -mmacosx-version-min=11.0 -DNDEBUG -g -Isrc $EXTRA "$@" -Wl,-object_path_lto,"$out.lto.o" -o "$out" bench/prof.c test/programs.c $SRC
dsymutil "$out" 2>/dev/null
