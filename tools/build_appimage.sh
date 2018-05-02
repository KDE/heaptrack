#!/bin/sh

cd $(dirname $0)/../

OUTDIR=$PWD

PREFIX=/opt

if [ ! -z "$1" ]; then
    PREFIX=$1
fi

if [ ! -z "$2" ]; then
    OUTDIR="$2"
fi

ZSTD=$(which zstd)

if [ -z "$ZSTD" ]; then
    echo "ERROR: cannot find zstd in PATH"
    exit 1
fi

if [ -z "$(which linuxdeployqt)" ]; then
    echo "ERROR: cannot find linuxdeployqt in PATH"
    exit 1
fi

if [ -z "$(which appimagetool)" ]; then
    echo "ERROR: cannot find appimagetool in PATH"
    exit 1
fi

if [ ! -d build-appimage ]; then
    mkdir build-appimage
fi

cd build-appimage

cmake -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
make DESTDIR=appdir install

linuxdeployqt "./appdir/$PREFIX/share/applications/org.kde.heaptrack.desktop" \
    -executable="./appdir/$PREFIX/lib/heaptrack/libexec/heaptrack_interpret" \
    -executable="./appdir/$PREFIX/lib/heaptrack/libheaptrack_preload.so" \
    -executable="./appdir/$PREFIX/lib/heaptrack/libheaptrack_inject.so" \
    -executable="$ZSTD" \
    -bundle-non-qt-libs

# Ensure we prefer the bundled libs also when calling dlopen, cf.: https://github.com/KDAB/hotspot/issues/89
mv "./appdir/$PREFIX/bin/heaptrack_gui" "./appdir/$PREFIX/bin/heaptrack_gui_bin"
printf '#!/bin/bash\nf="$(readlink -f "${0}")"\nd="$(dirname "$f")"\nLD_LIBRARY_PATH="$d/../lib:$LD_LIBRARY_PATH" "$d/heaptrack_gui_bin" "$@"\n' > ./appdir/$PREFIX/bin/heaptrack_gui

chmod +x ./appdir/$PREFIX/bin/heaptrack_gui

# use the shell script as AppRun entry point
rm ./appdir/AppRun
ln -sr ./appdir/$PREFIX/bin/heaptrack ./appdir/AppRun

# Actually create the final image
appimagetool ./appdir $OUTDIR/heaptrack-$(git describe)-x86_64.AppImage
