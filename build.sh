#!/bin/bash -eux

cd "$(dirname "$0")"
base=$(pwd)
mkdir -p opt
export DESTDIR=$(pwd)/opt

if which pacman; then
    pacman -S --needed mingw-w64-i686-{gcc,make,cmake,libuv}
    export CMAKE_GENERATOR='MSYS Makefiles'
    export CXXFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
    export CFLAGS="$CXXFLAGS"
fi

cd "$base"
mongo_version=1.10.2
if [[ ! -d "mongo-c-driver-$mongo_version" ]]; then
    wget "https://github.com/mongodb/mongo-c-driver/releases/download/$mongo_version/mongo-c-driver-$mongo_version.tar.gz" -O "mongo-c-driver-$mongo_version.tar.gz"
    tar -xf "mongo-c-driver-$mongo_version.tar.gz"
fi
cd "mongo-c-driver-$mongo_version"
mkdir -p cmake-build; cd cmake-build
cmake .. -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -DCMAKE_BUILD_TYPE=Release -DENABLE_BSON=ON -DENABLE_MONGOC=OFF -DENABLE_EXAMPLES=OFF
make -j4 && make install

cd "$base"
zydis_version=2.0.1
if [[ ! -d "zydis-$zydis_version" ]]; then
    wget "https://github.com/zyantific/zydis/archive/v$zydis_version.tar.gz" -O "zydis-$zydis_version.tar.gz"
    tar -xf "zydis-$zydis_version.tar.gz"
fi
cd "zydis-$zydis_version"
cmake . -DZYDIS_BUILD_EXAMPLES=NO -DZYDIS_BUILD_TOOLS=NO
make -j4 && make install

cd "$base"
cmake .
make -j4
