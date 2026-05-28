#!/bin/sh
if [ -z "$1" ]; then
    echo "Usage: $0 <SKROTVIKTOR_DIR>" >&2
    exit 1
fi

SKROTVIKTOR_DIR=$1

env PXR_PLUGINPATH_NAME=$SKROTVIKTOR_DIR/lib/usd/hairProcHoudini/resources \
    LD_LIBRARY_PATH=$SKROTVIKTOR_DIR/lib \
    PYTHONPATH=$SKROTVIKTOR_DIR/lib/python \
    hython testenv/genHairProc.py
