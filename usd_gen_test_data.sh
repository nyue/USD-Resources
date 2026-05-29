#!/bin/sh
if [ -z "$1" ]; then
    echo "Usage: $0 <SKROTVIKTOR_DIR>" >&2
    exit 1
fi

SKROTVIKTOR_DIR=$1

env PXR_PLUGINPATH_NAME=$SKROTVIKTOR_DIR/lib/usd/hairProc/resources \
    LD_LIBRARY_PATH=$SKROTVIKTOR_DIR/lib:$HOME/systems/peasyocl/head/lib:$HOME/systems/OpenUSD/24.03/python3.11/lib \
    PYTHONPATH=$SKROTVIKTOR_DIR/lib/python:$PYTHONPATH \
    OCL_KERNEL_PATHS=$SKROTVIKTOR_DIR/ocl/kernels \
    python testenv/genHairProc.py
