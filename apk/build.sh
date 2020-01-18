#!/bin/bash

# Expected arguments
# 1. architecture (e.g. arm64-v8a)
# 2. path to buildtools dir 
# 3. path to android platform folder to use
# 4. absolute module path
# 5. output path

if [ $# -lt 5 ]; then
	echo "build.sh: Invalid arguments"
	exit 1
fi

arch=$1
BT=$2
PLATFORM=$3
libpath=$4
out=$5

cd $out

libpath=$libpath
libfile=$(basename -- $libpath)
libname=$(echo $libfile | sed -e "s/^lib//; s/\.so$//")
pkgname=$(echo $libname | sed -e "s/-/_/")
name=${libname^}

sed "s/SWA_APP/$name/g; \
	s/SWA_LIB/$libname/g; \
	s/SWA_PKG/$pkgname/g" \
	AndroidManifest.xml.in > AndroidManifest.xml

baseapk=base.swa.apk
apku=wip.swa.unsigned.apk
apk=$pkgname.swa.apk

# copy base apk and update manifest
cp $baseapk $apku
$BT/aapt package -u -M AndroidManifest.xml \
	-I "${PLATFORM}/android.jar" \
	-F $apku 

# make sure the module library is linked
ln -sf $libpath lib/$arch
$BT/aapt add $apku lib/$arch/$libfile

# sign apk
$BT/apksigner sign --ks keystore.jks \
	--ks-key-alias androidkey --ks-pass pass:android \
	--key-pass pass:android --out $apk \
	$apku

# adb install $apk
# adb logcat -s ny:V
