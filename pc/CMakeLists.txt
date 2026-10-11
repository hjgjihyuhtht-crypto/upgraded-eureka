# Kinect 3D Studio — versão para PC (Windows e Linux)
#
#   Linux:   cmake -S pc -B build-pc && cmake --build build-pc -j
#   Windows: veja pc/scripts/build_windows.sh (compilação cruzada com llvm-mingw)
#
# Reaproveita do projeto Android: libusb, libfreenect e o núcleo de sensores
# (calibração, colorização, gravação .k3drec).
cmake_minimum_required(VERSION 3.16)
project(KinectStudioPC C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(K3D_BUILD_APP "Compila o aplicativo gráfico (precisa de raylib)" ON)
option(K3D_BUILD_TESTS "Compila os testes" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(K3D_NITE_DEFAULT ON)
else()
  set(K3D_NITE_DEFAULT OFF)
endif()
option(K3D_WITH_NITE "Linux: OpenNI2 + driver Freenect + carregamento do NiTE2 (esqueleto)" ${K3D_NITE_DEFAULT})
if(K3D_WITH_NITE)
  # o driver do OpenNI2 é uma .so que leva o libfreenect dentro
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(ANDROID_CPP ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
set(TP ${ANDROID_CPP}/third_party)

# ------------------------------------------------------------------ libusb (LGPL-2.1, DLL/SO separada)
set(LIBUSB_DIR ${TP}/libusb)
set(USB_SRC
  ${LIBUSB_DIR}/libusb/core.c ${LIBUSB_DIR}/libusb/descriptor.c ${LIBUSB_DIR}/libusb/hotplug.c
  ${LIBUSB_DIR}/libusb/io.c ${LIBUSB_DIR}/libusb/sync.c ${LIBUSB_DIR}/libusb/strerror.c)
if(WIN32)
  list(APPEND USB_SRC
    ${LIBUSB_DIR}/libusb/os/events_windows.c ${LIBUSB_DIR}/libusb/os/threads_windows.c
    ${LIBUSB_DIR}/libusb/os/windows_common.c ${LIBUSB_DIR}/libusb/os/windows_usbdk.c
    ${LIBUSB_DIR}/libusb/os/windows_winusb.c ${LIBUSB_DIR}/libusb/libusb-1.0.def)
  set(USB_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/usbcfg_win)
  configure_file(config/libusb_config_windows.h ${USB_CONFIG_DIR}/config.h COPYONLY)
elseif(APPLE)
  list(APPEND USB_SRC ${LIBUSB_DIR}/libusb/os/darwin_usb.c ${LIBUSB_DIR}/libusb/os/events_posix.c ${LIBUSB_DIR}/libusb/os/threads_posix.c)
  message(WARNING "macOS: não testado.")
else()
  list(APPEND USB_SRC
    ${LIBUSB_DIR}/libusb/os/linux_usbfs.c ${LIBUSB_DIR}/libusb/os/events_posix.c
    ${LIBUSB_DIR}/libusb/os/threads_posix.c ${LIBUSB_DIR}/libusb/os/linux_netlink.c)
  set(USB_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/usbcfg_linux)
  configure_file(config/libusb_config_linux.h ${USB_CONFIG_DIR}/config.h COPYONLY)
endif()
add_library(usb-1.0 SHARED ${USB_SRC})
target_include_directories(usb-1.0 PRIVATE ${USB_CONFIG_DIR} ${LIBUSB_DIR}/libusb/os PUBLIC ${LIBUSB_DIR}/libusb)
if(WIN32)
  set_target_properties(usb-1.0 PROPERTIES PREFIX "lib" OUTPUT_NAME "usb-1.0")
  target_link_libraries(usb-1.0 PRIVATE setupapi ole32 advapi32)
  target_compile_options(usb-1.0 PRIVATE -Wno-everything)
else()
  target_compile_options(usb-1.0 PRIVATE -fvisibility=hidden -pthread -w)
  target_link_libraries(usb-1.0 PRIVATE pthread)
endif()

# ------------------------------------------------------------- libfreenect (Apache-2.0)
set(FN_DIR ${TP}/libfreenect)
add_library(freenect_static STATIC
  ${FN_DIR}/src/core.c ${FN_DIR}/src/tilt.c ${FN_DIR}/src/cameras.c ${FN_DIR}/src/flags.c
  ${FN_DIR}/src/usb_libusb10.c ${FN_DIR}/src/registration.c ${FN_DIR}/src/audio.c ${FN_DIR}/src/loader.c)
target_include_directories(freenect_static PUBLIC ${FN_DIR}/include PRIVATE ${FN_DIR}/src)
target_compile_options(freenect_static PRIVATE -w)
target_link_libraries(freenect_static PUBLIC usb-1.0)
if(NOT WIN32)
  target_link_libraries(freenect_static PUBLIC m)
endif()

# ------------------------------------------- núcleo de sensores (mesmo código do Android)
add_library(k3d_sensors STATIC
  ${ANDROID_CPP}/sensors/calibration.cpp ${ANDROID_CPP}/sensors/colorize.cpp
  ${ANDROID_CPP}/sensors/recording.cpp ${ANDROID_CPP}/sensors/frame_source.cpp
  ${ANDROID_CPP}/sensors/playback_source.cpp)
target_include_directories(k3d_sensors PUBLIC ${ANDROID_CPP}/sensors)
target_link_libraries(k3d_sensors PUBLIC freenect_static)
if(NOT WIN32)
  target_link_libraries(k3d_sensors PUBLIC pthread)
endif()

# ------------------------------------------------------- corpo + jogos (C++ puro, testável)
add_library(k3d_core STATIC
  src/body/body_tracker.cpp
  src/body/sim_depth.cpp
  src/game/game_common.cpp
  src/game/river.cpp
  src/game/rebound.cpp
  src/game/dodge.cpp
  src/game/leaks.cpp
  src/game/bubbles.cpp
  src/game/adventure.cpp
  src/game/autopilot.cpp)
target_include_directories(k3d_core PUBLIC src)
target_compile_options(k3d_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ------------------------------------------------------------------- Kinect no PC
add_library(k3d_kinect STATIC
  src/kinect/device_watch.cpp
  src/kinect/pc_session.cpp
  src/kinect/body_source.cpp)
target_include_directories(k3d_kinect PUBLIC src)
target_link_libraries(k3d_kinect PUBLIC k3d_core k3d_sensors freenect_static usb-1.0)
target_compile_options(k3d_kinect PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ------------------------------------------------ OpenNI2 + driver Freenect (só Linux)
# libOpenNI2.so (Apache-2.0) é compilado a partir do código em third_party/OpenNI2 e fica ao lado do
# executável; o driver OpenNI2/Drivers/libFreenectDriver.so (do libfreenect) faz o OpenNI2 enxergar o
# Kinect v1. O NiTE2 (proprietário) NÃO é distribuído: é carregado em tempo de execução se o usuário tiver.
if(K3D_WITH_NITE)
  set(NI2 ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenNI2)
  set(XNL ${NI2}/ThirdParty/PSCommon/XnLib)
  file(GLOB XNLIB_SRC ${XNL}/Source/*.cpp ${XNL}/Source/Linux/*.cpp)
  # USB do XnLib só é usado pelos drivers PrimeSense (que não compilamos): dispensa libudev.
  # A matriz simétrica depende de um Matrix3x3 sem implementação no XnLib (e o núcleo não usa).
  list(FILTER XNLIB_SRC EXCLUDE REGEX "XnUSB\\.cpp$|XnLinuxUSB(Device)?\\.cpp$|XnSytmmetricMatrix3x3\\.cpp$")
  file(GLOB NI2_CORE_SRC ${NI2}/Source/Core/*.cpp)
  file(GLOB NI2_JPEG_SRC ${NI2}/ThirdParty/LibJPEG/*.c)
  set_source_files_properties(${NI2_JPEG_SRC} PROPERTIES LANGUAGE CXX)
  add_library(OpenNI2 SHARED ${NI2_CORE_SRC} ${XNLIB_SRC} ${NI2_JPEG_SRC}
    ${NI2}/Source/Drivers/OniFile/Formats/XnCodec.cpp ${NI2}/Source/Drivers/OniFile/Formats/XnStreamCompression.cpp)
  target_include_directories(OpenNI2
    PUBLIC ${NI2}/Include
    PRIVATE ${XNL}/Include ${NI2}/Source/Drivers/OniFile/Formats ${NI2}/ThirdParty/LibJPEG)
  target_compile_definitions(OpenNI2 PRIVATE OPENNI2_EXPORT)
  target_compile_options(OpenNI2 PRIVATE -w -fvisibility=hidden -fno-strict-aliasing)
  target_link_libraries(OpenNI2 PRIVATE dl pthread rt)
  set_target_properties(OpenNI2 PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  set(FND ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreenectDriver/src)
  add_library(FreenectDriver SHARED ${FND}/DeviceDriver.cpp ${FND}/DepthStream.cpp ${FND}/ColorStream.cpp)
  target_include_directories(FreenectDriver PRIVATE ${FND} ${NI2}/Include ${FN_DIR}/src)
  target_compile_definitions(FreenectDriver PRIVATE PROJECT_VER="0.7.5")
  target_compile_options(FreenectDriver PRIVATE -w)
  target_link_libraries(FreenectDriver PRIVATE freenect_static m)
  set_target_properties(FreenectDriver PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/OpenNI2/Drivers
    BUILD_RPATH "\$ORIGIN/../.." INSTALL_RPATH "\$ORIGIN/../..")

  add_library(k3d_nite STATIC src/kinect/nite_tracker.cpp)
  target_include_directories(k3d_nite PUBLIC src)
  target_link_libraries(k3d_nite PUBLIC k3d_kinect OpenNI2 dl pthread)
  target_compile_definitions(k3d_nite PUBLIC K3D_HAVE_NITE=1)
  target_compile_options(k3d_nite PRIVATE -Wall -Wextra -Wno-unused-parameter)
  add_dependencies(k3d_nite FreenectDriver)
endif()

# ------------------------------------------------------------------- aplicativo
if(K3D_BUILD_APP)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
  set(SUPPORT_MODULE_RAUDIO ON CACHE BOOL "" FORCE)
  # raylib 5.5 (licença zlib). Para usar uma cópia local: -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/caminho/raylib
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  # Fontes embutidas no executável
  set(EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
  set(EMBED_SRC)
  foreach(pair "assets/fonts/Poppins-Bold.ttf:font_Poppins_Bold" "assets/fonts/Poppins-Medium.ttf:font_Poppins_Medium"
               "packaging/icon.png:app_icon_png")
    string(REPLACE ":" ";" parts "${pair}")
    list(GET parts 0 file)
    list(GET parts 1 sym)
    add_custom_command(OUTPUT ${EMBED_DIR}/${sym}.c
      COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/${file}
              -DOUT=${EMBED_DIR}/${sym}.c -DNAME=${sym} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file})
    list(APPEND EMBED_SRC ${EMBED_DIR}/${sym}.c)
  endforeach()

  set(K3D_GAME_SOURCES
    src/app/app.cpp
    src/app/fileio.cpp
    src/app/render_common.cpp
    src/app/ui.cpp
    src/app/audio.cpp
    src/app/silhouette.cpp
    src/app/photos.cpp
    src/app/scene_wait.cpp
    src/app/scene_menu.cpp
    src/app/scene_game.cpp
    src/app/render_river.cpp
    src/app/render_rebound.cpp
    src/app/render_dodge.cpp
    src/app/render_leaks.cpp
    src/app/render_bubbles.cpp
    ${EMBED_SRC})
  add_executable(KinectStudio src/app/main.cpp src/app/scene_sensors.cpp ${K3D_GAME_SOURCES})
  target_link_libraries(KinectStudio PRIVATE k3d_kinect raylib)
  if(K3D_WITH_NITE)
    target_link_libraries(KinectStudio PRIVATE k3d_nite)
  endif()
  target_include_directories(KinectStudio PRIVATE ${ANDROID_CPP}/sensors)
  if(WIN32)
    target_link_libraries(KinectStudio PRIVATE shell32 ole32 uuid)
  endif()
  target_compile_options(KinectStudio PRIVATE -Wall -Wno-unused-parameter -Wno-missing-field-initializers)
  if(NOT WIN32)
    set_target_properties(KinectStudio PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
  endif()
  if(WIN32)
    set_target_properties(KinectStudio PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(KinectStudio PRIVATE -static -mwindows)
    target_sources(KinectStudio PRIVATE packaging/windows/app.rc)
  endif()
endif()

# ------------------------------------------------------------------------ testes
if(K3D_BUILD_TESTS)
  # O jogo em modo Android (__ANDROID__) rodando no PC com um "Kotlin" falso: confere a ponte com a
  # GameActivity (fonte entregue, revogada e destruída). O build Android de verdade fica no Gradle.
  if(K3D_BUILD_APP AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(JNI QUIET COMPONENTS JVM)
    find_path(K3D_JNI_H jni.h PATHS ${JNI_INCLUDE_DIRS} /usr/lib/jvm/default-java/include $ENV{JAVA_HOME}/include)
    if(K3D_JNI_H)
      add_executable(android_mode_test
        tests/android_host/harness.cpp tests/android_host/android_stub.c tests/android_host/game_main.cpp
        ${ANDROID_CPP}/game/game_bridge.cpp ${K3D_GAME_SOURCES})
      target_compile_definitions(android_mode_test PRIVATE __ANDROID__)
      target_include_directories(android_mode_test PRIVATE tests/android_host ${ANDROID_CPP}/usb_bridge ${ANDROID_CPP}/sensors
                                 ${K3D_JNI_H} ${K3D_JNI_H}/linux)
      target_link_libraries(android_mode_test PRIVATE k3d_kinect raylib)
      target_compile_options(android_mode_test PRIVATE -w)
    else()
      message(STATUS "jni.h não encontrado: android_mode_test não será compilado")
    endif()
  endif()

  enable_testing()
  add_executable(tracker_test tests/tracker_test.cpp)
  target_link_libraries(tracker_test PRIVATE k3d_core)
  add_test(NAME tracker_test COMMAND tracker_test)

  add_executable(games_test tests/games_test.cpp)
  target_link_libraries(games_test PRIVATE k3d_core)
  add_test(NAME games_test COMMAND games_test)

  add_executable(adventure_test tests/adventure_test.cpp)
  target_link_libraries(adventure_test PRIVATE k3d_core)
  add_test(NAME adventure_test COMMAND adventure_test)

  add_executable(kinect_test tests/kinect_test.cpp)
  target_link_libraries(kinect_test PRIVATE k3d_kinect)
  add_test(NAME kinect_test COMMAND kinect_test)

  if(K3D_WITH_NITE)
    # NiTE FALSO, só para o teste (gera quadros com o simulador). Nunca vai para o pacote.
    add_library(fake_nite SHARED tests/fake_nite/fake_nite.cpp)
    target_link_libraries(fake_nite PRIVATE k3d_core)
    target_include_directories(fake_nite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenNI2/Include)
    set_target_properties(fake_nite PROPERTIES OUTPUT_NAME NiTE2 LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fake_nite
                          CXX_VISIBILITY_PRESET hidden)
    add_executable(nite_test tests/nite_test.cpp)
    target_link_libraries(nite_test PRIVATE k3d_nite)
    add_dependencies(nite_test fake_nite)
    set_target_properties(nite_test PROPERTIES BUILD_RPATH "\$ORIGIN")
    add_test(NAME nite_test COMMAND nite_test)
  endif()
endif()
