#!/bin/bash

client_user=amaro
client_host=169.229.49.108
bf_user=root
bf_host=192.168.100.2

curdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
build_dir=build
bf_env_setup=/opt/poky/2.5.3/environment-setup-aarch64-poky-linux

# $1 is file
# $2 is user
# $3 is host
# $4 is target dir
function upload {
    echo **uploading $1 to $3:$4**
    scp $1 $2@$3:$4
}

pushd $curdir/..

(
echo **Building nicserver for ARM**
source $bf_env_setup
make clean && make build/nicserver -j16
pushd $build_dir
upload nicserver $bf_user $bf_host /root
popd
)

# build x86 now
echo **Building client and hostserver for x86**
make clean && make build/client build/hostserver -j16
pushd $build_dir
upload client $client_user $client_host /home/$client_user
popd

popd
echo **DONE!**
