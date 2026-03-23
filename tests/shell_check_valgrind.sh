#!/bin/sh

# Skip gracefully when valgrind isn't installed (so `make test` still works).
command -v valgrind >/dev/null 2>&1 || {
    echo "Valgrind not installed; skipping valgrind tests."
    exit 0
}

INPUT=`head -n 1 "$1"`
echo "$INPUT" | valgrind --leak-check=full --log-file="valgrind.out" ./msh > /dev/null
# Get how many errors valgrind reports
NERRORS=`awk '/ERROR SUMMARY/{print $4; exit}' valgrind.out 2>/dev/null`
if [ -z "$NERRORS" ]; then
    NERRORS=0
fi
if [ "$NERRORS" -ne 0 ]; then
    cat valgrind.out;
    echo "Valgrind on $1: FAILURE"
else
    echo "Valgrind on $1: SUCCESS"
fi
rm valgrind.out
