# Filesystem identity — syscall ABI 1.15

FsIdentity=75 accepts an owned file/directory handle in RDI. On success RDX
contains the existing native node identity: mount index in bits 56..63,
filesystem-local ID in bits 0..55. It grants no additional access and requires
the same metadata authority as FsStat. Invalid handles/types fail normally.
Old FsStat and its 32-byte layout/reserved fields remain unchanged.

Local identities are ext2 inode numbers or FAT directory-entry identities,
stable while the node exists. Reuse after deletion is permitted. Mounts are
fixed for the current boot; this is not a persistent device UUID.

Static libc stat v3 appends uint64 dev_t/ino_t and time_t atime/ctime to the
old size/mode/mtime fields (56 bytes: dev at 24, ino at 32, atime at 40,
ctime at 48). File identities come from FsIdentity: st_dev=mount+1 and
st_ino=local ID. The native filesystem does not expose access/change times:
those fields are zero as an unavailable marker, not measured timestamps.
futimes fails ENOTSUP; timestamp changes are not silently accepted. Since the
supported native filesystems expose only regular files/directories, lstat is
equivalent to stat and O_NOFOLLOW does not traverse symlinks. No symlink or
Unix mode-bit permission support is claimed.
