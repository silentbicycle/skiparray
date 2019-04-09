# skiparray Changes By Release

## v0.1.1 - 2019-xx-yy

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
