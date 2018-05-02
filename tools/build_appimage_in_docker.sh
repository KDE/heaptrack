#!/bin/sh

cd $(dirname $0)/../

if [ ! -d /tmp/heaptrack-appimage-artifacts ]; then
    mkdir /tmp/heaptrack-appimage-artifacts
fi

sudo docker build -t heaptrack_appimage -f tools/Dockerfile . || exit 1
sudo docker run -v /tmp/heaptrack-appimage-artifacts:/artifacts -it heaptrack_appimage
mv /tmp/heaptrack-appimage-artifacts/heaptrack-*.AppImage .
ls -latr heaptrack-*.AppImage | tail -n 1
