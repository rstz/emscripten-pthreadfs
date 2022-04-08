mergeInto(LibraryManager.library, {
  // OPFS Backend
  $wasmFS$OPFSAccessHandles : {},
  $wasmFS$OPFSDirectoryHandles : {},
  $wasmFS$OPFSNonLockingFileHandles : {},

  _wasmfs_create_opfs_backend_js__deps : [
    '$wasmFS$backends', '$wasmFS$OPFSAccessHandles', '$wasmFS$OPFSDirectoryHandles',
    '$wasmFS$OPFSNonLockingFileHandles',
  ],
  _wasmfs_create_opfs_backend_js : async function(backend) {
    wasmFS$backends[backend] = {

      // Directory operations

      // Maps `directory_id` to the OPFS root directory. 
      getRootDirectoryHandle : async (directory_id) => {
        wasmFS$OPFSDirectoryHandles[directory_id] = await navigator.storage.getDirectory();
      },
      // Open directory `name` under `parent` and create it if it does not exist. Map the result to
      // handle `directory_id`.
      createOrOpenDirectoryHandle : async (directory_id, name, parent) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[parent]);
        // We should not carelessly overwrite directory ids.
        assert(!wasmFS$OPFSDirectoryHandles.hasOwnProperty(directory_id));
#endif
        let parent_handle = wasmFS$OPFSDirectoryHandles[parent];
        let handle;
        try {
          handle = await parent_handle.getDirectoryHandle(name, {create : true});
        } catch (err) {
          if (err.name === 'TypeMismatchError') {
            // A file of the same name already exists.
            return {{{ cDefine('EEXISTS') }}};
          } else {
            // TODO: Add graceful failure.
            abort("Unknown error in createOrOpenDirectoryHandle");
          }
        }
        wasmFS$OPFSDirectoryHandles[directory_id] = handle;
        return 0;
      },

      // Removes the directory handle `directory_id` refers to from the set of directory handles.
      // This does not remove the directory from the storage backend.
      closeDirectoryHandle : async (directory_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[directory_id]);
#endif
        delete wasmFS$OPFSDirectoryHandles[directory_id];
      },

      // Returns true iff directory `directory_name` exists under `parent`.
      directoryExists : async (directory_name, parent) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[parent]);
#endif
        let parent_handle = wasmFS$OPFSDirectoryHandles[parent];
        try {
          await parent_handle.getDirectoryHandle(directory_name, {create : false});
        } catch (err) {
          return false;
        }
        return true;
      },
      // Returns true iff file `file_name` exists under `parent`.
      fileExists : async (file_name, parent) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[parent]);
#endif
        let parent_handle = wasmFS$OPFSDirectoryHandles[parent];
        try {
          await parent_handle.getFileHandle(file_name, {create : false});
        } catch (err) {
          return false;
        }
        return true;
      },

      // Remove file or directory `name` under `parent`.
      // A directory can only be reomved if it is empty.
      // Files with open access handles cannot be removed.
      removeFileOrDirectory : async (name, parent) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[parent]);
#endif
        let parent_handle = wasmFS$OPFSDirectoryHandles[parent];
        await parent_handle.removeEntry(name);
        return 0;
      },

      // Returns all files and directories under `directory_id`.
      // TODO: Do we need separate methods to retrieve files and directories?
      getDirectoryEntries : async (directory_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[directory_id]);
#endif
        let directory_handle = wasmFS$OPFSDirectoryHandles[directory_id];
        // TODO Consider refactoring this to use `for await` once that is supported by Emscripten's
        // minifier.
        let entries = [];
        let it = directory_handle.values();
        let curr = await it.next();
        while (!curr.done) {
          entries.push(curr.value.name);
          curr = await it.next();
        }
        return entries;
      },

      // File operations.

      // Open file `name` under `parent` and create it if it does not exist. Map the result to
      // handle `file_id`.
      // Only a single access handle per file can exist over all Javascript contexts.
      // TODO: Add graceful error handling if an access handle already exists.
      createOrOpenFile : async (file_id, name, parent) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[parent]);
        // We should not carelessly overwrite file ids.
        assert(!wasmFS$OPFSAccessHandles.hasOwnProperty(file_id));
#endif
        let parent_handle = wasmFS$OPFSDirectoryHandles[parent];
        let file_handle = await parent_handle.getFileHandle(name, {create : true});
        let access_handle;
        // TODO: Remove this once the Access Handles API has decided their exact API shape.
        try {
          if (FileSystemFileHandle.prototype.createSyncAccessHandle.length == 0) {
            access_handle = await file_handle.createSyncAccessHandle();
          } else {
            access_handle = await file_handle.createSyncAccessHandle({mode : "in-place"});
          }
        } catch (err) {
          if (err.name === "InvalidStateError") {
            abort("Only a single access handle can exist per file");
          }
          abort("Unknown error opening a file.")
        }
        wasmFS$OPFSAccessHandles[file_id] = access_handle;
        return 0;
      },
      // Closes the access handle associated with `file_id`.
      close : async (file_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSAccessHandles[file_id]);
