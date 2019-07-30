Draconity
=========

Draconity provides a means to load custom grammars into Dragon Naturally Speaking on MacOS (and soon, Windows).

TODOs
-----

Draconity is in the process of being ported to Windows (via Linux); here's what's left:

- [x] Implement python protocol client/server reference implementation - https://github.com/caspark/draconity-prototyping (`py` dir)
- [x] Prototype of network transport based on libuv which works on Linux & Windows - https://github.com/caspark/draconity-prototyping (`cpp` dir)
- [x] Port most existing Draconity functionality to C++ (thanks Aegis!)
- [x] Build and link with libbson
- [x] Build and link with libzydis (remove `USE_ZYDIS` flag)
- [x] Implement libuv-based network transport in Draconity
- [x] Connect libuv-based network tranport to Draconity's logic
- [x] Remove MacOS-only cruft (audit `__APPLE__` flag)
- [x] Complete C++ port (remove `CPP_PORT_IS_DONE` flag)
- [ ] Audit all the remaining FIXMEs and TODOs (probably some more porting work there)
- [x] Get it building on Windows
- [x] Implement the shim/code hooking of Dragon portion for Windows

Nice to haves:

- [ ] Vendor libuv, libbson, libzydis and integrate into a single cmake build to make it less likely that I completely forget how to build this in the future
- [ ] Refactor to make Draconity aware of multiple clients
- [ ] Provide clients of Draconity a way to update grammar before recognizing an utterance

Building
========

MacOS
-----

Worked at one point in time, should still work? Send PRs if not :)

Linux
-----

Building on Linux is mainly useful for testing everything aside from the Dragon interactions (e.g. network transport).

First build libbson 1.9.2 using cmake (or the ccmake TUI). There are some caveats with this:

* Seems like you have to do an in-source build (run `cmake` or `ccmake` while `pwd` == source dir)
* You want to build a static library (e.g. set `ENABLE_STATIC` to `ON`)
* You want to build it with `-fPIC` (e.g. add `-fPIC` to CMake's `CFLAGS` - hit `t` if you're using ccmake)
* Later you'll need to manually point Draconity's cmake setup to wherever you built libbson.

Then build Zydis (v2.0.3 should be fine); this is a standard cmake build with no caveats, and then `make install` (or `checkinstall`) it. This can be an out-of-source build.

Now Draconity should be buildable (you have to do an in-source build here too):

```
cd draconity
env CMAKE_LIBRARY_PATH=/home/caspar/src/libbson-1.9.2 cmake -DCMAKE_CXX_FLAGS:STRING="-fpermissive -fsanitize=address" .
make -j8
env LD_PRELOAD="/usr/lib/gcc/x86_64-linux-gnu/7/libasan.so lib/libdraconity.so" sleep 5
```

If you are using an IDE, then you might want to also include `-DCMAKE_EXPORT_COMPILE_COMMANDS=1` in the cmake invocation (for example, Visual Studio code can use this to properly configure itself).

You should see Draconity spew some output as it loads, then `sleep` will exit after 5 seconds. You can use the Python prototyping client at https://github.com/caspark/draconity-prototyping to verify the network transport works (`make pyclient`).

Windows
-------

Just placeholder instructions for now:

It's possible to build on Windows with no errors or warnings. Build on MinGW (use MSYS2) with CMake. Please see https://github.com/jcaw/build-helpers for build scripts specific to a particular path layout.
