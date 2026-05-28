#!/bin/sh
(
	export SKROTVIKTOR_DIR=$HOME/Applications/skrotViktor
	env PXR_PLUGINPATH_NAME=$SKROTVIKTOR_DIR/lib/usd/hairProcHoudini/resources \
		LD_LIBRARY_PATH=$SKROTVIKTOR_DIR/lib \
		OCL_KERNEL_PATHS=$SKROTVIKTOR_DIR/ocl/kernels \
		houdini
)
