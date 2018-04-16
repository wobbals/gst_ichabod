cmake_minimum_required (VERSION 3.5)
project (GstIchabod)

set (CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")
set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")

file (GLOB SOURCES "gst_ichabod/*.c")
list (REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gst_ichabod/main.c")

message ("libcrane using sources: ${SOURCES}")

find_package (PkgConfig)
pkg_check_modules (GLIB REQUIRED glib-2.0)
pkg_check_modules (LIBUV REQUIRED libuv)
pkg_check_modules (LIBZIP REQUIRED libzip)
pkg_check_modules (LIBJANSSON REQUIRED jansson)
pkg_check_modules (LIBZMQ REQUIRED libzmq)
pkg_check_modules (LIBGSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules (LIBGSTREAMERBASE REQUIRED gstreamer-base-1.0)

# Curl is in like 4 different places on different OSes I've looked at.
# lazily attempt to load it but don't sweat it if there's a failure.
# This seems to work fine on a few linuxes and OSX
pkg_check_modules (LIBCURL curl)

# # Repair broken -framework flags from pkg_check_modules
# if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#   function(repair_framework_flags BAD_FLAG)
#     string(REPLACE "-framework;" "-framework " GOOD_FLAG "${BAD_FLAG}")
#     set(REPAIRED_FLAG "${GOOD_FLAG}" PARENT_SCOPE)
#   endfunction()
#   # "-framework;Security;-framework;CoreFoundation" to
#   # "-framework Security;-framework CoreFoundation"
#   repair_framework_flags("${LIBAVCODEC_LDFLAGS}")
#   set(LIBAVCODEC_LDFLAGS "${REPAIRED_FLAG}")
#   repair_framework_flags("${LIBAVFORMAT_LDFLAGS}")
#   set(LIBAVFORMAT_LDFLAGS "${REPAIRED_FLAG}")
#   repair_framework_flags("${LIBAVFILTER_LDFLAGS}")
#   set(LIBAVFILTER_LDFLAGS "${REPAIRED_FLAG}")
# endif()

link_libraries (${GLIB_LDFLAGS})
link_libraries (${LIBUV_LDFLAGS})
link_libraries (${LIBZIP_LDFLAGS})
link_libraries (${LIBJANSSON_LDFLAGS})
link_libraries (${LIBZMQ_LDFLAGS})
link_libraries (${LIBGSTREAMER_LDFLAGS})
link_libraries (${LIBGSTREAMERBASE_LDFLAGS})
link_libraries (curl)

include_directories (
  ${GLIB_INCLUDE_DIRS}
  ${LIBUV_INCLUDE_DIRS}
  ${LIBZIP_INCLUDE_DIRS}
  ${LIBJANSSON_INCLUDE_DIRS}
  ${LIBCURL_INCLUDE_DIRS}
  ${LIBZMQ_INCLUDE_DIRS}
  ${LIBGSTREAMER_INCLUDE_DIRS}
  ${LIBGSTREAMERBASE_INCLUDE_DIRS}
)

# This comes at the end of all the linking commands issued above.
add_library (crane ${SOURCES})
link_libraries (crane)
add_executable (ichabod "gst_ichabod/main.c")
