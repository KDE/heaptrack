#!/bin/sh

cd $(dirname $0)/..

mkdir -p output

docker run -it --rm \
    -v $PWD:/github/workspace/ \
    --user $(id -u):$(id -g) \
    heaptrack-appimage \
    /github/workspace/tools/build_appimage.sh /github/workspace /github/workspace/output/build-appimage
