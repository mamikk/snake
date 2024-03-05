#!/bin/bash
set -euo pipefail

img="$1"
shift

if [ -z "${img}" ]; then
    echo "Usage: $0 IMG [FILES]" 2>&1
    exit 1
fi

rm -f "${img}"
truncate -s $((1440*1024)) "${img}"
mformat -f 1440 -i "${img}" ::/
for file in "$@"; do
    mcopy -i "${img}" "${file}" ::/
done
#mdir -i "${img}" ::/
