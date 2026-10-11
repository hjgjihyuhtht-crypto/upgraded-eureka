#!/usr/bin/env bash
# Compila no Linux e gera dist/KinectStudio-Linux.tar.gz.
# Dependências (Ubuntu/Debian): sudo apt install build-essential cmake git xorg-dev libgl1-mesa-dev
set -euo pipefail
PC="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$PC/.." && pwd)"
WORK="${WORK:-$ROOT/build-pc-linux}"
cmake -S "$PC" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release ${RAYLIB_DIR:+-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$RAYLIB_DIR} ${EXTRA_CMAKE_ARGS:-}
cmake --build "$WORK/build" -j"$(nproc)"
(cd "$WORK/build" && ./tracker_test && ./adventure_test && ./kinect_test && ./games_test --rapido && \
  if [ -x ./nite_test ]; then ./nite_test; fi)
PKG="$WORK/KinectStudio"
rm -rf "$PKG" && mkdir -p "$PKG/licencas"
cp "$WORK/build/KinectStudio" "$WORK/build/libusb-1.0.so" "$PKG/"
cp "$PC/packaging/LEIA-ME.txt" "$PC/packaging/51-kinect.rules" "$PKG/"
# OpenNI2 + driver Freenect (o NiTE2 é proprietário: NÃO vai no pacote, o usuário copia para nite/)
if [ -f "$WORK/build/libOpenNI2.so" ]; then
  cp "$WORK/build/libOpenNI2.so" "$PKG/"
  mkdir -p "$PKG/OpenNI2/Drivers" "$PKG/nite"
  cp "$WORK/build/OpenNI2/Drivers/libFreenectDriver.so" "$PKG/OpenNI2/Drivers/"
  cp "$PC/packaging/nite-COMO-INSTALAR.txt" "$PKG/nite/COMO-INSTALAR.txt"
  cp "$PC/third_party/OpenNI2/LICENSE" "$PKG/licencas/OpenNI2-Apache-2.0.txt"
  cp "$PC/third_party/OpenNI2/NOTICE" "$PKG/licencas/OpenNI2-NOTICE.txt"
fi
TP="$ROOT/app/src/main/cpp/third_party"
cp "$TP/libusb/COPYING" "$PKG/licencas/libusb-LGPL-2.1.txt"
cp "$TP/libfreenect/APACHE20" "$PKG/licencas/libfreenect-Apache-2.0.txt"
cp "$PC/assets/fonts/OFL.txt" "$PKG/licencas/Poppins-OFL.txt"
cat > "$PKG/kinect-studio.sh" <<'SH'
#!/bin/sh
cd "$(dirname "$0")" && LD_LIBRARY_PATH=. exec ./KinectStudio "$@"
SH
chmod +x "$PKG/kinect-studio.sh"
mkdir -p "$ROOT/dist"
tar -czf "$ROOT/dist/KinectStudio-Linux.tar.gz" -C "$WORK" KinectStudio
echo "Pronto: $ROOT/dist/KinectStudio-Linux.tar.gz"
