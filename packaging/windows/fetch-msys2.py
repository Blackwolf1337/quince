#!/usr/bin/env python3
# Unpacks MSYS2 UCRT64 packages and everything they depend on into a sysroot,
# as the Windows libraries for a cross build. The compiler itself comes from
# the host, so MSYS2's toolchain packages are left out.
# usage: fetch-msys2.py <cache dir> <sysroot> <package>...
import os
import re
import subprocess
import sys
import urllib.request

REPO = "https://repo.msys2.org/mingw/ucrt64/"
PREFIX = "mingw-w64-ucrt-x86_64-"
# the host's MinGW builds; its headers and import libraries must not be mixed
SKIP = {"gcc", "binutils", "crt-git", "headers-git", "crt", "headers", "windows-default-manifest",
        "tools-git", "libmangle-git", "winpthreads-git", "winpthreads", "gdb", "make", "cmake",
        "ninja", "pkgconf", "pkg-config", "cc", "gcc-ada", "gcc-fortran", "gcc-objc"}


def fetch(url, path):
    if not os.path.exists(path):
        tmp = path + ".part"
        urllib.request.urlretrieve(url, tmp)
        os.replace(tmp, path)
    return path


def read_db(cache):
    db = os.path.join(cache, "ucrt64-db")
    os.makedirs(db, exist_ok=True)
    # a fresh index each run: package files are replaced upstream
    archive = os.path.join(cache, "ucrt64.db.tar.zst")
    if os.path.exists(archive):
        os.remove(archive)
    fetch(REPO + "ucrt64.db.tar.zst", archive)
    subprocess.run(["bsdtar", "-xf", archive, "-C", db], check=True)
    packages, provides = {}, {}
    for entry in os.listdir(db):
        fields, key = {}, None
        with open(os.path.join(db, entry, "desc"), encoding="utf-8") as f:
            for line in f.read().splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line.strip("%")
                    fields[key] = []
                elif line and key:
                    fields[key].append(line)
        name = fields["NAME"][0]
        packages[name] = fields
        for p in fields.get("PROVIDES", []):
            provides.setdefault(re.split(r"[<>=]", p)[0], name)
    return packages, provides


def main():
    cache, sysroot, wanted = sys.argv[1], sys.argv[2], sys.argv[3:]
    packages, provides = read_db(cache)
    todo, chosen = [PREFIX + w for w in wanted], set()
    while todo:
        name = re.split(r"[<>=]", todo.pop())[0]
        name = name if name in packages else provides.get(name, name)
        if name in chosen or name.removeprefix(PREFIX) in SKIP:
            continue
        if name not in packages:
            print("not in the repo:", name, file=sys.stderr)
            continue
        chosen.add(name)
        todo += packages[name].get("DEPENDS", [])
    os.makedirs(os.path.join(cache, "pkgs"), exist_ok=True)
    os.makedirs(sysroot, exist_ok=True)
    stamp = os.path.join(sysroot, ".packages")
    have = set(open(stamp).read().split()) if os.path.exists(stamp) else set()
    for name in sorted(chosen):
        filename = packages[name]["FILENAME"][0]
        if filename in have:
            continue
        path = fetch(REPO + filename, os.path.join(cache, "pkgs", filename))
        # all but pacman's own files (meta packages hold nothing else)
        subprocess.run(["bsdtar", "-xf", path, "-C", sysroot, "--exclude", ".BUILDINFO", "--exclude", ".MTREE",
                        "--exclude", ".PKGINFO", "--exclude", ".INSTALL"], check=True)
        have.add(filename)
        print("unpacked", filename)
    with open(stamp, "w") as f:
        f.write("\n".join(sorted(have)))
    print(len(chosen), "packages in", sysroot)


main()
