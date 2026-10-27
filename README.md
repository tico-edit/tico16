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
| `^O` (F3) | Write Out, asking for the file name |
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
uniform, and a final one is added, as in nano. New files are DOS format.
A trailing Ctrl+Z is kept. Bytes are not converted in any way.

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

`make test` needs a host C compiler (`cc`) and `dosbox-x`. It runs:

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
