# Known differences from tico

tico16 follows [tico](https://github.com/tico-edit/tico), and through it
GNU nano 8.7.1, for everything it implements. This file records the
places where it knowingly does not. When a difference is removed,
delete its entry.

## Most commands are not there

tico16 has cursor movement, typing, Enter, Tab, Backspace/Delete,
`^K` cut, `M-6` copy, `^U` paste, `^C` Location, `^O` Write Out,
`^S` Save and `^X` Exit, plus their function-key equivalents. Everything
else -- help, search and replace, undo, the mark, Read File, Go To Line,
Execute, justify, the toggles and the rest -- is left out, and its keys
report `Unbound key: KEY`. The shortcut bar lists only the commands that
exist, in tico's order.

## New buffers are saved in DOS format

tico (like nano) writes a buffer that wasn't read from a file, or a file
with no line break in it, with Unix line endings. tico16 writes these in
DOS format (CR LF), since it runs on DOS. Files that have a line break
keep the format their first one shows, as in tico.

## A trailing Ctrl+Z is kept, out of sight

A Ctrl+Z (`1A`) as the last byte of a file is the old DOS end-of-file
marker. tico16 keeps it aside rather than showing it as `^Z` on a last
line of its own: typing at the end of the file goes before it, and it is
written back after the text on every save, even if the text is deleted.
A Ctrl+Z anywhere else is ordinary text.

## No line-ending or file options when writing

The Write Out prompt has no `M-D`/`M-M` format toggles, append, prepend,
backup, browser, Tab completion or history. Its label still shows
`[DOS Format]` or `[Mac Format]`.

## Saving goes through NAME.$$$

tico16 writes the whole file to `NAME.$$$` in the same directory, then
deletes `NAME` and renames `NAME.$$$` to it, so a failed write (a full
disk, say) leaves the original untouched. DOS can't rename over a file,
so between the delete and the rename the text is only in `NAME.$$$`; if
the rename fails, the error says the text is there. A read-only file is
refused ("Permission denied") rather than written; the hidden and system
attributes of the file being replaced are kept.

## No check for changes on disk

There is no "File was modified since you opened it" question, no lock
file, and nothing else that looks at the file after it has been read.

## Bytes 128-255 are shown as they are

tico shows text as UTF-8. tico16 does no conversion: bytes 128-255 are
shown with the PC's code page glyphs. Control bytes (0-31 except Tab,
and 127) are shown in caret notation (`^A`, `^?`), as in tico.

## The title bar says tico16

The upper left of the title bar shows `tico16` and tico16's version,
where tico shows `tico` and its own version.

## The screen is restored on exit

Like tico on a terminal with an alternate screen, tico16 puts back the
DOS screen and cursor it started with when it exits.
