#!/bin/sh

set -e

srcdir=$(readlink -f "$1")
buildir=$(readlink -f "$2")

if [ -z "$srcdir" ] || [ -z "$buildir" ]; then
    echo "usage: $0 <srcdir> <builddir>"
    exit 1
fi

gitversion=$(git -C "$srcdir" describe)

ZSTD=$(which zstd)

if [ -z "$ZSTD" ]; then
    echo "ERROR: cannot find zstd in PATH"
    exit 1
fi

if [ -z "$(which linuxdeploy)" ]; then
    echo "ERROR: cannot find linuxdeploy in PATH"
    exit 1
fi

. /opt/rh/gcc-toolset-14/enable

mkdir -p "$buildir" && cd "$buildir"
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH=/opt/rh/gcc-toolset-14/root/ \
    -DCMAKE_BUILD_TYPE=Release  -DAPPIMAGE_BUILD=ON "$srcdir"
make -j$(nproc)
rm -Rf appdir
make DESTDIR=appdir install

# copy the zstd binary into the app image
cp $ZSTD ./appdir/usr/bin/zstd

# Ensure we prefer the bundled libs also when calling dlopen, cf.: https://github.com/KDAB/hotspot/issues/89
mv "./appdir/usr/bin/heaptrack_gui" "./appdir/usr/bin/heaptrack_gui_bin"
cat << WRAPPER_SCRIPT > ./appdir/usr/bin/heaptrack_gui
#!/bin/bash
f="\$(readlink -f "\${0}")"
d="\$(dirname "\$f")"
unset QT_PLUGIN_PATH
LD_LIBRARY_PATH="\$d/../lib:\$LD_LIBRARY_PATH" "\$d/heaptrack_gui_bin" "\$@"
WRAPPER_SCRIPT
chmod +x ./appdir/usr/bin/heaptrack_gui

mv "./appdir/usr/lib/heaptrack/libexec/heaptrack_interpret" "./appdir/usr/lib/heaptrack/libexec/heaptrack_interpret_bin"
cat << WRAPPER_SCRIPT > ./appdir/usr/lib/heaptrack/libexec/heaptrack_interpret
#!/bin/bash
f="\$(readlink -f "\${0}")"
d="\$(dirname "\$f")"
LD_LIBRARY_PATH="\$d/../../:\$LD_LIBRARY_PATH" "\$d/heaptrack_interpret_bin" "\$@"
WRAPPER_SCRIPT
chmod +x ./appdir/usr/lib/heaptrack/libexec/heaptrack_interpret

# include breeze icons
mkdir -p "appdir/usr/share/icons/breeze"
cp -v "/usr/share/icons/breeze/breeze-icons.rcc" "appdir/usr/share/icons/breeze/"

# use the shell script as AppRun entry point
# also make sure we find the bundled zstd
cat << WRAPPER_SCRIPT > ./appdir/AppRun
#!/bin/bash
f="\$(readlink -f "\${0}")"
d="\$(dirname "\$f")"
bin="\$d/usr/bin"
PATH="\$PATH:\$bin" "\$bin/heaptrack" "\$@"
WRAPPER_SCRIPT
chmod +x ./appdir/AppRun

# tell the linuxdeploy qt plugin to include these platform plugins
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland-generic.so"

mkdir -p appdir/usr/plugins/wayland-shell-integration/
cp /usr/plugins/wayland-shell-integration/libxdg-shell.so appdir/usr/plugins/wayland-shell-integration/

linuxdeploy --appdir appdir --plugin qt \
    -e "./appdir/usr/lib/heaptrack/libexec/heaptrack_interpret_bin" \
    -e "./appdir/usr/lib/heaptrack/libheaptrack_preload.so" \
    -e "./appdir/usr/lib/heaptrack/libheaptrack_inject.so" \
    -e "./appdir/usr/bin/heaptrack_gui_bin" \
    -e "./appdir/usr/bin/zstd" \
    -l "/usr/lib64/libz.so.1" \
    -l /usr/lib64/libharfbuzz.so.0 \
    -l /usr/lib64/libfreetype.so.6 \
    -l /usr/lib64/libfontconfig.so.1 \
    -l /usr/lib64/libwayland-egl.so \
    -i "$srcdir/src/analyze/gui/128-apps-heaptrack.png" --icon-filename=heaptrack \
    -d "./appdir/usr/share/applications/org.kde.heaptrack.desktop" \
    --output appimage

mv Heaptrack*x86_64.AppImage "/github/workspace/heaptrack-$gitversion-x86_64.AppImage"
