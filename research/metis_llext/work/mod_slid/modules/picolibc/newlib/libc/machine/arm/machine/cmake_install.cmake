# Install script for directory: /home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine

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

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/machine" TYPE FILE FILES
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine/acle-compat.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine/_endian.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine/fenv.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine/fenv-fp.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine/math.h"
    "/home/adhyan/Desktop/Compiler/corpora/modules/lib/picolibc/newlib/libc/machine/arm/machine/param.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/adhyan/Desktop/Compiler/research/metis_llext/work/mod_slid/modules/picolibc/newlib/libc/machine/arm/machine/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
