# PThreadFS

The Emscripten Pthread File System (PThreadFS) unlocks using (partly) asynchronous storage APIs such as [OPFS Access Handles](https://docs.google.com/document/d/121OZpRk7bKSF7qU3kQLqAEUVSNxqREnE98malHYwWec/edit#heading=h.gj2fudnvy982) through the Emscripten File System API. This enables C++ applications compiled through Emscripten to use persistent storage without using [Asyncify](https://emscripten.org/docs/porting/asyncify.html). PThreadFS requires only minimal modifications to the C++ code and nearly achieves feature parity with the Emscripten's classic File System API.

PThreadFS works by replacing Emscripten's file system API with a new API that proxies all file system operations to a dedicated pthread. This dedicated thread maintains a virtual file system that can use different APIs as backend (very similar to the way Emscripten's VFS is designed). In particular, PThreadFS comes with built-in support for asynchronous backends such as [OPFS Access Handles](https://docs.google.com/document/d/121OZpRk7bKSF7qU3kQLqAEUVSNxqREnE98malHYwWec/edit#heading=h.gj2fudnvy982).
Although the underlying storage API is asynchronous, PThreadFS makes it appear synchronous to the C++ application.

If OPFS Access Handles are not available, PThreadFS will attempt to use the [Storage Foundation API](https://github.com/WICG/storage-foundation-api-explainer). As a last fallback, Emscripten's MEMFS in-memory file system is used.

The code is still prototype quality and **should not be used in a production environment** for the time being.

## Enable OPFS Access Handles in Chrome

OPFS Access Handles require recent versions of Google Chrome Canary. "Experimental Web Platform Features" must be enabled in [chrome://flags](chrome://flags).

Alternatively, you may enable the API with Chrome's " --enable-runtime-features=FileSystemAccessAccessHandle". On MacOS, this can be done through
```
open -a /Applications/Google\ Chrome\ Canary.app --args --enable-runtime-features=FileSystemAccessAccessHandle
```

Both Storage Foundation API and OPFS are available as [origin trials](https://developer.chrome.com/origintrials/) in Chrome 95.
## Getting the code

PthreadFS is available on Github in the [emscripten-pthreadfs](https://github.com/rstz/emscripten-pthreadfs) repository. All code resides in the `pthreadfs` folder. It should be usable with any up-to-date Emscripten version. 

There is **no need** to use a fork of Emscripten itself since all code operates in user-space.

## Using PThreadFS in a project

In order to use the code in a new project, you only need the three files in the `pthreadfs` folder: `pthreadfs_library.js`, `pthreadfs.cpp` and `pthreadfs.h`. The files are included as follows:

### Code changes

- PThreadFS maintains a virtual file system. The OPFS backend is mounted at `/persistent/`. Only files in this folder are persisted between sessions. All other files will be stored in-memory through MEMFS.

You may specify the folder used by PThreadFS by modifying the `PTHREADFS_FOLDER` preprocessor definition, e.g. by compiling with `-DPTHREADFS_FOLDER=mypthreadfsfolder`.

### Build process changes

There are two changes required to build a project with PThreadFS:
- Compile `pthreadfs.h` and `pthreadfs.cpp` and link the resulting object to your application. Add `-pthread` to the compiler flag to include support for pthreads.
- Add the following options to the linking step:
```
-pthread -O2 -s PROXY_TO_PTHREAD --js-library=library_pthreadfs.js
```
**Example**
If your build process was 
```shell
emcc myproject.cpp -o myproject.html
```
Your new build step should be
```shell
emcc -pthread -s PROXY_TO_PTHREAD -O2 --js-library=library_pthreadfs.js myproject.cpp pthreadfs.cpp -o myproject.html
```

### Advanced Usage

If you want to modify the PThreadFS file system directly, you may use the macro `EM_PTHREADFS_ASM()` defined in `pthreadfs.h`. The macro allows you to run asynchrononous Javascript on the Pthread hosting the PThreadFS file system. For example, you may create a folder in the virtual file system by calling
```
EM_PTHREADFS_ASM(
  await PThreadFS.mkdir('mydirectory');
);
```
See `pthreadfs/examples/emscripten-tests/fsafs.cpp` for exemplary usage.


## Known Limitations

- All files to be stored using the file system access Access Handles must be stored in the `/persistent` folder.
- Files in the `/persistent` folder cannot interact through syscalls with other files (e.g. moving, copying, etc.).
- The code is still prototype quality and **should not be used in a production environment** yet. It is possible that the use of PThreadFS might lead to subtle bugs in other libraries.
- PThreadFS requires PROXY_TO_PTHREAD to be active. In particular, no system calls interacting with the file system should be called from the main thread.
- Some functionality of the Emscripten File System API is missing, such as sockets, IndexedDB integration and support for XHRequests.
- PThreadFS depends on C++ libraries. `EM_PTHREADFS_ASM()` cannot be used within C files.
- Performance is good if and only if optimizations (compiler option `-O2`) are enabled and DevTools are closed.
- Accessing the file system before `main()` requires linker option `PTHREAD_POOL_SIZE=<expression>` to be active. Doing so may lead to some blocking of the main thread, which is risky. Check out `examples/early_syscall.cpp` for an example.
- PThreadFS is a case-insensitive file system.
- Compiling with the Closure Compiler is not supported.
- Compiling with optimization `-O3` is not yet supported and may lead to a faulty build.

## Examples

The examples are provided to show how projects can be transformed to use PThreadFS. To build them, navigate to the `pthreadfs/examples/` folder and run `make all`. You need to have the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) activated for the build process to succeed.

### SQLite Speedtest

This example shows how to compile and run the [speedtest1](https://www.sqlite.org/cpu.html) from the SQLite project in the browser.

The Makefile downloads the source of the speedtest and sqlite3 directly from <https://sqlite.org>.

To compile, navigate to the `pthreadfs/examples/` directory and run

```shell
make sqlite-speedtest
cd dist/sqlite-speedtest
python3 -m http.server 8888
```
Then open the following link in a Chrome instance with the
_OPFS Access Handles_ [enabled](#enable-and-detect-opfs-in-chrome):

[localhost:8888/sqlite-speedtest](http://localhost:8888/sqlite-speedtest). The results of the speedtest can be found in the DevTools console.

### Other tests

The folder `pthreadfs/examples/emscripten-tests` contains a number of other file system tests, mostly taken from Emscripten's standard test suite.

To compile, navigate to the `pthreadfs/examples/` directory and run

```shell
make emscripten-tests
cd dist/emscripten-tests
python3 -m http.server 8888
```
Then open the following link in a Chrome instance with the
_OPFS Access Handles_ [enabled](#enable-and-detect-opfs-in-chrome):

[localhost:8888/emscripten-tests](http://localhost:8888/emscripten-tests) and choose a test. The results of the test can be found in the DevTools console.

## Authors
- Richard Stotz (<rstz@chromium.org>)

This is not an official Google product.