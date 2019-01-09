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

cmake -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Release  -DAPPIMAGE_BUILD=ON ..
make -j$(nproc)
make DESTDIR=appdir install

# copy the zstd binary into the app image
cp $ZSTD ./appdir/$PREFIX/bin/zstd

linuxdeployqt "./appdir/$PREFIX/share/applications/org.kde.heaptrack.desktop" \
    -executable="./appdir/$PREFIX/lib/heaptrack/libexec/heaptrack_interpret" \
    -executable="./appdir/$PREFIX/lib/heaptrack/libheaptrack_preload.so" \
    -executable="./appdir/$PREFIX/lib/heaptrack/libheaptrack_inject.so" \
    -executable="./appdir/$PREFIX/bin/zstd" \
    -bundle-non-qt-libs

# Ensure we prefer the bundled libs also when calling dlopen, cf.: https://github.com/KDAB/hotspot/issues/89
mv "./appdir/$PREFIX/bin/heaptrack_gui" "./appdir/$PREFIX/bin/heaptrack_gui_bin"
cat << WRAPPER_SCRIPT > ./appdir/$PREFIX/bin/heaptrack_gui
#!/bin/bash
f="\$(readlink -f "\${0}")"
d="\$(dirname "\$f")"
LD_LIBRARY_PATH="\$d/../lib:\$LD_LIBRARY_PATH" "\$d/heaptrack_gui_bin" "\$@"
WRAPPER_SCRIPT
chmod +x ./appdir/$PREFIX/bin/heaptrack_gui

# include breeze icons
if [ -d /opt/share/icons/breeze ]; then
    cp -va /opt/share/icons/breeze ./appdir/$PREFIX/share/icons/
fi

# use the shell script as AppRun entry point
# also make sure we find the bundled zstd
rm ./appdir/AppRun
cat << WRAPPER_SCRIPT > ./appdir/AppRun
#!/bin/bash
f="\$(readlink -f "\${0}")"
d="\$(dirname "\$f")"
bin="\$d/$PREFIX/bin"
PATH="\$PATH:\$bin" "\$bin/heaptrack" "\$@"
WRAPPER_SCRIPT
chmod +x ./appdir/AppRun

# Actually create the final image
appimagetool ./appdir $OUTDIR/heaptrack-x86_64.AppImage
