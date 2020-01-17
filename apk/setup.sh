#!/bin/bash

# Expected arguments
# 1. architecture (e.g. arm64-v8a)
# 2. path to buildtools dir 
# 3. path to android platform folder to use
# 4. path to AndroidManifest.xml.in
# 5. path to output folder

if [ $# -lt 5 ]; then
	echo "setup.sh: Invalid arguments"
	exit 1
fi

arch=$1
BT=$2
PLATFORM=$3
manifestInput=$4
out=$5

cd $out

mkdir -p lib/$arch
mkdir -p assets

# the first argument must be the full path of AndroidManifest.xml.in
# we copy it to the build dir to allow using it in build.sh
cp $manifestInput ./AndroidManifest.xml.in

# link libraries to the lib/$arch dir since they
# have to be there when packaged with aapt
ln -sf \
	/opt/android-ndk/sources/cxx-stl/llvm-libc++/libs/$arch/libc++_shared.so \
	lib/$arch

d="$(pwd)/.."
b="$d/../.."
ln -sf \
	$d/libswa.so \
	$d/subprojects/dlg/libdlg.so \
	lib/$arch

# generate dummy apk
apk=base.swa.apk
rm -f $apk

cp AndroidManifest.xml.in AndroidManifest.xml
$BT/aapt package -f -M AndroidManifest.xml \
	-I "${PLATFORM}/android.jar" \
	-F $apk 

$BT/aapt add $apk \
	lib/$arch/libc++_shared.so \
	lib/$arch/libswa.so \
	lib/$arch/libdlg.so \
