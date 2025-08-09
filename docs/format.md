# Image format version 2

All integer fields use little endian encoding. Images contain an 8 KiB header and allocation region, 128 fixed 256 byte inode records, and 8 to 32768 data blocks of 4096 bytes. Version 1 images require external migration and are rejected.

The superblock contains magic `DS3IMG02` at byte 0, format version at 8, block size at 12, data block count at 16, transaction generation at 20, and CRC32 at 24. The CRC covers the entire image with the checksum field zeroed. The allocation bitmap starts at byte 4096. Set bits correspond to allocated data blocks.

Each inode stores kind, parent, size, modification timestamp, a 96 byte terminated name, and 32 direct block references. References are one based; zero denotes an unused slot. Directories are represented by parent references in child inodes. The root is inode zero. Names are limited to 95 bytes. Regular files are limited to 128 KiB.

Opening an image verifies the checksum, inode types, parent chains, duplicate sibling names, data block ownership, file lengths, and bitmap agreement. Every command holds an advisory lock on a separate persistent lock file. Cooperating processes must use the same canonical image path.

A mutation changes an in-memory snapshot. Commit encodes the snapshot, recomputes the checksum, writes and syncs a temporary sibling, and renames it over the previous image. A directory sync is attempted after rename and reports a warning if unavailable. A process failure before rename leaves the previous image in place; a failure afterward leaves the new image. Host storage guarantees still apply. Failed commands never call commit. This design trades memory and write amplification for a small transaction surface.

The image is local to one host. The HTTP gateway serializes through the same CLI lock; replication, consensus, permissions, symbolic links, and large files are outside the current format.
