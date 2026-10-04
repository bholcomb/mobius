#!/bin/bash
# Build the distribution runtime into dist/portable/: the "portable"
# configuration (-march=x86-64-v2 instead of -march=native), staged with
# the same layout as bin/. Standalone bundles are made from it.
#
# The build tool stages every configuration into the destination named in
# buildy.yaml (bin/), and only reads the workspace from a file named
# buildy.yaml. So for this build the staging destination in buildy.yaml is
# switched to dist/portable, and restored on exit, whatever happens.
set -e
cd "$(dirname "$0")/.."
# Dependencies are built per configuration only when first fetched, so
# build the portable variant of each one that lacks it.
for dep in sqlite3 glfw3; do
    dir=".buildy_cache/deps/$dep"
    override="buildy_config/dependencies/${dep}_build.yaml"
    if [ -d "$dir" ] && [ -f "$override" ] && [ ! -d "$dir/build/linux-x86_64-portable" ]; then
        echo "Building $dep (portable)"
        cp "$override" "$dir/buildy.yaml"
        (cd "$dir" && "$OLDPWD/buildy" -c portable -j "${JOBS:-8}") || { rm -f "$dir/buildy.yaml"; exit 1; }
        rm -f "$dir/buildy.yaml"
    fi
done

cp buildy.yaml .buildy.yaml.saved
trap 'mv -f .buildy.yaml.saved buildy.yaml' EXIT
sed -i 's|^  destination: "${workspace}"$|  destination: "${workspace}/dist/portable"|' buildy.yaml
grep -q 'dist/portable' buildy.yaml || { echo "build_portable: staging destination not found in buildy.yaml"; exit 1; }
./buildy -c portable -j "${JOBS:-8}" "$@"
echo "Portable runtime: dist/portable/bin"
