/* config.h do libusb para Windows (MinGW / llvm-mingw / MSYS2). */
#define DEFAULT_VISIBILITY /**/
#define ENABLE_LOGGING 1
#define PLATFORM_WINDOWS 1
#define PRINTF_FORMAT(a, b) __attribute__ ((__format__ (__printf__, a, b)))
#define HAVE_STRUCT_TIMESPEC 1
#define _WIN32_WINNT 0x0601
