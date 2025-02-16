#!/bin/bash

NJOBS=`nproc`
BASE_DIR=$PWD

if [[ `basename $PWD` != "nulix" ]]; then
	echo "This script must be run from main/root directory"
	exit 1
fi

# create a build directory
mkdir -p buildroot/build
cd buildroot/build
wget -c https://buildroot.org/downloads/buildroot-2024.02.10.tar.gz
tar -xzvf buildroot-2024.02.10.tar.gz

# configure buildroot
cd buildroot-2024.02.10
cp ../../config .config
sed -i "s|BR2_TOOLCHAIN_EXTERNAL_PATH=.*|BR2_TOOLCHAIN_EXTERNAL_PATH=\"$BASE_DIR\/musl\/musl-install\"|g" .config

# build
make -j$NJOBS