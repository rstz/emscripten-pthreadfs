/**
 * @license
 * Copyright 2021 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

mergeInto(LibraryManager.library, {
  $SFAFS__deps: ['$PThreadFS'],
  $SFAFS: {

    benchmark: function(name, fct) {
      if('benchmark_results' in Module) {
        let time_pre = performance.now();
        let result = fct();
        let time_needed = performance.now() - time_pre;

        Module.benchmark_results[`${name}_time`] = (Module.benchmark_results[`${name}_time`] || 0) + time_needed;
        Module.benchmark_results[`${name}_num`] = (Module.benchmark_results[`${name}_num`] || 0) + 1;
        return result;
      }
      return fct();
    },

    /* Debugging */

    debug: function(...args) {
      // Uncomment to print debug information.
      //
      // console.log('SFAFS', arguments);
    },

    /* Helper functions */

    realPath: function(node) {
      var parts = [];
      while (node.parent !== node) {
        parts.push(node.name);
        node = node.parent;
      }
      if (!parts.length) {
        return '/';
      }
      parts.push('');
      parts.reverse();
      return parts.join('/').toLowerCase();
    },

    encodedPath: function(node) {
      return SFAFS.encodePath(SFAFS.realPath(node));
    },

    joinPaths: function(path1, path2) {
      if (path1.endsWith('/')) {
        if (path2.startsWith('/')) {
          return path1.slice(0, -1) + path2;
        }
        return path1 + path2;
      } else {
        if (path2.startsWith('/')) {
          return path1 + path2;
        }
        return path1 + '/' + path2;
      }
    },

    // directoryPath ensures path ends with a path delimiter ('/').
    //
    // Example:
    // * directoryPath('/dir') = '/dir/'
    // * directoryPath('/dir/') = '/dir/'
    directoryPath: function(path) {
      if (path.length && path.slice(-1) == '/') {
        return path;
      }
      return path + '/';
    },

    // extractFilename strips the parent path and drops suffixes after '/'.
    //
    // Example:
    // * extractFilename('/dir', '/dir/myfile') = 'myfile'
    // * extractFilename('/dir', '/dir/mydir/myfile') = 'mydir'
    extractFilename: function(parent, path) {
      parent = SFAFS.directoryPath(parent);
      path = path.substr(parent.length);
      var index = path.indexOf('/');
      if (index == -1) {
        return path;
      }
      return path.substr(0, index);
    },

    /* Path encoding for Storage Foundation API
     * 
     * Storage Foundation does not support directories, hence SFAFS encodes a
     * file's full path in the file name. Storage Foundation imposes the
     * following restrictions on file names:
     * - A name can be at most 100 characters long, and
     * - Only characters a-z, 0-9 and _ may be used.
     * 
     * SFAFS therefore uses an adapted, case-insensitive, case-preserving
     * Percent-encoding for encoding file names. Since % itself is an
     * unsupported character for Storage Foundation, it is replaced with _
     * (underscore). Using a case-insensitive encoding significantly saves
     * encoding length and therefore allows SFAFS to support paths up to ~90
     * characters.
    */
    encodePath: function(path) {
      let uri_encoded_string = encodeURIComponent(path);
      // encodeURIComponent leaves the following non-alphanumeric chars: 
      // - _ . ! ~ * ' ( )
      // Those are replaced (similar to percent encoding) with their byte value
      // in ASCII as a hex, preceded by %.
      let encoded_path_with_percent = uri_encoded_string.replaceAll('-', '%2d');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('_', '%5f');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('.', '%2e');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('!', '%21');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('~', '%7e');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('*', '%2a');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll("'", '%27');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll("(", '%28');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll(")", '%29');

      let encoded_path = encoded_path_with_percent.replaceAll('%', '_');
      encoded_path = encoded_path.toLowerCase();
      return encoded_path;
    },

    decodePath: function(encoded_path) {
      let encoded_path_with_percent = encoded_path.replaceAll('_', '%');

      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%2d', '-');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%5f', '_');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%2e', '.');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%21', '!');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%7e', '~');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%2a', '*');
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%27', "'");
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%28', "(");
      encoded_path_with_percent = encoded_path_with_percent.replaceAll('%29', ")");

      let decoded_path = decodeURIComponent(encoded_path_with_percent);
      return decoded_path;
    },


    listByPrefix: async function(prefix) {
      let entries = await storageFoundation.getAll();
      return entries.filter(name => name.startsWith(prefix))
    },

    // Caches open file handles to simulate opening a file multiple times.
    openFileHandles: {},

    /* Filesystem implementation (public interface) */

    createNode: function (parent, name, mode, dev) {
      SFAFS.debug('createNode', arguments);
      if (!PThreadFS.isDir(mode) && !PThreadFS.isFile(mode)) {
        throw new PThreadFS.ErrnoError({{{ cDefine('EINVAL') }}});
      }
      var node = PThreadFS.createNode(parent, name, mode);
      node.node_ops = SFAFS.node_ops;
      node.stream_ops = SFAFS.stream_ops;
      if (PThreadFS.isDir(mode)) {
        node.contents = {};
      }
      node.timestamp = Date.now();
      return node;
    },

    mount: function (mount) {
      SFAFS.debug('mount', arguments);
      return SFAFS.createNode(null, '/', {{{ cDefine('S_IFDIR') }}} | 511 /* 0777 */, 0);
    },

    cwd: function() { return process.cwd(); },

    chdir: function() { process.chdir.apply(void 0, arguments); },

    allocate: function() {
      SFAFS.debug('allocate', arguments);
      throw new PThreadFS.ErrnoError({{{ cDefine('EOPNOTSUPP') }}});
    },

    ioctl: function() {
      SFAFS.debug('ioctl', arguments);
      throw new PThreadFS.ErrnoError({{{ cDefine('ENOTTY') }}});
    },

    /* Operations on the nodes of the filesystem tree */

    node_ops: {
      getattr: async function(node) {
        SFAFS.debug('getattr', arguments);
        let attr = {};
        // device numbers reuse inode numbers.
        attr.dev = PThreadFS.isChrdev(node.mode) ? node.id : 1;
        attr.ino = node.id;
        attr.mode = node.mode;
        attr.nlink = 1;
        attr.uid = 0;
        attr.gid = 0;
        attr.rdev = node.rdev;
        if (PThreadFS.isDir(node.mode)) {
          attr.size = 4096;
        } else if (PThreadFS.isFile(node.mode)) {
          if (node.handle) {
            attr.size = await node.handle.getLength();
          } 
          else {
            let path = SFAFS.realPath(node);
            if (path in SFAFS.openFileHandles) {
              attr.size = await SFAFS.openFileHandles[path].getLength();
            }
            else {
              if (SFAFS.encodePath(path).length > 100) {
                console.log("SFAFS warning (getattr): Path length might be to long.");
              }
              let fileHandle = await storageFoundation.open(SFAFS.encodePath(path));
              attr.size = await fileHandle.getLength();
              await fileHandle.close();
            }
          }
        } else if (PThreadFS.isLink(node.mode)) {
          attr.size = node.link.length;
        } else {
          attr.size = 0;
        }
        attr.atime = new Date(node.timestamp);
        attr.mtime = new Date(node.timestamp);
        attr.ctime = new Date(node.timestamp);
        // NOTE: In our implementation, st_blocks = Math.ceil(st_size/st_blksize),
        //       but this is not required by the standard.
        attr.blksize = 4096;
        attr.blocks = Math.ceil(attr.size / attr.blksize);
        return attr;
      },

      setattr: async function(node, attr) {
        SFAFS.debug('setattr', arguments);
        if (attr.mode !== undefined) {
          node.mode = attr.mode;
        }
        if (attr.timestamp !== undefined) {
          node.timestamp = attr.timestamp;
        }
        if (attr.size !== undefined) {
          let useOpen = false;
          let fileHandle = node.handle;
          try {
            if (!fileHandle) {
              // Setting a file's length requires an open file handle.
              // Since the file has no open handle, open a handle and close it later.
              useOpen = true;
              if (SFAFS.encodedPath(node).length > 100) {
                console.log("SFAFS warning (setattr): Path length might be to long.");
              }
              fileHandle = await storageFoundation.open(SFAFS.encodedPath(node));
            }
            try {
              await fileHandle.setLength(attr.size);
            }
            catch (e) {
              if (e.name == 'QuotaExceededError') {
                await storageFoundation.requestCapacity(2*1024*1024*1024);
                await fileHandle.setLength(attr.size);
              }
              else {
                throw e;
              }
            }
          } catch (e) {
            if (!('code' in e)) throw e;
            throw new PThreadFS.ErrnoError(-e.errno);
          } finally {
            if (useOpen) {
              await fileHandle.close();
            }
          }
        }
      },

      lookup: async function (parent, name) {
        SFAFS.debug('lookup', arguments);
        var parentPath = SFAFS.directoryPath(SFAFS.realPath(parent));

        var encoded_children = await SFAFS.listByPrefix(SFAFS.encodePath(parentPath));

        let children = encoded_children.map((child) => SFAFS.decodePath(child));

        let lowercase_name = name.toLowerCase()

        var exists = false;
        var mode = 511 /* 0777 */
        for (var i = 0; i < children.length; ++i) {
          var path = children[i].substr(parentPath.length);
          if (path == lowercase_name) {
            exists = true;
            mode |= {{{ cDefine('S_IFREG') }}};
            break;
          }

         let subdirName = SFAFS.directoryPath(lowercase_name);
          if (path.startsWith(subdirName)) {
            exists = true;
            mode |= {{{ cDefine('S_IFDIR') }}};
            break;
          }
        }

        if (!exists) {
          throw PThreadFS.genericErrors[{{{ cDefine('ENOENT') }}}];
        }

        var node = PThreadFS.createNode(parent, lowercase_name, mode);
        node.node_ops = SFAFS.node_ops;
        node.stream_ops = SFAFS.stream_ops;
        return node;
      },

      mknod: function (parent, name, mode, dev) {
        SFAFS.debug('mknod', arguments);
        var node = SFAFS.createNode(parent, name, mode, dev);
        if (!PThreadFS.isFile) {
          console.log('SFAFS error: mknod is only implemented for files')
          throw new PThreadFS.ErrnoError({{{ cDefine('ENOSYS') }}});
        }

        node.handle = null;
        node.refcount = 0;
        return node;
      },

      rename: async function (old_node, new_dir, new_name) {
        SFAFS.debug('rename', arguments);
        let source_is_open = false;

        let old_path = SFAFS.realPath(old_node);
        let encoded_old_path = SFAFS.encodePath(old_path);
        if (old_path in SFAFS.openFileHandles) {
          await SFAFS.openFileHandles[old_path].close();
          delete SFAFS.openFileHandles[old_path];
          source_is_open = true;
        }

        delete old_node.parent.contents[old_node.name];
        old_node.parent.timestamp = Date.now()
        old_node.name = new_name;
        new_dir.contents[new_name] = old_node;
        new_dir.timestamp = old_node.parent.timestamp;
        old_node.parent = new_dir;
        let new_path = SFAFS.realPath(old_node);
        let encoded_new_path = SFAFS.encodePath(new_path);
        if (encoded_new_path.length > 100) {
          console.log("SFAFS warning (rename): Path length might be to long.");
        }

        // Close and delete an existing file if necessary
        let all_files = await storageFoundation.getAll()
        if (all_files.includes(encoded_new_path)) {
          if (new_path in SFAFS.openFileHandles) {
            await SFAFS.openFileHandles[new_path].close();
            delete SFAFS.openFileHandles[new_path];
            console.log("SFAFS Warning: Renamed a file with an open handle. This might lead to unexpected behaviour.")
          }
          await storageFoundation.delete(encoded_new_path);
        }
        await storageFoundation.rename(encoded_old_path, encoded_new_path);
        if (source_is_open) {
          SFAFS.openFileHandles[new_path] = await storageFoundation.open(encoded_new_path);
          // TODO(rstz): Find a more efficient way of updating PThreadFS.streams          
          for (stream of PThreadFS.streams){
            if (typeof stream !== typeof undefined && stream.node == old_node) {
              stream.handle = SFAFS.openFileHandles[new_path];
              stream.node.handle = stream.handle;
            }
          }            
        }
      },

      unlink: async function(parent, name) {
        SFAFS.debug('unlink', arguments);
        var path = SFAFS.joinPaths(SFAFS.realPath(parent), name);
        try {
          await storageFoundation.delete(SFAFS.encodePath(path));
        }
        catch (e) {
          if (e.name == 'NoModificationAllowedError') {
            console.log("SFAFS error: Cannot unlink an open file in StorageFoundation.");
            throw new PThreadFS.ErrnoError({{{ cDefine('EBUSY') }}});
          }
          else {
            throw e;
          }
        }
      },

      rmdir: async function(parent, name) {
        SFAFS.debug('rmdir', arguments);
        let path = SFAFS.directoryPath(SFAFS.joinPaths(SFAFS.realPath(parent), name));
        let files_in_folder = await SFAFS.listByPrefix(SFAFS.encodePath(path));
        if (files_in_folder.length > 0) {
          throw new FS.ErrnoError({{{ cDefine('ENOTEMPTY') }}});
        }
        // SFAFS does not store folders through the API.
        return true;
      },

      readdir: async function(node) {
        SFAFS.debug('readdir', arguments);
        let entries = ['.', '..'];
        let parentPath = SFAFS.directoryPath(SFAFS.realPath(node));
        let children = await SFAFS.listByPrefix(SFAFS.encodePath(parentPath));
        children = children.map(child => SFAFS.extractFilename(parentPath, SFAFS.decodePath(child)));
        return entries.concat(children);;
      },

      symlink: function(parent, newName, oldPath) {
        console.log('SFAFS error: symlink is not implemented')
        throw new PThreadFS.ErrnoError({{{ cDefine('ENOSYS') }}});
      },

      readlink: function(node) {
        console.log('SFAFS error: readlink is not implemented')
        throw new PThreadFS.ErrnoError({{{ cDefine('ENOSYS') }}});
      },
    },

    /* Operations on file streams (i.e., file handles) */

    stream_ops: {
      open: async function (stream) {
        SFAFS.debug('open', arguments);
        if (PThreadFS.isDir(stream.node.mode)) {
          // Everything is correctly set up already
          return;
        }
        if (!PThreadFS.isFile(stream.node.mode)) {
          console.log('SFAFS error: open is only implemented for files')
          throw new PThreadFS.ErrnoError({{{ cDefine('ENOSYS') }}});
        }

        if (stream.node.handle) {
          //TODO: check when this code path is actually executed, it seems to
          //duplicate some of the caching behavior below.
          stream.handle = stream.node.handle;
          ++stream.node.refcount;
        } else {
          var path = SFAFS.realPath(stream.node);

          if (SFAFS.encodePath(path).length > 100) {
            console.log("SFAFS warning (open): Path length might be to long.");
          }

          // Open existing file.
          if(!(path in SFAFS.openFileHandles)) {
            SFAFS.openFileHandles[path] = await storageFoundation.open(SFAFS.encodePath(path));
          }
          stream.handle = SFAFS.openFileHandles[path];
          stream.node.handle = stream.handle;
          stream.node.refcount = 1;
        }
        SFAFS.debug('end open');
      },

      close: async function (stream) {
        SFAFS.debug('close', arguments);
        if (PThreadFS.isDir(stream.node.mode)) {
          // Everything is correctly set up already
          return;
        }
        if (!PThreadFS.isFile(stream.node.mode)) {
          console.log('SFAFS error: close is only implemented for files');
          throw new PThreadFS.ErrnoError({{{ cDefine('ENOSYS') }}});
        }

        stream.handle = null;
        --stream.node.refcount;
        if (stream.node.refcount <= 0) {
          await stream.node.handle.close();
          stream.node.handle = null;
          delete SFAFS.openFileHandles[SFAFS.realPath(stream.node)];
        }
        SFAFS.debug('end close');
      },

      fsync: async function(stream) {
        SFAFS.debug('fsync', arguments);
        if (stream.handle == null) {
          throw new PThreadFS.ErrnoError({{{ cDefine('EBADF') }}});
        }
        await stream.handle.flush();
        SFAFS.debug('end fsync');
        return 0;
      },

      read: async function (stream, buffer, offset, length, position) {
        SFAFS.debug('read', arguments);
        let data = new Uint8Array(length);
        let result = await stream.handle.read(data, position);
        buffer.set(result.buffer, offset);
        SFAFS.debug('end read');
        return result.readBytes;
      },

      write: async function (stream, buffer, offset, length, position) {
        SFAFS.debug('write', arguments);
        stream.node.timestamp = Date.now();
        let data = new Uint8Array(buffer.slice(offset, offset+length));
        let writeResult;
        try {
          writeResult = await stream.handle.write(data, position);
        }
        catch (e) {
          if (e.name == 'QuotaExceededError') {
            await storageFoundation.requestCapacity(2*1024*1024*1024);
            writeResult = await stream.handle.write(data, position);
          }
        }
        return writeResult.writtenBytes;
      },

      llseek: async function (stream, offset, whence) {
        SFAFS.debug('llseek', arguments);
        var position = offset;
        if (whence === 1) {  // SEEK_CUR.
          position += stream.position;
        } else if (whence === 2) {  // SEEK_END.
          position += await stream.handle.getLength();
        } else if (whence !== 0) {  // SEEK_SET.
          throw new PThreadFS.ErrnoError({{{ cDefine('EINVAL') }}});
        }

        if (position < 0) {
          throw new PThreadFS.ErrnoError({{{ cDefine('EINVAL') }}});
        }
        stream.position = position;
        SFAFS.debug('end llseek');
        return position;
      },

      mmap: function(stream, buffer, offset, length, position, prot, flags) {
        SFAFS.debug('mmap', arguments);
        throw new PThreadFS.ErrnoError({{{ cDefine('EOPNOTSUPP') }}});
      },

      msync: function(stream, buffer, offset, length, mmapFlags) {
        SFAFS.debug('msync', arguments);
        throw new PThreadFS.ErrnoError({{{ cDefine('EOPNOTSUPP') }}});
      },

      munmap: function(stream) {
        SFAFS.debug('munmap', arguments);
        throw new PThreadFS.ErrnoError({{{ cDefine('EOPNOTSUPP') }}});
      },
    }
  }
});
