#!/usr/bin/env python3
# What windeployqt does, from Linux: the Qt plugins and QML modules Quince
# uses, Qt's translations, and every DLL any of it loads from the MSYS2 tree.
# usage: deploy.py <dist folder with quince.exe> <msys2 ucrt64 dir> <qml source dir>
import json
import os
import shutil
import subprocess
import sys

dist, ucrt, qml_src = sys.argv[1:4]
qt_plugins = os.path.join(ucrt, "share", "qt6", "plugins")
qt_qml = os.path.join(ucrt, "share", "qt6", "qml")
qt_translations = os.path.join(ucrt, "share", "qt6", "translations")

# plugin folders a Quick app needs (sqldrivers: only SQLite)
PLUGINS = ["platforms", "styles", "imageformats", "iconengines", "tls", "networkinformation",
           "sqldrivers/qsqlite.dll"]


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

# the QML modules the sources import, as qmlimportscanner finds them
scan = subprocess.run(["/usr/lib/qt6/qmlimportscanner", "-rootPath", qml_src, "-importPath", qt_qml],
                      check=True, capture_output=True, text=True)
for module in json.loads(scan.stdout):
    path = module.get("path")
    if module.get("type") == "module" and path and path.startswith(qt_qml):
        copy(path, os.path.join(dist, "qml", os.path.relpath(path, qt_qml)))
# type info is for tools only
for root, _, files in os.walk(os.path.join(dist, "qml")):
    for f in files:
        if f.endswith(".qmltypes"):
            os.remove(os.path.join(root, f))

for f in os.listdir(qt_translations):
    if f.startswith(("qtbase_", "qtdeclarative_")):
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
