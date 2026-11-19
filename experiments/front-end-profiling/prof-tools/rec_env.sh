#!/bin/bash
# usage: rec_env.sh <template> <name> <envspec> <binary> args...
cd /tmp/chibi-v2-frontend
tpl=$1; name=$2; envspec=$3; bin=$4; shift 4
rm -rf traces/$name.trace
xcrun xctrace record --template "$tpl" --output traces/$name.trace --env "$envspec" --target-stdout traces/$name.out --launch -- $bin "$@" > /dev/null 2>&1
cat traces/$name.out
