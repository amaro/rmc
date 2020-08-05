#!/bin/bash

client_user=amaro
client_host=169.229.49.108
bf_user=root
bf_host=192.168.100.2

curdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
build_dir=build
script_dir=scripts
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
echo **Building ARM components**
source $bf_env_setup
make clean && make arm -j16
pushd $build_dir
upload nicserver $bf_user $bf_host /root
popd
)

# build x86 now
echo **Building x86 components**
make clean && make x86 -j16
pushd $build_dir
upload client $client_user $client_host /home/$client_user
upload normc_client $client_user $client_host /home/$client_user
popd
pushd $script_dir
upload analyze_output.py $client_user $client_host /home/$client_user
popd

popd
echo **DONE!**
