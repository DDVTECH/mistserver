#!/bin/sh

pd=${PWD}
d=${PWD}/../
config="Release"

if [ ! -d ${d}/external ] ; then
    mkdir ${d}/external
fi

if [ ! -d ${d}/external/mbedtls ] ; then
    #prepare mbedtls for build
    cd ${d}/external/
    git clone https://github.com/diederickh/mbedtls
    
    cd ${d}/external/mbedtls
    git checkout -b dtls_srtp_support
    git merge 15179bfbaa794506c06f923f85d7c71f0dfd89e9

    git am < ${pd}/webrtc_mbedtls_keying_material_fix.diff
    if [ $? -ne 0 ] ; then
        echo "Failed to apply patch"
        exit
    fi
fi

if [ ! -d ${d}/build ] ; then
    mkdir ${d}/build
fi

if [ ! -d ${d}/installed ] ; then
    mkdir ${d}/installed
    #Build mbedtls
    mkdir -p ${d}/external/mbedtls/build
    cd ${d}/external/mbedtls/build
    cmake -DCMAKE_INSTALL_PREFIX=${d}/installed -DENABLE_PROGRAMS=Off ..
    cmake --build . --config ${config} --target install -- -j 8
fi


cd ${d}
export PATH="${PATH}:${d}/installed/include"
cmake -DCMAKE_CXX_FLAGS="-I${d}/installed/include/ -L${d}/installed/lib/" \
      -DCMAKE_PREFIX_PATH=${d}/installed/include \
      -DCMAKE_MODULE_PATH=${d}/installed/ \
      -DPERPETUAL=1 \
      -DDEBUG=3 \
      -GNinja \
      .

ninja

