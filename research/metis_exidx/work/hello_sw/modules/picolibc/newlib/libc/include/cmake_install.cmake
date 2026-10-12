# Install script for directory: /home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/adhyan/local-toolchain/bin/arm-none-eabi-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/adhyan/Desktop/Compiler/research/metis_exidx/work/hello_sw/modules/picolibc/newlib/libc/include/sys/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/adhyan/Desktop/Compiler/research/metis_exidx/work/hello_sw/modules/picolibc/newlib/libc/include/machine/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/adhyan/Desktop/Compiler/research/metis_exidx/work/hello_sw/modules/picolibc/newlib/libc/include/ssp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/adhyan/Desktop/Compiler/research/metis_exidx/work/hello_sw/modules/picolibc/newlib/libc/include/rpc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/adhyan/Desktop/Compiler/research/metis_exidx/work/hello_sw/modules/picolibc/newlib/libc/include/arpa/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/alloca.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/argz.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/ar.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/assert.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/byteswap.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/cpio.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/ctype.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/devctl.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/dirent.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/endian.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/envlock.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/envz.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/errno.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/fastmath.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/fcntl.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/fenv.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/fnmatch.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/getopt.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/glob.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/grp.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/iconv.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/ieeefp.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/inttypes.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/langinfo.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/libgen.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/limits.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/locale.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/malloc.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/math.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/memory.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/ndbm.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/newlib.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/paths.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/picotls.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/pwd.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/regdef.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/regex.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/sched.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/search.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/setjmp.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/signal.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/spawn.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/stdint.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/stdnoreturn.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/stdlib.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/string.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/strings.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/tar.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/termios.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/time.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/uchar.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/unctrl.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/unistd.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/utime.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/utmp.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/wchar.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/wctype.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/wordexp.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/include/complex.h")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/adhyan/Desktop/Compiler/research/metis_exidx/work/hello_sw/modules/picolibc/newlib/libc/include/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
