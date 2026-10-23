# tico16 — Design Document

**Status:** Draft  
**Version:** 0.2  
**Repository:** `tico-edit/tico16`  
**Executable:** `tico.exe` (`TICO.EXE` on DOS)  
**Target:** 16-bit real-mode DOS, Intel 8088 or later  
**Implementation:** C, Open Watcom 2.0  
**License:** MIT

## 1. Overview

**tico16** is a fast, deliberately minimal, full-screen editor for 16-bit DOS computers. It reproduces the essential visual interface, keyboard shortcuts, and editing behavior of [tico](https://github.com/tico-edit/tico), which follows GNU nano conventions, without bringing along modern features inappropriate for XT-class hardware.

The initial real-hardware target is **Phaistos**, a Monotech NuXT Turbo XT with an NEC V20 CPU and a VGA card. The minimum instruction-set target is the original Intel 8088, not the V20.

The program is distributed as a single standalone **`tico.exe`**. It should make editing files such as `CONFIG.SYS` substantially faster and more pleasant than MS-DOS 6.22 `EDIT`.

## 2. Principles

1. Preserve tico/nano muscle memory wherever feasible. Where tico and this document disagree, tico (and, behind it, nano 8.7.1) wins unless a deviation is recorded in `DIFFERENCES.md`.
2. Optimize startup, typing, navigation, and redraws for XT-class systems.
3. Keep the implementation small and straightforward; avoid infrastructure for excluded features.
4. Handle line-ending formats exactly as tico does; never alter a consistently formatted file.
5. Do no background filesystem work.
6. Require no configuration, installation, or runtime support files.
7. Build and test on Linux, and publish ready-to-run DOS binaries automatically.

## 3. Platform and build target

| Property | Requirement |
|---|---|
| Operating system | MS-DOS 3.3 or compatible, subject to validation |
| CPU | 8088/8086-compatible instruction set |
| Execution mode | 16-bit real mode |
| Display | IBM-compatible 80×25 text mode, color or monochrome |
| Input | PC-compatible keyboard |
| Memory | Conventional DOS memory only; no EMS/XMS required |
| Compiler | Linux-hosted Open Watcom 2.0 |
| Memory model | Compact (near code, far data) |
| Build system | GNU Make |
| Binary | Standalone `tico.exe` |

Use the existing video mode where possible. No mouse, network access, expanded memory, extended memory, or coprocessor is required.

## 4. User interface

Match the actual tico layout and behavior as closely as possible within an 80×25 display. Consult the tico source (checked out as a sibling, `../tico`) rather than relying on a generic nano approximation.

The screen comprises a title bar, editable viewport, status/message line, and a two-line shortcut bar, as in tico. The title bar shows `tico16 VERSION` (where tico shows `tico VERSION`), the filename (or `New Buffer`), and `Modified` when changed. The cursor position is reported on demand with `^C` (Location), in tico's format: `line L/N (P%), col C/W (P%), char X/T (P%)`.

The shortcut bar uses tico's main-menu priority order (`SHORTCUT_PRIORITY` in tico's `src/ui.rs`) with unimplemented entries dropped, laid out in tico's column sizing. For v0.01 that is: Exit, Write Out, Cut, Paste, Location, Copy. Only implemented commands are shown.

Use direct text-video-memory access for responsive drawing, with detection of color (`B800h`) versus monochrome (`B000h`) adapters. BIOS video calls may be used for initialization and cursor management. Prefer incremental updates over full-screen redraws. Avoid per-character DOS/BIOS output calls. Do not assume VGA-specific capabilities. CGA snow avoidance is **not** implemented initially; keep video writes behind one small routine so retrace-synchronized writes can be added later without restructuring.

Long lines scroll horizontally **the nano way** (no soft wrapping): only the cursor's line is scrolled, in chunks, and lines extending past the right edge (or scrolled past the left) are marked with nano's indicators. Tabs expand visually at eight-column stops but remain tab bytes in the file. No automatic indentation.

Control bytes (0x00–0x1F other than tab, and 0x7F) are displayed in nano's caret notation (`^@`, `^M`, `^?`, …), occupying two columns. Bytes 0x80–0xFF are displayed as-is using the active DOS code page.

