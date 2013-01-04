cmake_minimum_required (VERSION 2.8)
project (IliasAsync)

# Declare list variables.
list (APPEND extra_libs)	# Libs against which to link this library
list (APPEND hdrs)		# Headers for this library
list (APPEND srcs)		# Source files for this library

include (CheckCXXCompilerFlag)
include (CheckCXXSourceRuns)	# Required to properly test TLS.

include_directories (include)
include_directories (internal)

# Linux glibc needs exposure declaration.
# Keep it simple and pull in every standard they have.
# Also make sure their functions are thread-safe.
add_definitions (-D_REENTRANT -D_GNU_SOURCE)

# XCode support:
# - we require llvm, since gcc on OSX lacks a c++11 implementation,
# - we require libstdc++, since gcc's stdlib on apple is simply ancient.
if (APPLE)
	set (CMAKE_XCODE_ATTRIBUTE_GCC_VERSION "com.apple.compilers.llvm.clang.1_0")
	set (CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++0x")
	set (CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
endif ()

# Setup compiler flags.
if (CMAKE_COMPILER_IS_GNUCC)
	# GNU or clang compiler: enable optimization,
	# hide everything not explicitly made visible.
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden -fvisibility=hidden")
	set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3")
	set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wno-missing-noreturn -Wno-unused -Wsign-compare -Wuninitialized -Wno-unused-value")
elseif (WIN32)
	# MSVC compiler
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi")
	set (CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
	set (CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG")
	set (CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG} /DEBUG")
	# C++ throws exception, C never throws exceptions: /EHsc
	# Use multiple CPUs during compilation
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /MP")
	# Disable incremental linking
	# Enable large address awareness
	set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LARGEADDRESSAWARE /NXCOMPAT /INCREMENTAL:NO")
	set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LARGEADDRESSAWARE /NXCOMPAT /INCREMENTAL:NO")
	set (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /LARGEADDRESSAWARE /NXCOMPAT /INCREMENTAL:NO")
endif ()

# Check if the compiler can needs/handles c++11 flags.
check_cxx_compiler_flag("-std=c++11" STD_CXX11)
check_cxx_compiler_flag("-std=c++0x" STD_CXX0X)
if (STD_CXX11)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif (STD_CXX0X)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
endif ()

# This library requires threads,
# both for its thread pool and for the atomics.
find_package (Threads REQUIRED)
if (CMAKE_USE_PTHREADS_INIT)
	# XXX is this the correct way on all OSes?
	set (CMAKE_REQUIRED_DEFINITIONS "-pthread")
	set (CMAKE_REQUIRED_LIBRARIES "-pthread")
endif ()
list (APPEND extra_libs ${CMAKE_THREAD_LIBS_INIT})
# FreeBSD requires inclusion of librt, linux requires it for clock_gettime (XXX in the future, not used yet).
find_library (RT_LIB rt)
if (RT_LIB)
	list (APPEND extra_libs ${RT_LIB})
endif ()

# Test for TLS support: __thread compiler extension.
file (READ "CMake/source/tls__thread.cc" TLS_THREAD_CC)
mark_as_advanced (TLS_THREAD_CC)
check_cxx_source_runs ("${TLS_THREAD_CC}" HAS___THREAD)
# Test for TLS support: thread_local keyword.
file (READ "CMake/source/tls__thread_local.cc" TLS_THREADLOCAL_CC)
mark_as_advanced (TLS_THREADLOCAL_CC)
check_cxx_source_runs ("${TLS_THREADLOCAL_CC}" HAS_THREAD_LOCAL)
# Enable TLS work-around if required.
if (NOT HAS___THREAD AND NOT HAS_THREAD_LOCAL)
	# Add to srcs, since this is not a public header.
	list (APPEND srcs internal/tls_fallback.h)
	add_definitions (-DHAS_TLS=0)
else ()
	add_definitions (-DHAS_TLS=1)
endif ()


list (APPEND hdrs
	include/ilias/ll.h
	include/ilias/msg_queue.h
	include/ilias/promise.h
	include/ilias/threadpool.h
	include/ilias/workq.h
	)
list (APPEND srcs
	src/ll.cc
	src/msg_queue.cc
	src/promise.cc
	src/threadpool.cc
	src/workq.cc
	)
add_definitions (-Dilias_async_EXPORTS)		# Enable export of library interface.

# Actual library target.
add_library (ilias_async
	${hdrs}
	${srcs}
	)
target_link_libraries (ilias_async ${extra_libs})
set_property (TARGET ilias_async PROPERTY VERSION "0.0")
set_property (TARGET ilias_async PROPERTY SOVERSION "0.0")
