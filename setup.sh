#!/bin/sh

set -eu

missing=0
for tool in cc make tar; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Missing required tool: %s\n' "$tool" >&2
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    printf '%s\n' 'On Debian, install them with: sudo apt-get install build-essential tar' >&2
    exit 1
fi

printf '%s\n' 'Required build tools are available.'

if ! command -v clang-format >/dev/null 2>&1; then
    printf '%s\n' \
        'Optional clang-format is missing; install it with: sudo apt-get install clang-format'
fi

if ! command -v gcov >/dev/null 2>&1; then
    printf '%s\n' \
        'Optional gcov is missing; install it with: sudo apt-get install gcc'
fi
