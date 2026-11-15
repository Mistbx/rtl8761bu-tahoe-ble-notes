#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
sdk=$(xcrun --sdk macosx --show-sdk-path)
lilu=../dependencies/Lilu-1.7.2/Lilu
flags=(-arch x86_64 -isysroot "$sdk" -mmacosx-version-min=12.0 -mkernel -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -I../dependencies/MacKernelSDK-master/Headers -I"$sdk/System/Library/Frameworks/Kernel.framework/Headers" -I"$lilu" -O2 -Wall -DPRODUCT_NAME=RealtekEventMask -DMODULE_VERSION=0.2.3 -DLILU_CUSTOM_IOKIT_INIT=1)
for unit in RealtekEventMask.cpp "$lilu/Library/plugin_start.cpp"; do
 xcrun clang++ "${flags[@]}" -std=gnu++14 -fno-exceptions -fno-rtti -fapple-kext -c "$unit" -o "$(basename "$unit" .cpp).o"
done
xcrun clang "${flags[@]}" -c kmod_info.c -o kmod_info.o
mkdir -p RealtekEventMask.kext/Contents/MacOS
cp Info.plist RealtekEventMask.kext/Contents/
xcrun clang++ -arch x86_64 -isysroot "$sdk" -mmacosx-version-min=12.0 -fapple-kext -Xlinker -kext -nostdlib -lkmodc++ -lkmod -lcc_kext -o RealtekEventMask.kext/Contents/MacOS/RealtekEventMask RealtekEventMask.o plugin_start.o kmod_info.o
xattr -cr RealtekEventMask.kext
codesign --force --sign - RealtekEventMask.kext
codesign --verify --deep --strict RealtekEventMask.kext