## 5. Keyboard and editing

The binding source of truth is tico's default keymap (`src/keymap.rs`). v0.01 implements:

| Key | Action |
|---|---|
| Arrow keys, `^P` / `^N` | Move cursor (`^F`/`^B` are search keys in tico, so they are not bound) |
| Home / End, `^A` / `^E` | Beginning / end of line |
| PgUp / PgDn, `^Y` / `^V` | Page navigation |
| `^Home` / `^End`, `M-\` / `M-/` | First / last line |
| Backspace, `^H` / Delete, `^D` | Delete before / at cursor |
| Enter | Insert line break |
| Tab | Insert tab byte |
| `^O`, F3 | Write Out (filename prompt, pre-filled with the current name) |
| `^S` | Save without prompting (prompts for a name if the buffer has none) |
| `^X`, F2 | Exit, prompting "Save modified buffer?" when modified |
| `^K`, F9 | Cut current line; consecutive `^K`s append to the cut buffer |
| `M-6` | Copy current line into the cut buffer |
| `^U`, F10 | Paste cut buffer |
| `^C`, F11 | Report cursor position (Location) |

Meta keys can be typed as Alt+key or as Esc followed by the key, as in nano. Any other key reports tico's `Unbound key: KEY`. Status messages follow tico's code: "Copied", "Pasted", "Nothing was cut", "Cancelled", and so on; `^K` clears the status line.

Use BIOS keyboard input (`INT 16h`) without a custom keyboard interrupt handler. Some Alt combinations (`M-\`, `M-/`) are only reported by enhanced-keyboard BIOS services (`INT 16h` AH=10h); detect and use those when available and fall back to AH=00h, where the `^Home`/`^End` equivalents still work.

Install INT 23h (Ctrl+C) and INT 1Bh (Ctrl+Break) handlers that ignore the break, restoring the original vectors on exit, so DOS cannot abort the editor mid-session.

The first version does **not** include search, search-and-replace, undo/redo, interactive help, mark/region selection, or clipboard integration. Omitted commands must not be shown as available.

## 6. Invocation and configuration

Typical use:

```dos
C:\>TICO CONFIG.SYS
```

An optional filename opens that file or creates an empty buffer associated with the name if it does not exist. Invoking without a filename opens an unnamed buffer; saving it prompts for a name. Only one file/buffer is open at a time.

Per the `dos-hello16` conventions:

```text
tico [FILE]         edit FILE
tico --help         show help (also -h, -?)
tico --version      print "tico (tico16) VERSION"
```

An unknown option or a second operand prints a diagnostic and exits with status 1.

There is **no configuration file** and no configuration parser, theme system, plugin system, custom keymap, or startup script.

## 7. Text file formats

Line-ending handling follows tico, which follows nano 8.7.1 (`convert_line_endings` in tico's `src/fileio.rs`), including the Mac format nano has since dropped:

| Format | Line ending | Bytes |
|---|---|---|
| DOS | CRLF | `0D 0A` |
| Mac | CR | `0D` |
| Unix | LF | `0A` |

On load:

- A CR immediately before an LF is always dropped. The file is DOS format if its **first** line break is CRLF.
- A bare CR is a line break only on the first line, or once the file is known to be Mac format. Elsewhere it is ordinary content, shown as `^M`.
- A CR ending the file is a line break.
- The status line reports `Read N lines`, with tico's `(converted from DOS format)` / `(converted from Mac format)` suffix.

On save, every line break is written in the buffer's format. A buffer not read from a file (new file or unnamed buffer) is saved in **DOS** format; this deliberately differs from tico (which writes Unix) and is recorded in `DIFFERENCES.md`. There is no way to change a buffer's format (no `M-D`/`M-M` toggles).

Consequently, files with mixed line endings may be normalized on save, exactly as tico would.

Additional requirements:

- Final newline: follow nano/tico. A buffer whose last line is non-empty gets a final newline added on save (nano's "magic line"); a file that already ends with a line break is unchanged.
- A trailing DOS Ctrl+Z (`1A`) at the very end of the file is kept as hidden buffer metadata, not shown or editable, and written back on save, even if the buffer is emptied. Typing at the end of the buffer inserts before it. A `1A` anywhere else is ordinary content (`^Z`). Reads are binary; `1A` never ends input.
- Other bytes are 8-bit data; no Unicode or code-page conversion. NUL bytes are content, so the buffer never relies on NUL-terminated strings.
- Existing tab bytes are preserved, with an eight-column visual tab width.
- **Opening and saving an unchanged file whose line breaks are consistent and that ends with a line break (or is empty) is byte-for-byte identical.**

## 8. Buffer and memory

Compact memory model: code is near, data is far, and the buffer can use all of conventional memory. Storage is line-oriented: an array of line records pointing to far-allocated line text, which keeps navigation and per-line redraw simple. The practical file-size ceiling is whatever fits in free conventional memory, measured once implemented and documented in the README. Fail clearly if a file cannot fit; never truncate it silently. Do not impose a short fixed line-length limit.

Buffer code that has no DOS dependencies is kept portable, behind a thin `far`/allocation shim, so it can be unit-tested natively on Linux.

Track whether the buffer is modified. Clear the modified state only after a successful save. Keep typing and cursor movement independent of disk I/O.

## 9. File operations and safety

Use standard DOS file APIs (`INT 21h` handle functions) in binary mode. Read once at startup and write only on explicit save. Handle missing files, access denial, read-only media, disk-full conditions, out-of-memory errors, oversized files, and I/O failures without losing the in-memory buffer.

Saving to `NAME.EXT`:

1. If the target exists and has the read-only attribute, refuse with an error and change nothing.
2. Record the target's attributes (hidden, system, archive).
3. Write the whole buffer to `NAME.$$$` in the same directory and close it, checking every write for a short count (disk full) and the close for errors. On failure, delete `NAME.$$$` and report; the original is untouched.
4. Delete the original.
5. Rename `NAME.$$$` to `NAME.EXT`. If this fails, report that the data is safe in `NAME.$$$`.
6. Restore the recorded attributes on the new file.

No `.BAK` file is kept. DOS replacement is not atomic; between steps 4 and 5 the only copy on disk is `NAME.$$$`. Each failure path is tested.

There is explicitly **no external file-change detection**: no timestamp polling, directory monitoring, reload prompt, automatic reload, or background filesystem activity.

## 10. Explicit exclusions

- Configuration files and user-configurable options
- Syntax highlighting, tree-sitter, themes, and plugins
- Unicode conversion or detection
- Mouse support
- Multiple buffers, windows, or split panes
- Search and replace (deferred from v0.01)
- Undo/redo history
- Mark/region selection
- Line-ending format toggles
- Interactive help beyond visible shortcut hints
- Automatic indentation or soft wrapping
- External clipboard integration
- Macros
- Automatic saving, backups, or external-change detection/reloading
- CGA snow avoidance (may be added later)
- EMS/XMS requirements

Avoid adding scaffolding for these features in anticipation of hypothetical future versions.

## 11. Linux build and test workflow

Follow the conventions in [plicease/dos-hello16](https://github.com/plicease/dos-hello16) (checked out as a sibling, `../dos-hello16`), which is the canonical template for this project's build, CI, versioning, and releases.

```sh
make                         # Build tico.exe
make test                    # Run automated tests, including DOSBox-X
make clean                   # Remove generated files
make WATCOM=/path/to/watcom  # Override toolchain location
```

The default toolchain location is `~/opt/watcom`. The Makefile configures the Open Watcom include and executable paths without requiring the user to source an environment script. Compile for 8086 instructions (`-0`) in the compact model (`-mc`).

Tests:

1. Host-native unit tests (gcc) for buffer logic, line-ending parsing, and serialization.
2. DOSBox-X integration tests, run with DOSBox-X emulating an 8086, using a **test build** of the editor (`tico_t.exe`, built with `-DTICO_TEST`) that reads keystrokes from a script file (`%TICOKEYS%`) instead of `INT 16h` and appends dumps of text video memory to a file (`%TICOSCR%`). The script/dump hooks are the only difference from `tico.exe`.
3. A few smoke tests of the real `tico.exe` under DOSBox-X (`--version`, `--help`, bad options, start-up and exit).
4. Round-trip fixtures covering DOS, Mac, Unix, mixed endings (matching tico's behavior), empty files, final newline presence/absence, tabs, control and high-bit bytes, NULs, and trailing Ctrl+Z.
5. Editing tests for insert/delete, navigation, horizontal scrolling, cut/copy/paste, save prompts, read-only refusal, and write failures.
6. Compatibility testing with 8088 emulation and both color and monochrome displays.
7. Final responsiveness and usability testing on Phaistos.

## 12. Versioning, CI, and releases

Use the **same release conventions as `dos-hello16`**:

- A root `VERSION` file starts at `0.01` and advances `0.02`, `0.03`, …, `1.00`, etc.
- Compile that version into `tico.exe`; `tico --version` prints `tico (tico16) VERSION`.
- `.github/workflows/ci.yml` builds and runs `make test` on pull requests and pushes to `main`.
- Protect `main`: changes enter via pull requests, the `test` check is required, administrators cannot bypass protections, and force-push/deletion is disallowed.
- `.github/workflows/release.yml` runs on pushes to `main`. If tag `v<VERSION>` does not exist, it builds, tests, creates the tag, and publishes a GitHub Release with **`tico.exe` attached**. If the version is unchanged, it does nothing.
- The repository is **`tico-edit/tico16`**.

A version bump in a merged pull request is the mechanism for producing a new downloadable DOS executable.

## 13. Repository layout

```text
tico16/
├── .github/workflows/
│   ├── ci.yml
│   └── release.yml
├── docs/
│   └── tico16-DESIGN.md
├── src/
│   ├── tico.h        shared declarations
│   ├── main.c        command line
│   ├── editor.c      commands, prompts, drawing
│   ├── buffer.c      line storage (portable)
│   ├── text.c        line-ending conversion (portable)
│   └── dos.c         video, keyboard, files, interrupt handlers
├── tests/
│   ├── run.sh        DOSBox-X tests (fixtures are made by the script)
│   └── unit/         host-native tests
├── DIFFERENCES.md
├── LICENSE
├── Makefile
├── README.md
└── VERSION
```


## 14. Milestones

**M1 — Editor foundation:** Linux-hosted Open Watcom build, screen and keyboard handling, file loading, text buffer, navigation, editing, scrolling.

**M2 — Safe save and exit:** `^O`/`^X`, prompts, modified tracking, error handling, the save sequence in §9.

**M3 — Format fidelity:** tico-compatible DOS/Mac/Unix handling, Ctrl+Z preservation, round-trip tests. Required before the editor is considered suitable for general use.

**M4 — tico compatibility:** Match tico's visual layout, shortcut bar, messages, cut/copy/paste semantics, and interaction details.

**M5 — Release readiness:** DOSBox-X automation, GitHub CI, automatic tagged releases with `tico.exe`, and real-hardware testing on Phaistos.

Milestones may overlap; release only after the format and basic safety tests pass.

## 15. Open questions

1. Which adapter detection and rendering paths need real-hardware validation beyond Phaistos's VGA (MDA/Hercules, CGA, EGA)?

## 16. Acceptance criteria

The first usable release must:

- Run as **`TICO.EXE`** on an 8088-compatible DOS system.
- Build and test on Linux with GNU Make and Open Watcom.
- Offer the familiar essential tico/nano editing interface and shortcuts.
- Be noticeably more responsive than DOS EDIT on Phaistos.
- Read and write DOS, Mac, and Unix files the way tico does.
- Preserve unchanged, consistently formatted, newline-terminated files **byte-for-byte**, including trailing Ctrl+Z.
- Refuse to overwrite read-only files, and preserve other attributes.
- Handle recoverable I/O errors without silently losing edited data.
- Require no configuration or auxiliary runtime files.
- Publish versioned GitHub releases from `tico-edit/tico16`, each with **`tico.exe`** as a downloadable artifact.

**In short:** the familiar tico editing experience, with just enough functionality to be useful on an original IBM PC.
