# skiparray: an unrolled skip list library

This C library provides a `void * -> void *` ordered collection based on
a [Skip list][sl], but where a skip list links individual key/value
pairs, this instead links together small arrays (it's ["unrolled"][u]).
All arrays except the last are always at least half-full. It has roughly
the same relation to a skip list as a [B-tree][bt] does a more
conventional [Binary search tree][bst].

[sl]: https://en.wikipedia.org/wiki/Skiplist
[u]: https://en.wikipedia.org/wiki/Unrolled_linked_list
[bt]: https://en.wikipedia.org/wiki/B-tree
[bst]: https://en.wikipedia.org/wiki/Binary_search_tree


## Key Features:

- **Predictable memory usage, low overhead**

    Bindings are stored in small arrays, which are at least half-full.
    This leads to an overall memory usage of 2 - 4 words per entry (for
    its key and value, and possibly an empty neighbor's), plus < 1% for
    structural overhead. This grows gradually, by adding more small
    arrays; there are no sudden memory spikes caused by structures
    doubling in size.


- **High locality**

    Searches start by following the top layer's links, which are likely
    to already be in cache. After that, most mutations occur within a
    single small array. This reduces time lost to RAM cache misses, and
    makes certain operations (such as popping off the first or last
    pair) particularly efficient.


- **Portable**

    The library doesn't depend on anything beyond the C99 stdlib.
    Tested on Linux (`x86_64`, `armv7l`), OpenBSD (`x86_64`).


- **ISC license**

    You can use it freely, even for commercial purposes.


## Building

To build and install the library:

    $ make
    $ sudo make install

To install the library into a build sandox directory, for packaging:

    $ mkdir destdir
    $ env DESTDIR=destdir make install

To build and run tests (which depend on
[theft](https://github.com/silentbicycle/theft)):

    $ make test

To run benchmarks:

    $ make bench

Build arguments for `libskiparray` are provided via `pkg-config`:

    $ pkg-config --libs --static libskiparray
    -L/usr/local/lib -lskiparray


## General Use

Use `skiparray_new` to allocate a skiparray collection instance. This
must be called with a `struct skiparray_config`, in order to set the
comparison callback (`.cmp`). The other fields are optional.

Free the skiparray with `skiparray_free`. This can be given a callback
to free any bindings stored in the skiparray, so they don't leak.

Key/value pairs can be stored with `skiparray_set`, retrieved with
`skiparray_get`, and removed with `skiparray_forget`. `set`, and `get`,
and `forget` have variants that return the actual stored key as well as
the value (as a `struct skiparray_pair`), in case there are distinct key
instances which compare equal. `skiparray_set_with_pair` also takes a
flag, `replace_key`, to determine whether to replace or keep the current
key when updating an existing binding.

`skiparray_member` checks whether a key is present, and `skiparray_count`
returns how many bindings are stored.

`skiparray_first` and `skiparray_last` look up the first and last
bindings, or report that the skiparray is empty. Both have `pop` variants,
which also remove the first/last binding.

Iterators can be allocated with `skiparray_iter_new` and freed with
`skiparray_iter_free`. While there are iterators active, any functions
that would modify the skiparray structure will return a `LOCKED` error.
Seek to the first/last bindings with `skiparray_iter_seek_endpoint`, to
the first binding `>=` a particular key with `skiparray_iter_seek`, and
`skiparray_iter_next` and `skiparray_iter_prev` will step
forward/backward through the collection. `skiparray_iter_get` will
return the key and value for the iterator's current position. Allocating
an iterator for an empty collection will return an error.

For further details, see the comments in `include/skiparray.h`.
