#!/bin/sh
#
# Test scripts/check-link: the maps of the real links pass, and copies
# altered to bring in another library or a stray module fail, naming it.
#
# usage: tests/check-link.sh MEMBERS LIBRARY MAP OBJECTS TMAP TOBJECTS
#   MEMBERS   wlib -l listing of LIBRARY
#   MAP       tico.exe's map, OBJECTS its objects (one word)
#   TMAP      tico_t.exe's map, TOBJECTS its objects

set -u

[ $# -eq 6 ] || {
    echo "usage: $0 MEMBERS LIBRARY MAP OBJECTS TMAP TOBJECTS" >&2
    exit 2
}
members=$1 lib=$2 map=$3 objs=$4 tmap=$5 tobjs=$6
top=$(cd "$(dirname "$0")/.." && pwd)
check=$top/scripts/check-link
work=build/check-link
rm -rf "$work"
mkdir -p "$work"
pass=0
fail=0

ok()     { pass=$((pass + 1)); }
not_ok() { echo "FAIL: $*"; fail=$((fail + 1)); }

# check NAME COMMAND...
check() {
    name=$1
    shift
    if "$@"; then ok; else not_ok "$name"; fi
}

# re STRING: STRING as a sed basic regular expression
re() {
    printf '%s' "$1" | sed 's/[]/.[\*^$]/\\&/g'
}

# passes NAME MAP OBJECTS
passes() {
    # shellcheck disable=SC2086
    if sh "$check" -m "$members" "$2" "$lib" $3 >"$work/out" 2>"$work/err"
    then
        ok
    else
        not_ok "$1: check-link failed"
        sed 's/^/    /' "$work/err"
    fi
}

# fails NAME MAP OBJECTS MESSAGE: check-link exits 1 saying MESSAGE
fails() {
    # shellcheck disable=SC2086
    sh "$check" -m "$members" "$2" "$lib" $3 >"$work/out" 2>"$work/err"
    st=$?
    if [ "$st" -ne 1 ]; then
        not_ok "$1: exit status $st, expected 1"
    elif ! grep -qF -- "$4" "$work/err"; then
        not_ok "$1: no '$4' in"
        sed 's/^/    /' "$work/err"
    else
        ok
    fi
}

# alter NAME SED-SCRIPT: a copy of the release map, edited
alter() {
    sed "$2" "$map" > "$work/$1.map"
    if cmp -s "$map" "$work/$1.map"; then
        echo "alter $1: the map didn't change" >&2
        exit 2
    fi
}

first=${objs%% *}
libmod=$(grep -m1 "^Module: $lib(" "$map")

# the real links
passes "release map" "$map" "$objs"
passes "test map" "$tmap" "$tobjs"
check "release map: lists every module" \
    test "$(sh "$check" -m "$members" "$map" "$lib" $objs | grep -c '^object\|^library')" \
    -eq "$(grep -c '^Module: ' "$map")"

# each map against the other's objects
fails "test map, release objects" "$tmap" "$objs" "is not from the objects"
fails "release map, test objects" "$map" "$tobjs" "is not in the map"

# an Open Watcom runtime module
alter owcrt "/^$(re "$libmod")\$/a\\
Module: /opt/watcom/lib286/dos/clibc.lib(fputs)"
fails "Open Watcom runtime module" "$work/owcrt.map" "$objs" \
    "/opt/watcom/lib286/dos/clibc.lib(fputs) is not from the objects or $lib"

# another library used
alter owlib "/^$(re "$lib")\$/a\\
/opt/watcom/lib286/dos/clibc.lib"
fails "another library" "$work/owlib.map" "$objs" \
    "the linker used /opt/watcom/lib286/dos/clibc.lib"
fails "another library: count" "$work/owlib.map" "$objs" \
    "2 libraries used, not 1"

# a module claimed from libc16 that it doesn't have
alter notmember "/^Module: $(re "$lib")(/{
s/(.*)\$/(printf.c)/
:a
n
ba
}"
fails "not a libc16 module" "$work/notmember.map" "$objs" \
    "printf.c is not a module of $lib"
check "not a libc16 module: nothing else wrong" \
    test "$(wc -l < "$work/err")" -eq 1

# a stray object, and a missing one
alter stray "/^Module: $(re "$first")(/a\\
Module: build/r/extra.obj(build/r/extra.obj)"
fails "stray object" "$work/stray.map" "$objs" \
    "build/r/extra.obj(build/r/extra.obj) is not from the objects"
alter missing "/^Module: $(re "$first")(/d"
fails "missing object" "$work/missing.map" "$objs" "$first is not in the map"

# a map it can't read the way it expects
alter garbled "s/^Module: $(re "$first")(.*/Module: $(printf '%s' "$first" | sed 's/[/&\\]/\\&/g')/"
fails "garbled module line" "$work/garbled.map" "$objs" "unrecognized module line"
alter nolibs '/Libraries Used/d'
fails "no Libraries Used" "$work/nolibs.map" "$objs" \
    "no Libraries Used section in the map"
: > "$work/empty.lst"
sh "$check" -m "$work/empty.lst" "$map" "$lib" $objs >/dev/null 2>"$work/err"
check_st=$?
if [ "$check_st" -eq 1 ] && grep -q 'no modules listed' "$work/err"; then
    ok
else
    not_ok "empty member list: status $check_st"
fi

echo "check-link: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
