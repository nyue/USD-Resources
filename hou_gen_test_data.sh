#!/bin/sh
(
	export SKROTVIKTOR_DIR=$HOME/Applications/skrotViktor
	env PXR_PLUGINPATH_NAME=$SKROTVIKTOR_DIR/lib/usd/hairProcHoudini/resources \
		LD_LIBRARY_PATH=$SKROTVIKTOR_DIR/lib \
		PYTHONPATH=$SKROTVIKTOR_DIR/lib/python \
		hython hairProc/testenv/genHairProc.py
)
