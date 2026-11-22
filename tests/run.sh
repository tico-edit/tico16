#!/bin/sh
#
# Test tico.exe and its test build, tico_t.exe, under DOSBox-X.
#
# One batch file runs every DOS-side step, and the results are checked
# on the host afterwards.  Each step N records the program's exit status
# in RN.TXT (via rc.bat).  Command-line steps put standard output in
# ON.TXT.  Editing steps run tico_t.exe, which reads its keys from KN.TXT
# (see the key script notes in src/dos.c) and appends screen dumps to
# SN.TXT: one for each {Dump} in the script, and one as the editor exits.
# When a script runs out, tico_t stops with status 3.  rc.bat uses goto
# rather than "if errorlevel N echo ... >file" because the DOSBox-X
# shell opens the redirection even when the test is false.
#
# To add a test: add a step to steps.bat (or an "edit" line), then the
# matching checks below.
#
# Usage: tests/run.sh [path/to/tico.exe [path/to/tico_t.exe]]
# Environment: DOSBOX (default dosbox-x), KEEP=1 to keep the work
#              directory.

set -u

top=$(cd "$(dirname "$0")/.." && pwd)
exe=${1:-$top/tico.exe}
texe=${2:-$top/tico_t.exe}
DOSBOX=${DOSBOX:-dosbox-x}

