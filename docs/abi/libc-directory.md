# Directory-relative file API — syscall ABI 1.17

Native libc adds openat/fstatat/unlinkat and fdopendir/dirfd so applications and
browser engines can operate relative to an owned directory capability instead
of reconstructing global paths. AT_FDCWD uses the current libc directory;
absolute paths use the process root capability. Relative paths under another
directory FD reject '..' with ENOTSUP, preventing escape from that capability. This is a documented
capability restriction rather than an emulation of a global POSIX namespace.
Repeated separators and '.' may be normalized; the kernel still checks all
ordinary components. Symlinks do not exist, so AT_SYMLINK_NOFOLLOW and O_NOFOLLOW
cannot traverse one. Unknown flags fail EINVAL. Native constants live in fcntl.h.

fdopendir takes FD ownership only on success. closedir closes that descriptor;
dirfd borrows it. opendir creates the same representation with FD_CLOEXEC.
Other threads must not close/reuse a directory FD after ownership transfer.

charm_fd_from_handle adds CHARM_FD_DIRECTORY=2. File/directory imports validate
the actual filesystem type and all imports derive access flags from actual
HandleRights. They consume the handle only if descriptor allocation succeeds.
Importing an existing socket retains the caller-specified type contract; normal
socket operations still perform kernel type/right checks.

79 FsUnlinkTyped(dir,path,len,directory) is additive: directory=0 removes a
regular file, 1 removes an empty directory. Values above 1 are InvalidArgs.
The required parent Write right, target type, open-node check and removal are
evaluated under the existing VFS rules, with type check/removal in one VFS
critical section. WrongType identifies a target type mismatch; libc reports
EISDIR or ENOTDIR according to the requested operation. Existing FsUnlink=29
retains its old untyped behavior. Open targets still fail Busy: POSIX unlink of
an open file is not claimed.

80 FsRename(source_dir,source_path,source_len,target_dir,target_path,target_len)
requires Write authority on both directories. Paths follow the existing
capability-relative syntax. The VFS serializes lookup, destination checks and
rename under its existing mutex. Different mounts fail CrossDevice (new status
23, libc EXDEV). ext2 initially supports regular-file moves within/between
directories and replacement of a closed regular destination, preserving the
source inode and any open source handles. A distinct open destination is Busy.
Identical source/destination inode is a no-op. Directory moves and FAT rename
are NotSupported; no copy/delete operation is misrepresented as atomic rename.

ext2 updates directory-entry inode/rec_len according to the existing local
ext2-doc.html reference; source link count remains unchanged by a successful
rename. Replaced destination links are decremented and its blocks/inode are
reclaimed only after namespace commit. A new entry is written before publishing
the shortened predecessor record. On I/O failure before source removal,
rollback retains the original name when possible; after an ambiguous write,
the new name is retained if the old one disappeared, preventing data loss.
This is atomic to live VFS observers, not a journal or crash-atomic durability
guarantee. Metadata-write failures may require filesystem repair, as on the
existing non-journaled ext2 path. Regular successful operations preserve counts.

libc rename/renameat use this operation, with the same no-escape restriction
for directory-relative '..'. Unsupported filesystem/type combinations fail
ENOTSUP. The existing untyped unlink behavior is not reused for rename.

realpath is the fixed native no-symlink implementation: it resolves relative
paths against cwd, checks every traversed prefix and directory, rejects missing
components even when followed by '..', and returns the canonical absolute path.
Null resolved allocates PATH_MAX bytes; an explicit output is changed only on
success. Empty path is ENOENT, null input EFAULT, excessive lengths ENAMETOOLONG.

The native limits header exposes actual PATH_MAX=4096, NAME_MAX=255 and atomic
PIPE_BUF=4096 alongside compiler arithmetic limits. pathconf(_PC_NAME_MAX)
validates the path and returns 255. No symlinks exist: creation fails ENOTSUP
and readlink on an existing regular file/directory fails EINVAL. chmod fails
ENOTSUP instead of claiming a permission change; capability rights remain the
authority model. Native base file preallocation realizes the requested range
through positioned reads/writes, preserving contents and the descriptor offset.
fopen supports exclusive 'x' creation and records close-on-exec for 'e'.
