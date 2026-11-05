#!/usr/bin/env python3
"""Refresh this repo from a live work tree.

usage: snapshot.py SRC_TREE
Copies our own source (text only, allow-listed), and records every nested git
checkout as a pinned commit + local patch + overlay of untracked text files.
Never copies keys, device dumps, logs, build output, or Apple/vendor binaries.
"""
import csv, os, re, shutil, subprocess, sys, tarfile, tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]).resolve()
T = "lisa-tools"
MAX = 3 * 1024 * 1024
SERIAL = os.environ["LISA_SERIAL"]  # redacted from everything copied here
JUNK = re.compile(r"(^|/)(\.DS_Store|__pycache__|\.venv|venv|target)(/|$)|\.(pre-[\w-]+|orig|rej|bak|pyc|o|a|dylib|air|img|bin|ko|zst|log)$|\.before-")
SECRET = re.compile(r"(^|/)(owner\.pem|owner-root|\.env[^/]*|[^/]*\.p12|id_rsa[^/]*)(/|$)")
# Pinned only: proprietary blobs live in the working copy, so no patch or overlay.
PIN_ONLY = {f"{T}/research-lisa-vendor", f"{T}/theos-sdks", f"{T}/research-Mu-Silicium"}
# Needed for the current iOS 26 loop; the rest come with `bootstrap.sh --all`.
CORE = re.compile(rf"^(ios-sweet|{T}/(research-UniXNU-ios26|research-none24smu-UniXNU-zeta|research-xnu-12377|research-metal2vulkan|research-UniXNU-AdrenoMetal|ios26/gpu/[UZ]))(/|$)")
SKIP_SCAN = ("IOS-LISA-repo", ".cache", "build", "target", "out", "research", "node_modules", "xnu-deps")
stats = {"own": 0, "skipped": []}

def is_text(p):
    if p.stat().st_size > MAX: return False
    with open(p, "rb") as f: return b"\0" not in f.read(65536)

def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    data = src.read_bytes()
    data = re.sub(rb"^S=" + SERIAL.encode() + rb"$", b'S="${LISA_SERIAL:?set LISA_SERIAL to the fastboot serial of your phone}"', data, flags=re.M)
    data = data.replace(SERIAL.encode(), b"<device-serial>")
    dst.write_bytes(data)
    shutil.copymode(src, dst)

def git(path, *a, check=True):
    r = subprocess.run(["git", "-C", str(path), *a], capture_output=True)
    if check and r.returncode: raise SystemExit(f"git {a} in {path}: {r.stderr.decode()}")
    return r.stdout

def own(rel, exts=None, recurse=True):
    base = SRC / rel
    if base.is_file():
        files = [base]
    elif not base.is_dir():
        stats["skipped"].append(f"{rel} (missing)"); return
    else:
        files = sorted(base.rglob("*") if recurse else base.iterdir())
    for f in files:
        if not f.is_file() or f.is_symlink(): continue
        r = f.relative_to(SRC).as_posix()
        if JUNK.search(r) or SECRET.search(r): continue
        if exts and f.suffix not in exts: continue
        if not is_text(f):
            stats["skipped"].append(f"{r} (binary/large)"); continue
        copy(f, REPO / r); stats["own"] += 1

SCRIPT = {".sh", ".py", ".md", ".txt", ".h"}
for rel, exts, rec in [
    (".", {".md"}, False),
    (f"{T}", {".sh", ".py", ".md"}, False),
    (f"{T}/ipsw-release.json", None, True),
    (f"{T}/patches", None, True), (f"{T}/memory", None, True), (f"{T}/input", {".c", ".h", ".md"}, False),
    (f"{T}/lisa-gl/src", None, True), (f"{T}/lisa-gl", {".sh", ".py"}, False),
    (f"{T}/lisa-vulkan/src", None, True), (f"{T}/lisa-vulkan/build.sh", None, True),
    (f"{T}/lisa-vulkan/m2v-bridge/Cargo.toml", None, True), (f"{T}/lisa-vulkan/m2v-bridge/Cargo.lock", None, True),
    (f"{T}/lisa-metal-userland/src", None, True), (f"{T}/lisa-metal-userland/README.md", None, True),
    (f"{T}/lisa-metal-userland/build-lisa-metal.sh", None, True), (f"{T}/lisa-metal-userland/LisaMetal.plugin.plist", None, True),
    (f"{T}/ios26/lisa", SCRIPT, False), (f"{T}/ios26/lisa/springboard-overlay", None, True),
    (f"{T}/ios26/gpu/run-ci.sh", None, True),
    (f"{T}/diagnostic", {".c", ".h", ".sh", ".py", ".dts", ".md", ".S", ".s", ".ld", ".lds"}, False),
    # Our own bring-up tree (fork of ios-sweet); its device dumps stay out.
    ("lisa-build/README.md", None, True), ("lisa-build/NOTES.md", None, True), ("lisa-build/AGENTS.md", None, True),
    ("lisa-build/.gitignore", None, True), ("lisa-build/docs", None, True), ("lisa-build/.github", None, True),
    ("lisa-build/shim", None, True), ("lisa-build/tools", None, True), ("lisa-build/xnu", None, True),
]:
    own(rel, exts, rec)

