#!/usr/bin/env python3
"""Archive an already packaged K release with stable ordering and metadata."""
import argparse
from datetime import datetime, timezone
import gzip
import os
from pathlib import Path
import shutil
import stat
import tarfile
import tempfile
import zipfile


def entries(root):
    for path in (root, *sorted(root.rglob("*"), key=lambda p: p.relative_to(root).as_posix())):
        info = path.lstat()
        directory = stat.S_ISDIR(info.st_mode)
        if not directory and not stat.S_ISREG(info.st_mode):
            raise ValueError(f"expected a regular file or directory at {path}")
        name = path.relative_to(root.parent).as_posix()
        mode = 0o755 if directory or info.st_mode & 0o111 else 0o644
        yield path, name, directory, mode, info.st_size


def write_tar(root, file, epoch):
    with gzip.GzipFile(filename="", fileobj=file, mode="wb", mtime=epoch) as compressed:
        with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT,
                          encoding="utf-8") as out:
            for path, name, directory, mode, size in entries(root):
                member = tarfile.TarInfo(name)
                member.mode = mode
                member.mtime = epoch
                if directory:
                    member.type = tarfile.DIRTYPE
                    out.addfile(member)
                else:
                    member.size = size
                    with path.open("rb") as data:
                        out.addfile(member, data)


def write_zip(root, file, epoch):
    timestamp = datetime.fromtimestamp(max(315532800, epoch), timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(file, "w") as out:
        for path, name, directory, mode, size in entries(root):
            member = zipfile.ZipInfo(name + "/" if directory else name, timestamp)
            member.create_system = 3
            member.file_size = 0 if directory else size
            if directory:
                member.external_attr = ((stat.S_IFDIR | mode) << 16) | 0x10
                out.writestr(member, b"")
            else:
                member.external_attr = (stat.S_IFREG | mode) << 16
                member.compress_type = zipfile.ZIP_DEFLATED
                with path.open("rb") as data, out.open(member, "w") as dest:
                    shutil.copyfileobj(data, dest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    archive = args.archive.resolve()
    if not root.is_dir() or not root.name:
        parser.error("the release directory must exist and have a name")
    if root == archive or root in archive.parents:
        parser.error("the archive must be outside the release directory")
    if archive.name.endswith(".tar.gz"):
        write = write_tar
    elif archive.suffix == ".zip":
        write = write_zip
    else:
        parser.error("the archive must end in .tar.gz or .zip")
    try:
        epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
        if not 0 <= epoch <= 0xffffffff:
            raise ValueError("SOURCE_DATE_EPOCH is outside the gzip timestamp range")
        with tempfile.NamedTemporaryFile(dir=archive.parent, delete=False) as file:
            temp = Path(file.name)
            try:
                write(root, file, epoch)
                file.close()
                os.replace(temp, archive)
            finally:
                file.close()
                temp.unlink(missing_ok=True)
    except (OSError, ValueError) as e:
        parser.exit(1, f"archive_release: {e}\n")


if __name__ == "__main__":
    main()
