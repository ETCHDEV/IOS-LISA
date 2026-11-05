#!/bin/bash
# Recreate the checkouts this repo depends on, then reapply our local changes.
#   scripts/bootstrap.sh          what the current iOS 26 work needs
#   scripts/bootstrap.sh --all    also the iOS 16 kernel tree, Mesa and the reference repos
# Safe to rerun: a checkout that already exists is left alone.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
D="$ROOT/deps"
ALL=0; [ "${1:-}" = "--all" ] && ALL=1
export GIT_LFS_SKIP_SMUDGE=1 GIT_TERMINAL_PROMPT=0
fail=0

fetch() { # path url commit
	mkdir -p "$1"
	git -C "$1" init -q
	git -C "$1" remote add origin "$2" 2>/dev/null || true
	# LFS objects are not needed, and git-lfs may not be installed.
	git -C "$1" config filter.lfs.smudge cat
	git -C "$1" config filter.lfs.clean cat
	git -C "$1" config filter.lfs.process ""
	git -C "$1" config filter.lfs.required false
	git -C "$1" fetch -q --depth 1 origin "$3" && git -C "$1" checkout -q FETCH_HEAD
}

overlay() { # name path
	[ -d "$D/overlay/$1" ] && cp -R "$D/overlay/$1/." "$2/"
	return 0
}

while IFS=$'\t' read -r rel url commit tier; do
	[ "$rel" = path ] && continue
	[ "$tier" = core ] || [ "$ALL" = 1 ] || continue
	p="$ROOT/$rel"
	[ -e "$p/.git" ] && { echo "keep   $rel"; continue; }
	echo "fetch  $rel"
	if ! fetch "$p" "$url" "$commit"; then echo "FAILED $rel ($url $commit)"; fail=1; continue; fi
	name="${rel//\//__}"
	if [ -f "$D/patches/$name.patch" ]; then
		git -C "$p" apply --whitespace=nowarn "$D/patches/$name.patch" || { echo "PATCH FAILED $rel"; fail=1; }
	fi
	overlay "$name" "$p"
done < "$D/lock.tsv"

# UniXNU's scripts expect refer/xnu to point at the xnu-12377 checkout.
U="$ROOT/lisa-tools/research-UniXNU-ios26/refer"
[ -d "$U" ] && [ ! -e "$U/xnu" ] && ln -s ../../research-xnu-12377 "$U/xnu"
U="$ROOT/lisa-tools/ios26/gpu/U"
if [ -f "$D/gpu-U-mesa-work.txt" ] && [ -d "$U" ] && [ ! -e "$U/.cache/mesa-work/.git" ]; then
	read -r url commit < "$D/gpu-U-mesa-work.txt"
	echo "fetch  gpu/U Mesa work tree"
	if fetch "$U/.cache/mesa-work" "$url" "$commit"; then
		git -C "$U/.cache/mesa-work" apply --whitespace=nowarn "$D/patches/gpu-U-mesa-work.patch" || { echo "PATCH FAILED mesa-work"; fail=1; }
		overlay gpu-U-mesa-work "$U/.cache/mesa-work"
	else echo "FAILED mesa-work"; fail=1; fi
fi

# iOS 16 path: Mesa 25.0.7 from the release tarball plus our changes.
H="$ROOT/lisa-tools/research-hackios-userland"
if [ "$ALL" = 1 ] && [ -d "$H" ] && [ ! -d "$H/xnu-deps/build/mesa/mesa-25.0.7" ]; then
	M="$H/xnu-deps/build/mesa"; mkdir -p "$M"
	echo "fetch  Mesa 25.0.7"
	curl -sL --fail -o "$M/mesa-25.0.7.tar.xz" https://archive.mesa3d.org/mesa-25.0.7.tar.xz
	tar xJf "$M/mesa-25.0.7.tar.xz" -C "$M"
	patch -s -p1 -d "$M/mesa-25.0.7" < "$D/patches/mesa-25.0.7-hackios.patch" || { echo "PATCH FAILED mesa"; fail=1; }
fi

# iOS 16 userland build environment (ignored by that repo, so not in its patch).
if [ -d "$H" ] && [ ! -e "$H/xnu-env.sh" ]; then
	cat > "$H/xnu-env.sh" <<ENV
export THEOS="$H"
export XNU_SDK_VERSION=16.5
export XNUDEPS="$H/xnu-deps"
export PATH="\$XNUDEPS/host/bin:/opt/homebrew/opt/coreutils/libexec/gnubin:/opt/homebrew/bin:\$PATH"
ENV
	[ -e "$H/sdks" ] || ln -s ../theos-sdks "$H/sdks"
fi

# A few files record absolute paths from the original machine.
if [ "$ROOT" != /Users/arjun/IOS-LISA ]; then
	for f in lisa-tools/lisa-vulkan/m2v-bridge/Cargo.toml lisa-tools/check-uncompressed-ios-stage.sh lisa-tools/research-hackios-kernel/common/devices/lisa.conf; do
		[ -f "$ROOT/$f" ] && sed -i '' "s|/Users/arjun/IOS-LISA|$ROOT|g" "$ROOT/$f"
	done
fi

[ "$fail" = 0 ] && echo "bootstrap OK" || { echo "bootstrap finished with errors"; exit 1; }