#endif
        let file_handle = wasmFS$OPFSAccessHandles[file_id];
        await file_handle.close();
        delete wasmFS$OPFSAccessHandles[file_id];
        return 0;
      },
      // Pick `length` files from linear memory, starting at `buffer` and write them to the file
      // starting at `offset`. Returns the number of bytes written.
      write : async (file_id, buffer, length, offset) => {
#if ASSERTIONS
        assert(wasmFS$OPFSAccessHandles[file_id]);
#endif
        let file_handle = wasmFS$OPFSAccessHandles[file_id];
        let data = HEAPU8.subarray(buffer, buffer + length);
        let writtenBytes = await file_handle.write(data, {at : offset});
        return writtenBytes;
      },
      // Read `length` bytes from the file, starting at `offset` and write them to linear memory,
      // starting at position `buffer`. Returns the number of bytes read.
      read : async (file_id, buffer, length, offset) => {
#if ASSERTIONS
        assert(wasmFS$OPFSAccessHandles[file_id]);
#endif
        let file_handle = wasmFS$OPFSAccessHandles[file_id];
        let data = HEAPU8.subarray(buffer, buffer + length);
        let readBytes = await file_handle.read(data, {at : offset});
        return readBytes;
      },
      // Return the file's size in bytes.
      getSize : async (file_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSAccessHandles[file_id]);
#endif
        let file_handle = wasmFS$OPFSAccessHandles[file_id];
        return await file_handle.getSize();
      },
      // If `new_size` is smaller than the file's current size, truncate the file associated with
      // `file_id` to `new_size`. Otherwise, pad the file with zeroes until its size is `new_size`.
      setSize : async (file_id, new_size) => {
#if ASSERTIONS
        assert(wasmFS$OPFSAccessHandles[file_id]);
#endif
        let file_handle = wasmFS$OPFSAccessHandles[file_id];
        await file_handle.truncate(new_size);
        return 0;
      },
      // Flushes the access handle associated with `file_id`. This implements the behavior of 
      // syscalls fsync and fdatasync.
      flush : async (file_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSAccessHandles[file_id]);
#endif
        let file_handle = wasmFS$OPFSAccessHandles[file_id];
        await file_handle.flush();
        return 0;
      },

      // Non-locking file operations.
      // Opening an access handle locks the file for all other javascript contexts, such as workers
      // or other tabs. The File System Access API also allows the user to retrieve some data about
      // a file without locking the file. The necessary methods are implemented below.
      // These methods can be useful when implementing a read-only backend for Access Handles that 
      // works accross mutliple tabs. They can also be useful to allow access to OPFS Access
      // Handles from the main thread.
      // WARNING: At the time of this writing, it is unclear if these methods will be available in
      // other browsers than Chrome.
      // WARNING: The non locking API has never been optimized for performance

      // Open file `name` under `parent` and create it if it does not exist. Map the result to
      // non-locking handle `file_id`.
      createOrOpenFileNonLocking : async (file_id, name, parent) => {
#if ASSERTIONS
        assert(wasmFS$OPFSDirectoryHandles[parent]);
        // We should not carelessly overwrite non-locking file ids.
        assert(!wasmFS$OPFS.hasOwnProperty(file_id));
#endif
        let parent_handle = wasmFS$OPFSDirectoryHandles[parent];
        let file_handle = await parent_handle.getFileHandle(name, {create : true});
        wasmFS$OPFSNonLockingFileHandles[file_id] = file_handle;
        return 0;
      },
      // Closes the non-locking handle associated with `file_id`.
      closeNonLocking : async (file_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSNonLockingFileHandles[file_id]);
#endif
        delete wasmFS$OPFSNonLockingFileHandles[file_id];
        return 0;
      },
      // Read from a non-locking handle `length` bytes from the file, starting at `offset` and 
      // write them to linear memory, starting at position `buffer`. 
      readNonLocking : async (file_id, buffer, length, offset) => {
#if ASSERTIONS
        assert(wasmFS$OPFSNonLockingFileHandles[file_id]);
#endif
        let file_blob = await wasmFS$OPFSNonLockingFileHandles[file_id].getFile();
        let file_arraybuffer = await file_blob.arrayBuffer();
        var file_uint8view = new Uint8Array(file_arraybuffer);
        let read_maximum = Math.min(position + data.length, file_blob.size);
        let data = HEAPU8.subarray(buffer, buffer + length);
        data.set(file_uint8view.slice(position, read_maximum));
        return read_maximum - position;;
      },
      // Return the file's size in bytes from a non-locking handle.
      getSizeNonLocking : async (file_id) => {
#if ASSERTIONS
        assert(wasmFS$OPFSNonLockingFileHandles[file_id]);
#endif
        let file_blob = await wasmFS$OPFSNonLockingFileHandles[file_id].getFile();
        return file_blob.size;
      },
    };
  },
});