command -v "$DOSBOX" >/dev/null || { echo "$DOSBOX not found" >&2; exit 2; }
[ -f "$exe" ] || { echo "$exe not found" >&2; exit 2; }
[ -f "$texe" ] || { echo "$texe not found" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/tico16.XXXXXX")
pass=0
fail=0
LC_ALL=C
export LC_ALL

# ------------------------------------------------------------------
# fixtures

cd "$work" || exit 2
cp "$exe" tico.exe
cp "$texe" tico_t.exe

# fixture NAME FORMAT: printf FORMAT into NAME
fixture() {
    printf "$2" > "$1"
}

cat > rc.bat <<'EOS'
@echo off
if errorlevel 4 goto rc4
if errorlevel 3 goto rc3
if errorlevel 2 goto rc2
if errorlevel 1 goto rc1
echo rc0 >c:\r%1.txt
goto end
:rc1
echo rc1 >c:\r%1.txt
goto end
:rc2
echo rc2 >c:\r%1.txt
goto end
:rc3
echo rc3 >c:\r%1.txt
goto end
:rc4
echo rc4 >c:\r%1.txt
:end
EOS

cat > steps.bat <<'EOS'
@echo off
rem --- command line
tico --help >o1.txt
call rc 1
tico -h >o2.txt
call rc 2
tico --version >o3.txt
call rc 3
tico --bogus >o4.txt
call rc 4
tico one two >o5.txt
call rc 5
tico subdir >o6.txt
call rc 6
EOS

# edit STEP ARGS KEYS: run tico_t ARGS with the key script KEYS
edit() {
    printf '%s' "$3" > "K$1.TXT"
    cat >> steps.bat <<EOS
set TICOKEYS=c:\\k$1.txt
set TICOSCR=c:\\s$1.txt
tico_t $2
call rc $1
EOS
}

mkdir SUBDIR

# --- round trips: open, save unchanged (^O Enter, or ^S), exit
fixture DOS.TXT 'one\r\ntwo\r\n'
edit 10 dos.txt '{^O}{Enter}{^X}'
fixture UNIX.TXT 'one\ntwo\n'
edit 11 unix.txt '{^S}{^X}'
fixture MAC.TXT 'one\rtwo\r'
edit 12 mac.txt '{^S}{^X}'
fixture CTRLZ.TXT 'one\r\ntwo\r\n\032'
edit 13 ctrlz.txt '{^S}{^X}'
fixture BYTES.TXT 'tab\there\001\000\177\200\377\r\n\032mid\r\n'
edit 14 bytes.txt '{^S}{^X}'
fixture EMPTY.TXT ''
edit 15 empty.txt '{^S}{^X}'
fixture NOEOL.TXT 'one\ntwo'
edit 16 noeol.txt '{^S}{^X}'
fixture NOBREAK.TXT 'single'
edit 17 nobreak.txt '{^S}{^X}'
fixture MIXED.TXT 'a\r\nb\nc\rd\r\n'
edit 18 mixed.txt '{^S}{^X}'
fixture MACCRLF.TXT 'a\rb\r\nc'
edit 19 maccrlf.txt '{^S}{^X}'

# --- exit without changes, and the status line on opening
edit 20 dos.txt '{Dump}{^X}'
edit 21 new.txt '{Dump}{^X}'
edit 22 '' '{Dump}{^X}'

# --- typing into a new file, saved on exit
edit 23 typed.txt 'abc{Enter}{Tab}def{^X}y{Enter}'

# --- ^X, No: changes are dropped; ^X, ^C: cancelled
fixture KEEP.TXT 'keep\r\n'
edit 24 keep.txt 'xyz{^X}n'
edit 25 keep.txt 'xyz{^X}{^C}{Dump}'

# --- cut, cut (appending), paste; copy
fixture CUT.TXT '1\n2\n3\n'
edit 26 cut.txt '{^K}{^K}{Down}{^U}{^S}{^X}'
fixture COPY.TXT 'a\nb\n'
edit 27 copy.txt '{M-6}{Down}{Down}{^U}{Up}{Esc}6{^U}{^S}{^X}'
fixture CUTEND.TXT 'a\n'
edit 28 cutend.txt '{Down}{^K}{Dump}{^X}'
fixture CUTMID.TXT 'one\ntwo\nthree\n'
edit 29 cutmid.txt '{Down}{^K}{Up}{End}{^U}{^S}{^X}'

# --- backspace and delete across lines, and the magic line
fixture JOIN.TXT 'ab\ncd\n'
edit 30 join.txt '{Down}{Bsp}{End}{Del}{Down}{Bsp}{^S}{^X}'
fixture DELMID.TXT 'ab\n\ncd\n'
edit 31 delmid.txt '{Down}{Del}{Up}{End}{Del}{^S}{^X}'

# --- typing at the end of a file with a Ctrl+Z
fixture ZEDIT.TXT 'a\r\n\032'
edit 32 zedit.txt '{^End}x{^S}{^X}'

# --- writing under another name; overwriting
fixture ORIG.TXT 'orig\r\n'
edit 33 orig.txt '{^O}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}other.txt{Enter}{Dump}y{Dump}{^X}'
fixture EXIST.TXT 'old\r\n'
edit 34 '' 'new{^X}yexist.txt{Enter}{Dump}y'
edit 35 '' 'new{^X}yexist.txt{Enter}n{Dump}{^C}{Dump}{^X}n'

# --- read-only files are refused
fixture RO.TXT 'locked\r\n'
echo 'attrib +r ro.txt' >> steps.bat
edit 36 ro.txt 'x{^S}{Dump}{^X}n'
echo 'attrib -r ro.txt' >> steps.bat

# --- a write that fails keeps the buffer
edit 37 '' 'text{^O}nodir\x.txt{Enter}{Dump}{^X}n'

# --- display: control bytes, long lines, horizontal scrolling
fixture CTRL.TXT 'a\001b\177c\td\n'
edit 38 ctrl.txt '{Dump}{^X}'
long=$(printf '%s' 0123456789 0123456789 0123456789 0123456789 0123456789 \
    0123456789 0123456789 0123456789 0123456789 0123456789)
fixture LONG.TXT "$long\n$long\nshort\n"
edit 39 long.txt '{End}{Dump}{Down}{Dump}{^X}'

# --- paging, first and last line, cursor position
seq 1 100 | sed 's/$/\r/' > LINES.TXT
edit 40 lines.txt '{PgDn}{Dump}{PgDn}{PgUp}{^C}{Dump}{^End}{Dump}{M-\}{^C}{Dump}{^X}'
edit 41 lines.txt '{End}{Down}{^C}{Dump}{^X}'

# --- an unbound key; Esc-prefixed Meta keys
edit 42 '' '{^G}{Dump}x{Dump}{^X}n'

# --- a DOS critical error: the handler fails the call, and editing goes on
edit 43 '' 'ab{Crit}c{Dump}{^X}n'

# --- the format a file was read as
fixture DETU.TXT 'one\ntwo\n'
edit 50 detu.txt '{Dump}{^X}'
fixture DETM.TXT 'one\rtwo\r'
edit 51 detm.txt '{Dump}{^X}'
fixture DETN.TXT 'single'
edit 52 detn.txt '{Dump}{^O}{Dump}{^C}{^X}'
fixture DETE.TXT ''
edit 53 dete.txt '{Dump}{^X}'
fixture DETONE.TXT 'one\n'
edit 54 detone.txt '{Dump}{^X}'
# a new buffer writes DOS
edit 55 '' 'x{^O}{Dump}{^C}{^X}n'

# --- choosing the format in Write Out: Alt keys from ^O
printf 'echo off\nset PATH=C:\\DOS\n' > AUTOEXEC.BAT
edit 56 autoexec.bat '{^O}{Dump}{M-D}{Dump}{Enter}{Dump}{^X}'
edit 57 autoexec.bat '{Dump}{^X}'
# from ^X, Esc-prefixed, lowercase and uppercase
fixture TOUNIX.TXT 'a\r\nb\r\n'
edit 58 tounix.txt 'x{Bsp}{^X}y{Dump}{Esc}u{Dump}{Enter}'
fixture TOMAC.TXT 'a\r\nb\r\n'
edit 59 tomac.txt '{^O}{Esc}M{Dump}{Enter}{^X}'
fixture TOMAC2.TXT 'a\nb\n'
edit 60 tomac2.txt 'x{Bsp}{^X}y{M-M}{Enter}'
edit 61 tomac2.txt '{Dump}{^X}'
# no edits: the change alone marks the buffer modified; ^C keeps the
# new format, and ^S then writes it
fixture NOEDIT.TXT 'a\r\nb\r\n'
edit 62 noedit.txt '{^O}{M-U}{^C}{Dump}{^S}{Dump}{^X}'
edit 63 noedit.txt '{Dump}{^X}'
# cancelled, then ^X asks to save
fixture CANCEL.TXT 'a\r\n'
edit 64 cancel.txt '{^O}{M-M}{^C}{^X}{Dump}n'
# choosing the format the buffer already has changes nothing
fixture SAME.TXT 'a\r\n'
edit 65 same.txt '{^O}{M-D}{Esc}d{^C}{Dump}{^X}'
# the name being typed and its cursor survive a change
fixture NAMED.TXT 'a\n'
edit 66 named.txt '{^O}{Left}{Left}{Left}{Left}{M-D}X{Dump}{Esc}m{Dump}{^C}{^X}n'
# Esc Esc and an unknown Meta key don't cancel or type anything
edit 67 named.txt '{^O}{Esc}{Esc}{Dump}{Esc}q{M-6}{Dump}{^C}{^X}'
# the format, a new name, and overwriting
fixture EXIST2.TXT 'old\r\n'
edit 68 '' 'new{^O}{M-M}exist2.txt{Enter}{Dump}y{^X}'
fixture ORIG2.TXT 'orig\n'
edit 69 orig2.txt '{^O}{End}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}{Bsp}other2.txt{M-D}{Enter}{Dump}y{^X}'
# Ctrl+Z stays at the end
fixture ZFMT.TXT 'a\r\nb\r\n\032'
edit 70 zfmt.txt '{^O}{M-U}{Enter}{^X}'

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 \
    "$DOSBOX" -defaultconf -silent -nogui -nomenu \
    -set "cpu cycles=max" -set "cpu cputype=8086" -set "cpu core=normal" \
    -c "mount c \"$work\"" -c "c:" \
    -c "steps.bat" -c "exit" >dosbox.log 2>&1

# ------------------------------------------------------------------
# checks

ok()     { pass=$((pass + 1)); }
not_ok() { echo "FAIL: $*"; fail=$((fail + 1)); }

# check NAME COMMAND...
check() {
    name=$1
    shift
    if "$@"; then ok; else not_ok "$name"; fi
}

# rc STEP EXPECTED-STATUS
rc() {
    got=$(tr -d ' \r' < "R$1.TXT" 2>/dev/null | sed 's/^rc//')
    if [ "$got" = "$2" ]; then
        ok
    else
        not_ok "step $1: exit status ${got:-missing}, expected $2"
    fi
}

# expect STEP < expected-lines; compares ON.TXT with CRs removed
expect() {
    tr -d '\r' < "O$1.TXT" 2>/dev/null > got.txt
    cat > want.txt
    if cmp -s got.txt want.txt; then
        ok
    else
        not_ok "step $1: output differs"
        diff want.txt got.txt | sed 's/^/    /'
    fi
}

# bytes STEP FILE FORMAT: FILE holds exactly printf FORMAT
bytes() {
    printf "$3" > want.bin
    if cmp -s "$2" want.bin; then
        ok
    else
        not_ok "step $1: $2 differs"
        echo "    want: $(od -An -c want.bin | tr -s ' ')"
        echo "    got:  $(od -An -c "$2" 2>/dev/null | tr -s ' ')"
    fi
}

# screen STEP N: the Nth dump of step STEP, CRs removed, to screen.txt
screen() {
    tr -d '\r' < "S$1.TXT" 2>/dev/null |
        awk -v n="$2" '/^=== /{k++} k==n' > screen.txt
}

# shows STEP N LINE-REGEX...: dump N of step STEP has each line
shows() {
    step=$1
    n=$2
    shift 2
    screen "$step" "$n"
    for re in "$@"; do
        if grep -q -- "$re" screen.txt; then
            ok
        else
            not_ok "step $step dump $n: no line matching '$re'"
            sed 's/^/    |/' screen.txt
        fi
    done
}

# row STEP N ROW TEXT: row ROW (0 = title) of the dump is exactly TEXT
row() {
    screen "$1" "$2"
    got=$(sed -n "$(($3 + 2))p" screen.txt)
    if [ "$got" = "$4" ]; then
        ok
    else
        not_ok "step $1 dump $2 row $3: got '$got', want '$4'"
    fi
}

# the version, as the title bar shows it
ver=$(cat "$top/VERSION")

# command line
rc 1 0
check "step 1: help" grep -q '^Usage: tico' O1.TXT
rc 2 0
check "step 2: -h same as --help" cmp -s O1.TXT O2.TXT
rc 3 0
expect 3 <<EOS
tico (tico16) $(cat "$top/VERSION")
EOS
rc 4 1
expect 4 < /dev/null
rc 5 1
expect 5 < /dev/null
rc 6 1
expect 6 < /dev/null

# round trips
rc 10 0
bytes 10 DOS.TXT 'one\r\ntwo\r\n'
row 10 1 22 '                               [ Wrote 2 lines ]'
rc 11 0
bytes 11 UNIX.TXT 'one\ntwo\n'
rc 12 0
bytes 12 MAC.TXT 'one\rtwo\r'
rc 13 0
bytes 13 CTRLZ.TXT 'one\r\ntwo\r\n\032'
rc 14 0
bytes 14 BYTES.TXT 'tab\there\001\000\177\200\377\r\n\032mid\r\n'
rc 15 0
bytes 15 EMPTY.TXT ''
rc 16 0
bytes 16 NOEOL.TXT 'one\ntwo\n'
rc 17 0
bytes 17 NOBREAK.TXT 'single\r\n'
rc 18 0
bytes 18 MIXED.TXT 'a\r\nb\r\nc\rd\r\n'
rc 19 0
bytes 19 MACCRLF.TXT 'a\rb\rc\r'
check "no temporary files left" test -z "$(ls | grep -F '$$$')"

# opening
rc 20 0
row 20 1 0 "  tico16 $ver                         dos.txt"
shows 20 1 '\[ Read 2 lines (Read as DOS format) \]'
row 20 1 23 '^X Exit        ^K Cut         ^C Location'
row 20 1 24 '^O Write Out   ^U Paste       M-6 Copy'
rc 21 0
row 21 1 0 "  tico16 $ver                         new.txt"
shows 21 1 '\[ New File \]'
check "step 21: no file made" test ! -e NEW.TXT
rc 22 0
row 22 1 0 "  tico16 $ver                       New Buffer"
row 22 1 22 ''

# typing
rc 23 0
bytes 23 TYPED.TXT 'abc\r\n\tdef\r\n'

# exit choices
rc 24 0
bytes 24 KEEP.TXT 'keep\r\n'
rc 25 3
bytes 25 KEEP.TXT 'keep\r\n'
row 25 1 0 "  tico16 $ver                        keep.txt                         Modified"
shows 25 1 '\[ Cancelled \]'

# cut, copy, paste
rc 26 0
bytes 26 CUT.TXT '3\n1\n2\n'
rc 27 0
bytes 27 COPY.TXT 'a\nb\na\na\n'
rc 28 0
shows 28 1 '\[ Nothing was cut \]'
rc 29 0
bytes 29 CUTMID.TXT 'onetwo\n\nthree\n'

# joining lines
rc 30 0
bytes 30 JOIN.TXT 'abcd\n'
rc 31 0
bytes 31 DELMID.TXT 'abcd\n'

# Ctrl+Z stays at the end
rc 32 0
bytes 32 ZEDIT.TXT 'a\r\nx\r\n\032'

# another name
rc 33 0
row 33 1 22 'Save file under DIFFERENT NAME?'
row 33 2 0 "  tico16 $ver                        other.txt"
bytes 33 ORIG.TXT 'orig\r\n'
bytes 33 OTHER.TXT 'orig\r\n'
rc 34 0
row 34 1 22 'File "exist.txt" exists; OVERWRITE?'
bytes 34 EXIST.TXT 'new\r\n'
rc 35 0
shows 35 1 '^Write to File \[DOS Format\]: exist.txt$'
shows 35 2 '\[ Cancelled \]'
bytes 35 EXIST.TXT 'new\r\n'

# read-only
rc 36 0
shows 36 1 '\[ Error writing ro.txt: Permission denied \]'
bytes 36 RO.TXT 'locked\r\n'

# failed write
rc 37 0
shows 37 1 '\[ Error writing nodir\\x.txt: No such file or directory \]' \
    '^text$'

# display
rc 38 0
row 38 1 1 'a^Ab^?c d'
rc 39 0
# cursor at column 100: the line is shown from column 72, '<' first
row 39 1 1 "<$(printf '%s' "$long" | cut -c74-100)"
row 39 1 2 "$(printf '%s' "$long" | cut -c1-79)>"
row 39 2 1 "$(printf '%s' "$long" | cut -c1-79)>"
row 39 2 2 "<$(printf '%s' "$long" | cut -c74-100)"

# paging
rc 40 0
row 40 1 1 '20'
shows 40 1 '^=== dump cursor 1 0$'
shows 40 2 'line  20/101 (19%), col  1/ 3 ( 33%), char  48/292 (16%)'
row 40 3 21 ''
shows 40 3 '^=== dump cursor 21 0$'
shows 40 4 'line   1/101 ( 0%), col  1/ 2 ( 50%), char   0/'
rc 41 0
shows 41 1 'line   2/101 ( 1%), col  2/ 2 (100%), char   3/292 ( 1%)'

# keys
rc 42 0
shows 42 1 '\[ Unbound key: \^G \]'
row 42 2 22 ''

# critical error
rc 43 0
check "step 43: handler fails the call" grep -q '^=== crit al 3 calls 1 deverr 3800 errcode 2' S43.TXT
row 43 2 1 'abc'

# formats read
rc 50 0
shows 50 1 '\[ Read 2 lines (Read as Unix format) \]'
rc 51 0
shows 51 1 '\[ Read 2 lines (Read as Mac format) \]'
rc 52 0
shows 52 1 '\[ Read 1 line (No line break; will write DOS format) \]'
shows 52 2 '^Write to File \[DOS Format\]: detn.txt$'
rc 53 0
shows 53 1 '\[ Read 0 lines (No line break; will write DOS format) \]'
rc 54 0
shows 54 1 '\[ Read 1 line (Read as Unix format) \]'
rc 55 0
shows 55 1 '^Write to File \[DOS Format\]:$'

# choosing the format: ^O, Alt
rc 56 0
shows 56 1 '^Write to File \[Unix Format\]: autoexec.bat$'
row 56 1 23 'M-D DOS Format   M-U Unix Format'
row 56 1 24 '^C Cancel        M-M Mac Format'
row 56 1 0 "  tico16 $ver                      autoexec.bat"
shows 56 2 '^Write to File \[DOS Format\]: autoexec.bat$'
row 56 2 0 "  tico16 $ver                      autoexec.bat                       Modified"
row 56 2 23 'M-D DOS Format   M-U Unix Format'
row 56 2 24 '^C Cancel        M-M Mac Format'
shows 56 3 '\[ Wrote 2 lines \]'
row 56 3 0 "  tico16 $ver                      autoexec.bat"
bytes 56 AUTOEXEC.BAT 'echo off\r\nset PATH=C:\\DOS\r\n'
rc 57 0
shows 57 1 '\[ Read 2 lines (Read as DOS format) \]'
# ^X, Esc-prefixed
rc 58 0
shows 58 1 '^Write to File \[DOS Format\]: tounix.txt$'
shows 58 2 '^Write to File \[Unix Format\]: tounix.txt$'
row 58 2 23 'M-D DOS Format   M-U Unix Format'
bytes 58 TOUNIX.TXT 'a\nb\n'
rc 59 0
shows 59 1 '^Write to File \[Mac Format\]: tomac.txt$'
bytes 59 TOMAC.TXT 'a\rb\r'
rc 60 0
bytes 60 TOMAC2.TXT 'a\rb\r'
rc 61 0
shows 61 1 '\[ Read 2 lines (Read as Mac format) \]'
# no edits, ^C, ^S
rc 62 0
shows 62 1 '\[ Cancelled \]'
row 62 1 0 "  tico16 $ver                       noedit.txt                        Modified"
shows 62 2 '\[ Wrote 2 lines \]'
row 62 2 0 "  tico16 $ver                       noedit.txt"
bytes 62 NOEDIT.TXT 'a\nb\n'
rc 63 0
shows 63 1 '\[ Read 2 lines (Read as Unix format) \]'
rc 64 0
row 64 1 22 'Save modified buffer?'
bytes 64 CANCEL.TXT 'a\r\n'
rc 65 0
row 65 1 0 "  tico16 $ver                        same.txt"
bytes 65 SAME.TXT 'a\r\n'
# the answer and its cursor
rc 66 0
shows 66 1 '^Write to File \[DOS Format\]: namedX.txt$' \
    '^=== dump cursor 22 34$'
shows 66 2 '^Write to File \[Mac Format\]: namedX.txt$' \
    '^=== dump cursor 22 34$'
bytes 66 NAMED.TXT 'a\n'
rc 67 0
shows 67 1 '^Write to File \[Unix Format\]: named.txt$'
shows 67 2 '^Write to File \[Unix Format\]: named.txt$'
row 67 2 23 'M-D DOS Format   M-U Unix Format'
row 67 2 24 '^C Cancel        M-M Mac Format'
bytes 67 NAMED.TXT 'a\n'
# new name, overwrite
rc 68 0
row 68 1 22 'File "exist2.txt" exists; OVERWRITE?'
bytes 68 EXIST2.TXT 'new\r'
rc 69 0
row 69 1 22 'Save file under DIFFERENT NAME?'
bytes 69 ORIG2.TXT 'orig\n'
bytes 69 OTHER2.TXT 'orig\r\n'
rc 70 0
bytes 70 ZFMT.TXT 'a\nb\n\032'
check "no temporary files left after format changes" test -z "$(ls | grep -F '$$$')"

# ------------------------------------------------------------------

echo "$pass passed, $fail failed"
if [ "$fail" -ne 0 ] || [ "${KEEP:-0}" = 1 ]; then
    echo "work directory: $work"
else
    rm -rf "$work"
fi
[ "$fail" -eq 0 ]
