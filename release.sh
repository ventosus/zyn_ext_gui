#/bin/bash

mkdir -p release
pushd release &> /dev/null

#for toolchain in armv7h-linux;
for toolchain in i686-linux x86_64-linux;
do
	echo $toolchain
	sudo rm -rf $toolchain
	mkdir $toolchain
	pushd $toolchain &> /dev/null
	cmake \
		-DCMAKE_TOOLCHAIN_FILE=../../cmake/$toolchain.cmake \
		-DCMAKE_BUILD_TYPE=Release \
		-DCPACK_SYSTEM_NAME="$toolchain" \
		-DPLUGIN_DEST=zyn_ext_gui.lv2 \
		../..
	make -j4
	sudo make package
	popd &> /dev/null
done

cp */*.zip .
popd &> /dev/null
