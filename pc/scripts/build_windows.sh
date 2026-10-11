#!/usr/bin/env bash
# Compila o Kinect 3D Studio para Windows (x64) a partir do Linux, com llvm-mingw,
# e gera dist/KinectStudio-Windows.zip.
set -euo pipefail
PC="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$PC/.." && pwd)"
WORK="${WORK:-$ROOT/build-pc-windows}"
LLVM_MINGW="${LLVM_MINGW:-$WORK/llvm-mingw}"
VER=20241217
if [ ! -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" ]; then
  mkdir -p "$WORK"
  echo "Baixando llvm-mingw $VER..."
  curl -sSL -o "$WORK/llvm-mingw.tar.xz" \
    "https://github.com/mstorsjo/llvm-mingw/releases/download/$VER/llvm-mingw-$VER-ucrt-ubuntu-20.04-x86_64.tar.xz"
  mkdir -p "$LLVM_MINGW"
  tar -xJf "$WORK/llvm-mingw.tar.xz" -C "$LLVM_MINGW" --strip-components=1
  rm "$WORK/llvm-mingw.tar.xz"
fi
cmake -S "$PC" -B "$WORK/build" -DCMAKE_TOOLCHAIN_FILE="$PC/cmake/toolchain-llvm-mingw.cmake" \
  -DLLVM_MINGW="$LLVM_MINGW" -DCMAKE_BUILD_TYPE=Release -DK3D_BUILD_TESTS=OFF ${RAYLIB_DIR:+-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$RAYLIB_DIR}
cmake --build "$WORK/build" -j"$(nproc)" --target KinectStudio
"$LLVM_MINGW/bin/llvm-strip" "$WORK/build/KinectStudio.exe" "$WORK/build/libusb-1.0.dll" || true

PKG="$WORK/KinectStudio"
rm -rf "$PKG" && mkdir -p "$PKG/licencas"
cp "$WORK/build/KinectStudio.exe" "$WORK/build/libusb-1.0.dll" "$PKG/"
cp "$PC/packaging/LEIA-ME.txt" "$PKG/"
TP="$ROOT/app/src/main/cpp/third_party"
cp "$TP/libusb/COPYING" "$PKG/licencas/libusb-LGPL-2.1.txt"
cp "$TP/libfreenect/APACHE20" "$PKG/licencas/libfreenect-Apache-2.0.txt"
cp "$PC/assets/fonts/OFL.txt" "$PKG/licencas/Poppins-OFL.txt"
RL="${RAYLIB_DIR:-$WORK/build/_deps/raylib-src}"
[ -f "$RL/LICENSE" ] && cp "$RL/LICENSE" "$PKG/licencas/raylib-zlib.txt"
# Fim de linha do Windows no LEIA-ME
sed -i 's/$/\r/' "$PKG/LEIA-ME.txt"
mkdir -p "$ROOT/dist"
(cd "$WORK" && rm -f "$ROOT/dist/KinectStudio-Windows.zip" && zip -qr "$ROOT/dist/KinectStudio-Windows.zip" KinectStudio)
echo "Pronto: $ROOT/dist/KinectStudio-Windows.zip"
