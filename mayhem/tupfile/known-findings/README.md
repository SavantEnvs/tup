# Heap buffer under-read in `parse_tupfile` (src/tup/parser.c:589)

`crash-underread.tup` is a single NUL byte. As a `Tupfile` it makes `tup parse` read one byte before the
slurped buffer:

    ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 1
    located 1 bytes before 2-byte region
    #0 parse_tupfile  src/tup/parser.c:589   (while(newline[-1] == '\\' || (newline[-2] == '\\' ...))
    #1 parse          src/tup/parser.c:288

Cause: for a line that starts with a NUL, `get_newline()` returns a pointer at the start of the buffer, and the
line-continuation loop then reads `newline[-1]` / `newline[-2]` before the allocation.

Reproduce with the harness: `mkdir t && cd t && cp ../crash-underread.tup in && /mayhem/fuzz_tup_parse tupfile in`.

Fix: only inspect `newline[-1]` / `newline[-2]` when `newline > line` (resp. `newline - 1 > line`).