# ---- nested checkouts ----
deps = REPO / "deps"
shutil.rmtree(deps, ignore_errors=True)
(deps / "patches").mkdir(parents=True); (deps / "overlay").mkdir(); (deps / "history").mkdir()

def find_repos():
    out = []
    for dp, dn, fn in os.walk(SRC):
        rel = Path(dp).relative_to(SRC).as_posix()
        if rel != "." and (".git" in dn or ".git" in fn):
            out.append(rel)
        dn[:] = [d for d in dn if d != ".git" and d not in SKIP_SCAN and not d.startswith("mount")
                 and not (rel in (T, f"{T}/ios26") and d in ("ios-artifacts", "kdk-expanded", "diagnostic", "dyld-symbols", "lisa-firmware", "kdk", "work", "lisa", "stock-firmware"))]
    return sorted(out)

rows = []
for rel in find_repos():
    if rel == "lisa-build":
        git(SRC / rel, "bundle", "create", str(deps / "history/lisa-build.bundle"), "--all")
        continue
    p = SRC / rel
    url = git(p, "remote", "get-url", "origin", check=False).decode().strip()
    if not url:
        stats["skipped"].append(f"{rel} (no origin)"); continue
    head = git(p, "rev-parse", "--verify", "-q", "HEAD", check=False).decode().strip()
    if not head:
        stats["skipped"].append(f"{rel} (no commits)"); continue
    reachable = git(p, "branch", "-r", "--contains", head, check=False).strip() or git(p, "tag", "--contains", head, check=False).strip()
    base = head
    if not reachable:
        for ref in ("@{upstream}", "origin/HEAD", "origin/main", "origin/master"):
            mb = subprocess.run(["git", "-C", str(p), "merge-base", head, ref], capture_output=True)
            if mb.returncode == 0:
                base = mb.stdout.decode().strip(); break
        else:
            stats["skipped"].append(f"{rel}: HEAD {head[:9]} not on any remote ref")
    name = rel.replace("/", "__")
    rows.append([rel, url, base, "core" if CORE.match(rel) else "extra"])
    if rel in PIN_ONLY: continue
    # --ignore-submodules: each submodule is its own row with its own patch.
    diff = git(p, "diff", "--binary", "--ignore-submodules=all", base, "--", ".")
    if diff:
        (deps / "patches" / f"{name}.patch").write_bytes(diff)
    for f in git(p, "ls-files", "-o", "--exclude-standard", "-z").decode().split("\0"):
        if not f or f.endswith("/"): continue
        src = p / f
        r = f"{rel}/{f}"
        if not src.is_file() or src.is_symlink() or JUNK.search(r) or SECRET.search(r): continue
        if not is_text(src):
            stats["skipped"].append(f"{r} (binary/large)"); continue
        copy(src, deps / "overlay" / name / f); stats["own"] += 1

with open(deps / "lock.tsv", "w", newline="") as f:
    w = csv.writer(f, delimiter="\t", lineterminator="\n")
    w.writerow(["path", "url", "commit", "tier"]); w.writerows(rows)

# ---- Mesa: not a git checkout, so diff against the release tarball ----
md = SRC / T / "research-hackios-userland/xnu-deps/build/mesa"
if (md / "mesa-25.0.7.tar.xz").exists():
    with tempfile.TemporaryDirectory() as td:
        with tarfile.open(md / "mesa-25.0.7.tar.xz") as tf: tf.extractall(td, filter="data")
        ex = ["-x", ".DS_Store", "-x", "__pycache__", "-x", "*.pre-*", "-x", "*.pyc", "-x", "*.orig"]
        r = subprocess.run(["diff", "-ruN", *ex, "mesa-25.0.7", str(md / "mesa-25.0.7")], cwd=td, capture_output=True)
        text = r.stdout.replace(str(md).encode() + b"/mesa-25.0.7", b"mesa-25.0.7")
        (deps / "patches" / "mesa-25.0.7-hackios.patch").write_bytes(text)
mw = SRC / T / "ios26/gpu/U/.cache/mesa-work"
if (mw / ".git").exists():
    (deps / "patches" / "gpu-U-mesa-work.patch").write_bytes(git(mw, "diff", "--binary", "HEAD"))
    (deps / "gpu-U-mesa-work.txt").write_text(f"{git(mw,'remote','get-url','origin',check=False).decode().strip()} {git(mw,'rev-parse','HEAD').decode().strip()}\n")
    for f in git(mw, "ls-files", "-o", "--exclude-standard", "-z").decode().split("\0"):
        if f and not JUNK.search(f) and (mw/f).is_file() and is_text(mw/f):
            copy(mw/f, deps/"overlay"/"gpu-U-mesa-work"/f)

print(f"copied {stats['own']} files, {len(rows)} pinned repos")
print("skipped:"); print("\n".join("  " + s for s in stats["skipped"]))
