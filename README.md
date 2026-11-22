# tico16

[tico](https://github.com/tico-edit/tico), the nano-style text editor,
for 16-bit MS-DOS, built with Open Watcom and
[libc16](https://github.com/gr-dos/libc16).

`tico.exe` is a compact-model, real-mode 8086 executable (about 30 KB)
that runs on any DOS from an original IBM PC up. It needs no
configuration or other files, and edits files as large as conventional
memory allows.

    C:\>TICO CONFIG.SYS

## Usage

    tico [FILE]        edit FILE, or a new buffer
    tico --help        show help (also -h, -?)
    tico --version     show version

The exit code is 0 after editing, and 1 for a bad option or operand or
a file that can't be read (a directory, or too big for memory).

## Keys

| Key | Action |
|---|---|
| Arrows, `^P` `^N` | Move |
| Home/End, `^A` `^E` | Start / end of line |
| PgUp/PgDn, `^Y` `^V` | Page up / down |
| `^Home` `^End`, `M-\` `M-/` | First / last line |
| Backspace `^H`, Del `^D` | Delete before / at the cursor |
| `^K` (F9) | Cut the line; repeated `^K`s add to the cut |
| `M-6` | Copy the line |
| `^U` (F10) | Paste |
| `^C` (F11) | Show the cursor position |
| `^W` (F6) | Where Is: search forward; Enter alone repeats the last search |
| `^O` (F3) | Write Out, asking for the file name |
| `M-D` `M-U` `M-M` | In Write Out: write DOS, Unix or Mac format |
| `^S` | Save |
| `^X` (F2) | Exit, asking to save a modified buffer |

`M-` keys are Alt+key, or Esc then the key. (`M-\` and `M-/` need a
BIOS that reports those Alt keys; `^Home`/`^End` always work.)

## Files

tico16 reads and writes files the way tico and nano 8.7.1 do: the first
line break says whether a file is DOS (CR LF), Unix (LF) or Mac (CR)
format, and every line is written back in that format. A file opened
and saved without changes is unchanged, as long as its line breaks are
all the same kind and it ends with one; otherwise the breaks come out
uniform, and a final one is added, as in nano. New files, and files with
no line break, are DOS format.

Opening a file reports the format it was read as, e.g. `Read 2 lines
(Read as Unix format)`. The Write Out prompt always shows the format it
will write, `Write to File [Unix Format]:`, and `M-D`, `M-U` and `M-M`
choose DOS, Unix or Mac there, so a Unix `AUTOEXEC.BAT` can be made a
DOS one with `^O`, `M-D`, Enter. Choosing a different format changes the
buffer's format at once and marks it modified; cancelling the prompt
keeps the new format, and later `^S` saves use it.

A trailing Ctrl+Z is kept. Bytes are not converted in any way.

## Search

`^W` asks `Search:` and finds the next match after the cursor, moving the
cursor to its start. The search is always literal and ignores case: `*`,
`?`, `[`, `.`, `^`, `$` and the rest are ordinary characters, and only
the letters A-Z and a-z fold together (bytes 128-255 match only
themselves). It goes on line by line, and past the end of the file it
continues from the top, saying `Search Wrapped`. Back on the starting
line it counts a match only up to the cursor, so a match under the
cursor is found last and reported as `This is the only occurrence`. If
there is no match, it says `"TERM" not found` and the cursor stays put.
A match off the screen is centered in the view. Searching never changes
the text or marks it modified.

The last term is remembered and shown in the prompt, `Search [term]:`,
and Enter with nothing typed searches for it again. `^C` cancels,
leaving the cursor and the remembered term as they were.

See `DIFFERENCES.md` for where tico16 differs from tico, and
`docs/tico16-DESIGN.md` for the design.

## Building and testing

Everything is built and tested on Linux, cross-compiling with the
Linux-hosted Open Watcom toolchain and linking libc16's compact-model
library, `LIBC16C.LIB`, instead of Open Watcom's runtime, following the
[hello16](https://github.com/gr-dos/hello16) conventions:

    make         # builds tico.exe
    make test    # unit tests on the host, then the test suite under DOSBox-X
    make clean

`make` fetches the libc16 release pinned in `scripts/fetch-libc16` into
`build/libc16`, checking its SHA-256, and the Open Watcom snapshot it
was built with into `build/tools`. `make LIBC16_ROOT=../libc16` uses a
libc16 checkout built with its own `make` instead (libc16's
`docs/CONSUMERS.md`, "Using libc16").

### Link check

The compiler is told to name no default library (`wcc -zl`) and the
linker to use none (`wlink option nodefaultlibs`), with `LIBC16C.LIB` as
the only library. `scripts/check-link` then proves from the link itself
that this worked. After `tico.exe` and `tico_t.exe` are each linked, it
reads the linker's map (`build/r/tico.map`, `build/t/tico_t.map`) and
checks that:

- each module is either one of that build's own objects (`build/r/*.obj`
  or `build/t/*.obj`), or a module of `LIBC16C.LIB` listed by
  `wlib -l` (`build/libc16.lst`);
- each object is linked exactly once;
- the map's "Libraries Used" section names `LIBC16C.LIB` and nothing
  else.

Anything else fails the check. That includes any Open Watcom runtime
module or library, and also a module line or map it can't parse. Any
line with `module:` in it (in any case) that isn't exactly
`Module: FILE(MEMBER)` is rejected, not skipped. A
failure prints the offending module or library, deletes the executable
and stops the build, so neither `make`, `make test` nor CI can accept an
unchecked link. The summary line, e.g. `check-link: 5 objects and 42
modules of build/libc16/lib/LIBC16C.LIB; no other library`, is printed,
and the full module list is left in `build/r/link.txt` (`build/t/link.txt`).

The map format the check relies on (Open Watcom `wlink` `option map`):
one `Module: FILE(MEMBER)` line per linked module, where MEMBER is FILE
itself for an object file and the module's name for a library member,
and a boxed `|   Libraries Used   |` heading followed by one library per
line. The check is adapted from
[patch16](https://github.com/gr-dos/patch16)'s.

`make test` needs a host C compiler (`cc`) and `dosbox-x`. It runs:

- `tests/check-link.sh`: `scripts/check-link` accepts the real maps of
  both builds, and rejects each map checked against the other build's
  objects, along with altered copies of the release map: an Open Watcom
  `clibc.lib` module, a second library, a name that isn't a module of
  `LIBC16C.LIB`, a stray object, a missing object, a garbled module
  line, module records written `Module:` plus a tab, ` Module: `,
  `MODULE: ` or `Module:` with no space, a missing "Libraries Used"
  section and an empty member list.
  Each rejection must name the problem;

- `tests/unit/test_text.c`: the buffer and line-ending code
  (`src/buffer.c`, `src/text.c`), compiled for the host;
- `tests/run.sh`: the command-line behavior of `tico.exe`, and editing
  sessions driven through `tico_t.exe` in DOSBox-X emulating an 8086.
  `tico_t.exe` is `tico.exe` built with `-DTICO_TEST`: it reads its keys
  from a script file and writes dumps of the screen to another (see
  `src/dos.c` and the comments in `tests/run.sh`). Set `KEEP=1` to keep
  the work directory, or `DOSBOX=` to use another DOSBox-X.

## Releases

`main` only changes through pull requests, which need the `test` check
from `.github/workflows/ci.yml` to pass. When a merge changes `VERSION`
(MS-DOS style: `0.01`, `0.02`, ... `1.00`), `.github/workflows/release.yml`
tags it `v<VERSION>` and publishes a GitHub release with `tico.exe`.

## Source

- `src/main.c`: command line
- `src/editor.c`: the editor: commands, prompts, drawing
- `src/buffer.c`: lines of text (portable C)
- `src/text.c`: reading and writing line breaks (portable C)
- `src/dos.c`: video memory, keyboard, files, interrupt handlers
- `src/tico.h`: shared declarations

## License

MIT. See `LICENSE`.
