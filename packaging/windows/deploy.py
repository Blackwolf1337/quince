#!/usr/bin/env python3
# What windeployqt does, from Linux: the Qt plugins Quince uses, Qt's
# translations, and every DLL any of it loads from the MSYS2 tree.
# usage: deploy.py <dist folder with quince.exe> <msys2 ucrt64 dir>
import os
import shutil
import subprocess
import sys

dist, ucrt = sys.argv[1:3]
qt_plugins = os.path.join(ucrt, "share", "qt6", "plugins")
qt_translations = os.path.join(ucrt, "share", "qt6", "translations")

# plugin folders the core needs: covers (imageformats), HTTPS, the cache
# database (sqldrivers: only SQLite). The window is Slint's, so no platform
# plugin.
PLUGINS = ["imageformats", "tls", "networkinformation", "sqldrivers/qsqlite.dll"]


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.isdir(src):
        shutil.copytree(src, dst, dirs_exist_ok=True)
    else:
        shutil.copy2(src, dst)


for p in PLUGINS:
    src = os.path.join(qt_plugins, p)
    if os.path.exists(src):
        copy(src, os.path.join(dist, p))

# where Qt finds all this (MSYS2's Qt would look below share\qt6\)
shutil.copy2(os.path.join(os.path.dirname(os.path.abspath(__file__)), "qt.conf"), os.path.join(dist, "qt.conf"))

for f in os.listdir(qt_translations):
    if f.startswith("qtbase_"):
        copy(os.path.join(qt_translations, f), os.path.join(dist, "translations", f))

# every DLL from the MSYS2 tree that something in the folder loads
bin_dir = os.path.join(ucrt, "bin")
available = {f.lower(): f for f in os.listdir(bin_dir) if f.lower().endswith(".dll")}
present = set()
todo = []
for root, _, files in os.walk(dist):
    for f in files:
        if f.lower().endswith((".exe", ".dll")):
            todo.append(os.path.join(root, f))
            if root == dist:
                present.add(f.lower())
while todo:
    out = subprocess.run(["x86_64-w64-mingw32-objdump", "-p", todo.pop()], capture_output=True, text=True).stdout
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith("DLL Name:"):
            continue
        name = line.split(":", 1)[1].strip().lower()
        if name in present or name not in available:
            continue
        present.add(name)
        target = os.path.join(dist, available[name])
        shutil.copy2(os.path.join(bin_dir, available[name]), target)
        todo.append(target)
print(len(present), "DLLs next to quince.exe")
