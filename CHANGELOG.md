# skiparray Changes By Release

## v0.2.0 - 2019-xx-yy

### API Changes

Added `.ignore_values` flag to `struct skiparray_config` -- this
eliminates unnecessary allocation when only the skiparray's keys
are being used.

`skiparray_new`'s `config` argument is now `const`.

Added the `skiparray_builder` interface, which can be used to
incrementally construct a skiparray by appending ascending keys. This is
significantly more efficient than constructing by repeatedly calling
`skiparray_set`, because it avoids redundant searches for where to put
the new binding.

Added an incremental fold interface, with left and right folds over one
or more skiparrays' values. If there are multiple equal keys, a merge
callback will be called to merge the options to a single (key, value)
pair first. This is built on top of the iteration interface, so the
skiparray(s) will be locked during the fold.

Added `skiparray_filter`, which produces a filtered shallow copy of
another skiparray using a predicate function.

Moved the `free` callback (previously an argument to `skiparray_free`
and `skiparray_builder_free`) into the `skiparray_config` struct,
since (like `cmp` and the other callbacks) it shouldn't change over
the lifetime of the skiparray.


### Bug Fixes

`skiparray_new` could previously return `SKIPARRAY_NEW_ERROR_NULL` if
memory allocation failed, rather than `SKIPARRAY_NEW_ERROR_MEMORY`.

`skiparray_new` now returns `SKIPARRAY_NEW_ERROR_CONFIG` if the required
comparison callback is `NULL`, rather than `SKIPARRAY_NEW_ERROR_NULL`.

The `-s` (node size) option was missing from the benchmarking CLI's
usage info.

### Other Improvements

The benchmarking CLI can now take multiple, comma-separated limits (e.g.
`-l 1000,10000,100000`), to benchmarks behavior as input grows.

The benchmarking CLI's `-n` flag now uses exact name matching.

Added the benchmarking CLI's `-r` flag, to set the RNG seed.


## v0.1.1 - 2019-04-11

### API Changes

None.

### Bug Fixes

Fixed an overflow bug in the `get_nonexistent` benchmark that meant an
assertion could fail in a 32-bit environment. (Reported by @acfoltzer.)

Ensure that allocations are deterministically initialized, since the
custom memory hook interface doesn't guarantee it.

The SAN Makefile variable wasn't actually being used in build targets.

Portability: Use `uintptr_t`, not `uint64_t`, for word-aligned
allocation during memory benchmarking.

### Other Improvements

Added `cppcheck` target to the Makefile.

Added `scan-build` target to the Makefile.

Fixed some static analysis warnings, related to format strings. Also,
rename a variable to avoid harmless shadowing, and eliminate a redundant
variable update.


## v0.1.0 - 2019-04-08

Initial release.
