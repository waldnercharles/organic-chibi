#!/bin/bash
# usage: rec.sh <template> <name> <binary> args...
cd /tmp/chibi-v2-frontend
tpl=$1; name=$2; bin=$3; shift 3
rm -rf traces/$name.trace
xcrun xctrace record --template "$tpl" --output traces/$name.trace --target-stdout traces/$name.out --launch -- $bin "$@" > /dev/null 2>&1
cat traces/$name.out
