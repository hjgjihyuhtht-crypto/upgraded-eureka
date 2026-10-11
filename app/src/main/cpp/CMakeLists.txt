# Kinect 3D Studio — código nativo (Etapa 2: comunicação USB com Kinect v1)
cmake_minimum_required(VERSION 3.22.1)
project(kinect3dstudio C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ---------------------------------------------------------------- libusb (LGPL-2.1)
# Compilado como biblioteca compartilhada separada (libusb1.0.so) para cumprir a
# LGPL: o usuário pode substituir a .so sem recompilar o restante do app.
set(LIBUSB_DIR ${TP}/libusb)
add_library(usb1.0 SHARED
    ${LIBUSB_DIR}/libusb/core.c
    ${LIBUSB_DIR}/libusb/descriptor.c
    ${LIBUSB_DIR}/libusb/hotplug.c
    ${LIBUSB_DIR}/libusb/io.c
    ${LIBUSB_DIR}/libusb/sync.c
    ${LIBUSB_DIR}/libusb/strerror.c
    ${LIBUSB_DIR}/libusb/os/linux_usbfs.c
    ${LIBUSB_DIR}/libusb/os/events_posix.c
    ${LIBUSB_DIR}/libusb/os/threads_posix.c
    ${LIBUSB_DIR}/libusb/os/linux_netlink.c)
if(ANDROID)
    target_include_directories(usb1.0 PRIVATE ${LIBUSB_DIR}/android)
else()
    # Compilação de verificação no host Linux (scripts/check_native_host.sh)
    target_include_directories(usb1.0 PRIVATE ${K3D_HOST_LIBUSB_CONFIG_DIR})
endif()
target_include_directories(usb1.0
    PRIVATE ${LIBUSB_DIR}/libusb/os
    PUBLIC  ${LIBUSB_DIR}/libusb)
target_compile_options(usb1.0 PRIVATE -fvisibility=hidden -pthread -Wno-unused-parameter)
if(ANDROID)
    target_link_libraries(usb1.0 PRIVATE log)
else()
    target_link_libraries(usb1.0 PRIVATE pthread)
endif()

# ---------------------------------------------------------- libfreenect (Apache-2.0)
set(FN_DIR ${TP}/libfreenect)
add_library(freenect_static STATIC
    ${FN_DIR}/src/core.c
    ${FN_DIR}/src/tilt.c
    ${FN_DIR}/src/cameras.c
    ${FN_DIR}/src/flags.c
    ${FN_DIR}/src/usb_libusb10.c
    ${FN_DIR}/src/registration.c
    ${FN_DIR}/src/audio.c
    ${FN_DIR}/src/loader.c
    usb_bridge/freenect_android_open.c)
set_target_properties(freenect_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(freenect_static
    PUBLIC ${FN_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/usb_bridge
    PRIVATE ${FN_DIR}/src)
target_compile_options(freenect_static PRIVATE -Wno-unused-parameter -Wno-sign-compare)
target_link_libraries(freenect_static PUBLIC usb1.0 m)

# ------------------------------------------------------------- ponte JNI do app
# --------------------------------------------- sensores (puro C++, testável no host)
add_library(k3d_sensors STATIC
    sensors/calibration.cpp
    sensors/colorize.cpp
    sensors/recording.cpp
    sensors/frame_source.cpp
    sensors/playback_source.cpp)
set_target_properties(k3d_sensors PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(k3d_sensors PUBLIC sensors)
target_compile_options(k3d_sensors PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
target_link_libraries(k3d_sensors PUBLIC freenect_static)

add_library(k3d_native SHARED
    usb_bridge/kinect_session.cpp
    usb_bridge/jni_bridge.cpp
    usb_bridge/crash_handler.cpp)
target_compile_options(k3d_native PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(k3d_native PRIVATE k3d_sensors freenect_static)
if(ANDROID)
    target_sources(k3d_native PRIVATE usb_bridge/jni_bitmap.cpp)
    target_link_libraries(k3d_native PRIVATE log jnigraphics)
    # Páginas de 16 KB (Android 15+)
    target_link_options(k3d_native PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(usb1.0 PRIVATE "-Wl,-z,max-page-size=16384")
else()
    find_package(JNI REQUIRED)
    target_include_directories(k3d_native PRIVATE ${JNI_INCLUDE_DIRS})
endif()

# ------------------------------------------- jogo Expedição Ativa (só Android)
# Mesmo código da versão de PC (pc/src): rastreador corporal, 5 jogos, menus e fotos, desenhados
# com raylib (licença zlib) numa NativeActivity (com.kinect3dstudio.game.GameActivity, processo
# ":game"). Fica dentro de libk3d_native.so para usar a mesma sessão do Kinect aberta pelo Kotlin.
if(ANDROID)
    set(PC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../pc)
    include(FetchContent)
    set(PLATFORM Android CACHE STRING "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO ON CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
    target_compile_options(raylib PRIVATE -w)

    set(EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
    set(EMBED_SRC)
    foreach(pair "assets/fonts/Poppins-Bold.ttf:font_Poppins_Bold" "assets/fonts/Poppins-Medium.ttf:font_Poppins_Medium"
                 "packaging/icon.png:app_icon_png")
        string(REPLACE ":" ";" parts "${pair}")
        list(GET parts 0 file)
        list(GET parts 1 sym)
        add_custom_command(OUTPUT ${EMBED_DIR}/${sym}.c
            COMMAND ${CMAKE_COMMAND} -DIN=${PC_DIR}/${file} -DOUT=${EMBED_DIR}/${sym}.c -DNAME=${sym}
                    -P ${PC_DIR}/cmake/embed_file.cmake
            DEPENDS ${PC_DIR}/${file})
        list(APPEND EMBED_SRC ${EMBED_DIR}/${sym}.c)
    endforeach()

    set(PCS ${PC_DIR}/src)
    target_sources(k3d_native PRIVATE
        game/game_bridge.cpp
        ${PCS}/body/body_tracker.cpp ${PCS}/body/sim_depth.cpp
        ${PCS}/game/game_common.cpp ${PCS}/game/river.cpp ${PCS}/game/rebound.cpp ${PCS}/game/dodge.cpp
        ${PCS}/game/leaks.cpp ${PCS}/game/bubbles.cpp ${PCS}/game/adventure.cpp ${PCS}/game/autopilot.cpp
        ${PCS}/kinect/body_source.cpp ${PCS}/kinect/device_watch.cpp ${PCS}/kinect/pc_session.cpp
        ${PCS}/app/main.cpp ${PCS}/app/app.cpp ${PCS}/app/fileio.cpp ${PCS}/app/render_common.cpp
        ${PCS}/app/ui.cpp ${PCS}/app/audio.cpp ${PCS}/app/silhouette.cpp ${PCS}/app/photos.cpp
        ${PCS}/app/scene_wait.cpp ${PCS}/app/scene_menu.cpp ${PCS}/app/scene_game.cpp
        ${PCS}/app/render_river.cpp ${PCS}/app/render_rebound.cpp ${PCS}/app/render_dodge.cpp
        ${PCS}/app/render_leaks.cpp ${PCS}/app/render_bubbles.cpp
        ${EMBED_SRC})
    target_include_directories(k3d_native PRIVATE ${PCS} usb_bridge ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(k3d_native PRIVATE raylib android EGL GLESv2 OpenSLES)
    # o NativeActivity procura ANativeActivity_onCreate (android_native_app_glue, dentro do raylib)
    target_link_options(k3d_native PRIVATE "-Wl,-u,ANativeActivity_onCreate")
endif()

# ----------------------------------------------- testes nativos (somente host)
if(NOT ANDROID)
    enable_testing()
    add_executable(session_smoke_test tests/session_smoke_test.cpp usb_bridge/kinect_session.cpp)
    target_include_directories(session_smoke_test PRIVATE usb_bridge)
    target_link_libraries(session_smoke_test PRIVATE k3d_sensors freenect_static)
    add_test(NAME session_smoke_test COMMAND session_smoke_test)
    set_tests_properties(session_smoke_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(sensors_test tests/sensors_test.cpp)
    target_link_libraries(sensors_test PRIVATE k3d_sensors)
    add_test(NAME sensors_test COMMAND sensors_test)
endif()
