"""The Python inside a release app, which builds the English patch on the player's Mac from their own disc
(docs/adr/0001-english-patch-in-the-app.md says why): a trimmed copy of this Mac's python.org Python 3.13 framework with
Pillow and NumPy, made relocatable. Used by make_app.py --release.

    copy(dest) -> [(licence file, its name in the app's licences)]
    python3 tools/app_python.py DEST      (to try it on its own)

dest gets the layout of a Python prefix: bin/python3, lib/libpython3.13.dylib, lib/python3.13 (the standard library less
its tests, IDLE, Tk, curses and SSL, which the builder doesn't use), lib/python3.13/site-packages (Pillow and NumPy only, less
their tests). Every Mach-O is thinned to arm64, refers to the libraries beside it (never /Library/Frameworks), and is signed
ad hoc; the .py files are compiled (unchecked-hash .pyc) so the app never writes into itself. The launcher runs it with
PYTHONHOME=dest and a clean environment.
"""
import shutil
import subprocess
import sys
from pathlib import Path

SOURCE = Path("/Library/Frameworks/Python.framework/Versions/3.13")
VERSION = "3.13"
STDLIB = SOURCE / "lib" / f"python{VERSION}"
SITE = STDLIB / "site-packages"
PACKAGES = ("PIL", "numpy")
# The standard library's parts the builder never uses, and the extension modules that would need libraries outside the
# standard library's (Tcl/Tk, ncurses, OpenSSL: hashlib has its own md5 and sha modules without _hashlib)
LEFT_OUT = {"test", "idlelib", "tkinter", "turtledemo", "turtle.py", "ensurepip", "pydoc_data", "site-packages",
            f"config-{VERSION}-darwin", "__pycache__"}
LEFT_OUT_EXTENSIONS = ("_test", "_ctypes_test", "_xxtestfuzz", "xxlimited", "xxsubtype", "_tkinter", "_curses", "_ssl",
                       "_hashlib")
SYSTEM = ("/usr/lib/", "/System/")
LIBPYTHON = f"libpython{VERSION}.dylib"


def run(*command, **kwargs):
    return subprocess.run(command, check=True, capture_output=True, text=True, **kwargs).stdout


def is_macho(path):
    with open(path, "rb") as f:
        return f.read(4) in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def _ignore(directory, names):
    left = {n for n in names if n in LEFT_OUT or n == "tests" or n.endswith(".pyc")}
    if Path(directory).name == "lib-dynload":
        left |= {n for n in names if n.startswith(LEFT_OUT_EXTENSIONS)}
    return left


def _dist_info(package):
    """The package's .dist-info folder (its licence, version and record), by the name pip installed it under."""
    wheel = {"PIL": "pillow", "numpy": "numpy"}[package]
    found = sorted(SITE.glob(f"{wheel}-*.dist-info"))
    if len(found) != 1:
        raise SystemExit(f"{SITE}: expected one {wheel} .dist-info, found {[f.name for f in found]}")
    return found[0]


def copy(dest):
    if not (SOURCE / "Python").exists():
        raise SystemExit(f"{SOURCE}: no Python {VERSION} framework (python.org's installer puts it there)")
    for package in PACKAGES:
        if not (SITE / package).is_dir():
            raise SystemExit(f"{SITE / package}: not installed ({SOURCE}/bin/python3 -m pip install pillow numpy)")
    if dest.exists():
        shutil.rmtree(dest)
    for folder in ("bin", "lib"):
        (dest / folder).mkdir(parents=True)
    # The interpreter itself: the framework's bin/python3.13 is a stub that starts this one from inside the framework
    shutil.copy2(SOURCE / "Resources/Python.app/Contents/MacOS/Python", dest / "bin" / "python3")
    shutil.copy2(SOURCE / "Python", dest / "lib" / LIBPYTHON)
    shutil.copytree(STDLIB, dest / "lib" / f"python{VERSION}", ignore=_ignore)
    site = dest / "lib" / f"python{VERSION}" / "site-packages"
    for package in PACKAGES:
        shutil.copytree(SITE / package, site / package, ignore=_ignore)
        shutil.copytree(_dist_info(package), site / _dist_info(package).name)

    machos = [p for p in dest.rglob("*") if p.is_file() and not p.is_symlink() and is_macho(p)]
    for path in machos:
        if "arm64" not in run("lipo", "-archs", str(path)).split():
            raise SystemExit(f"{path}: no arm64 code")
        if run("lipo", "-archs", str(path)).strip() != "arm64":
            run("lipo", "-thin", "arm64", str(path), "-output", str(path))
    for path in machos:  # the rpaths of the machines they were built on (Pillow's libjpeg has its CI's)
        for rpath in rpaths(path):
            if not rpath.startswith("@"):
                run("install_name_tool", "-delete_rpath", rpath, str(path))
    framework = str(SOURCE / "Python")
    run("install_name_tool", "-id", f"@rpath/{LIBPYTHON}", str(dest / "lib" / LIBPYTHON))
    run("install_name_tool", "-change", framework, f"@rpath/{LIBPYTHON}", "-add_rpath", "@executable_path/../lib",
        str(dest / "bin" / "python3"))
    outside = []
    for path in machos:
        own = run("otool", "-D", str(path)).splitlines()[1:]
        for line in run("otool", "-L", str(path)).splitlines()[1:]:
            reference = line.strip().split(" (compatibility")[0]
            if reference not in own and not reference.startswith(SYSTEM + ("@rpath/", "@loader_path/")):
                outside.append(f"{path.relative_to(dest)}: {reference}")
    if outside:
        raise SystemExit("the app's Python still loads from outside it:\n" + "\n".join(outside))
    for path in machos:
        run("codesign", "--force", "--sign", "-", str(path))

    # Compiled once here: the app is signed, and Python must never write its caches into it
    python = dest / "bin" / "python3"
    run(str(python), "-m", "compileall", "-q", "-j", "0", "--invalidation-mode", "unchecked-hash",
        str(dest / "lib" / f"python{VERSION}"), env=environment(dest))
    version = run(str(python), "-c", "import sys, PIL, numpy; print(sys.version.split()[0], PIL.__version__, numpy.__version__)",
                  env=environment(dest)).split()
    print(f"Python {version[0]}, Pillow {version[1]}, NumPy {version[2]}: {size(dest) / 1e6:.0f} MB")
    return [(STDLIB / "LICENSE.txt", "python/LICENSE.txt"),
            (_dist_info("PIL") / "licenses" / "LICENSE", "pillow/LICENSE"),
            (_dist_info("numpy") / "LICENSE.txt", "numpy/LICENSE.txt")]


def rpaths(binary):
    """A Mach-O's LC_RPATH paths."""
    lines = run("otool", "-l", str(binary)).splitlines()
    return [lines[i + 2].split()[1] for i, line in enumerate(lines) if line.strip() == "cmd LC_RPATH"]


def environment(home):
    """The whole environment the app's Python runs with: nothing of the user's (their PYTHONPATH, their site-packages)."""
    return {"PYTHONHOME": str(home), "PYTHONNOUSERSITE": "1", "PYTHONDONTWRITEBYTECODE": "1", "PATH": "/usr/bin:/bin",
            "LANG": "en_US.UTF-8"}


def size(folder):
    return sum(p.stat().st_size for p in folder.rglob("*") if p.is_file() and not p.is_symlink())


if __name__ == "__main__":
    copy(Path(sys.argv[1]))
