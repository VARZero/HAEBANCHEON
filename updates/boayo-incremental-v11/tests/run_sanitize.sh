#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=${1:-/var/tmp/haebancheon-incremental-v11}
candidate=${SANITIZE_BINARY:-$work/sanitize/boayo-native-sanitize}
snapshot=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]+".build.json"))["source"])' "$candidate")
source="$snapshot/src"
mapfile -t objects < <(find "$snapshot/../objects" -name '*.o' ! -name main.o)
for name in state_cache texture_pool; do
  testsource="$here/test_$name.cpp"
  if [[ $name == texture_pool ]];then testsource="$here/../../boayo-dirty-v9/tests/test_texture_pool.cpp";fi
  g++ -std=c++17 -O2 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -I"$source" "$testsource" "${objects[@]}" -o "$work/test_${name}_sanitize"
  "$work/test_${name}_sanitize" > "$work/reports-final/${name}_sanitize.log" 2>&1
done
echo 'PASS ASan/UBSan state cache and texture pool'
