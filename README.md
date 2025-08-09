# DS3

A transactional image filesystem in C with a Go HTTP gateway. The CLI manages a directory tree, inode records, an allocation bitmap, and fixed data blocks inside one checksummed image.

## Build and use

Requires a C11 compiler and CMake 3.16 or newer. The gateway requires Go 1.22 or newer.

```sh
cmake -S . -B build
cmake --build build
./build/ds3 format workspace.ds3 256
./build/ds3 mkdir workspace.ds3 /notes
./build/ds3 put workspace.ds3 /notes/readme README.md
./build/ds3 ls workspace.ds3 /notes
./build/ds3 cat workspace.ds3 /notes/readme
./build/ds3 cp workspace.ds3 /notes/readme /notes/copy
./build/ds3 mv workspace.ds3 /notes/copy /notes/archive
./build/ds3 stat workspace.ds3 /notes/archive
./build/ds3 check workspace.ds3
```

Commands include `format`, `info`, `check`, `ls`, `stat`, `mkdir`, `touch`, `put`, `get`, `cat`, `cp`, `mv`, and `rm`. Creation requires an existing parent. Removal requires an empty directory. Format refuses an existing image. File contents are binary safe. The format supports 127 user inodes and 128 KiB per file.

## HTTP gateway

From `server`, set `DS3_TOKEN` to a secret of at least 32 characters and run `go run .`. Optional `DS3_BINARY` and `DS3_IMAGE` select the CLI and image. The service listens on localhost port 8088 and requires `Authorization: Bearer <token>` for every request.

Routes: `GET /health`, `GET /entries?path=/notes`, `POST /directories?path=/notes`, and `GET`, `PUT`, or `DELETE /files?path=/notes/example`. PUT accepts raw bytes with a 128 KiB limit. Image operation errors return 409. At most eight subprocess requests run concurrently, each with a ten second deadline. The gateway invokes the CLI directly without a shell.

See [format and transaction design](docs/format.md) for binary layout, integrity checks, and durability limits.
