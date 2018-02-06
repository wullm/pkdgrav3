cmake_minimum_required(VERSION 2.4)
project(pkdgrav3)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_RELEASE "-O3")

if(UNIX)
  set(CMAKE_REQUIRED_LIBRARIES m)
  set(HAVE_LIBM 1)
endif()


include(CheckCXXCompilerFlag)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_OPT_ARCH_NATIVE_SUPPORTED)
if (COMPILER_OPT_ARCH_NATIVE_SUPPORTED)
  add_compile_options(-march=native)
endif()

find_package(GSL REQUIRED)      # GNU Scientific Library
#add_definitions(-DHAVE_CONFIG_H)

# _GNU_SOURCE gives us more options
INCLUDE(CheckCSourceCompiles)
check_c_source_compiles("
#include <features.h>
#ifdef __GNU_LIBRARY__
  int main() {return 0;} 
#endif
" HAVE_GNU_SOURCE)
if (HAVE_GNU_SOURCE)
  set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
endif()

# Check for restrict keyword
# Builds the macro A_C_RESTRICT form automake
foreach(ac_kw __restrict __restrict__ _Restrict restrict)
  check_c_source_compiles(
  "
  typedef int * int_ptr;
  int foo (int_ptr ${ac_kw} ip) {
    return ip[0];
  }
  int main(){
    int s[1];
    int * ${ac_kw} t = s;
    t[0] = 0;
    return foo(t);
  }
  "
  RESTRICT)
  if(RESTRICT)
    set(ac_cv_c_restrict ${ac_kw})
    break()
  endif()
endforeach()
if(RESTRICT)
  add_definitions("-Drestrict=${ac_cv_c_restrict}")
else()
  add_definitions("-Drestrict=")
endif()


INCLUDE (CheckIncludeFiles)
CHECK_INCLUDE_FILES (malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILES (sys/time.h HAVE_SYS_TIME_H)
CHECK_INCLUDE_FILES (sys/stat.h HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILES (unistd.h HAVE_UNISTD_H)

INCLUDE(CheckSymbolExists)
check_symbol_exists(floor math.h HAVE_FLOOR)
check_symbol_exists(pow math.h HAVE_POW)
check_symbol_exists(sqrt math.h HAVE_SQRT)
check_symbol_exists(strchr string.h HAVE_STRCHR)
check_symbol_exists(strrchr string.h HAVE_STRRCHR)
check_symbol_exists(strdup string.h HAVE_STRDUP)
check_symbol_exists(strstr string.h HAVE_STRSTR)
check_symbol_exists(memmove string.h HAVE_MEMMOVE)
check_symbol_exists(memset string.h HAVE_MEMSET)
check_symbol_exists(gettimeofday sys/time.h HAVE_GETTIMEOFDAY)

check_symbol_exists(wordexp wordexp.h HAVE_WORDEXP)
check_symbol_exists(wordfree wordexp.h HAVE_WORDFREE)
check_symbol_exists(glob glob.h HAVE_GLOB)
check_symbol_exists(globfree glob.h HAVE_GLOBFREE)
check_symbol_exists(gethostname unistd.h HAVE_GETHOSTNAME)
check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
check_symbol_exists(mkdir sys/stat.h HAVE_MKDIR)
check_symbol_exists(strverscmp string.h HAVE_STRVERSCMP)

check_symbol_exists(backtrace execinfo.h USE_BT)

#AC_CHECK_FUNCS([gethrtime read_real_time time_base_to_time clock_gettime mach_absolute_time])
check_symbol_exists(atexit stdlib.h HAVE_ATEXIT)

add_subdirectory(mdl2)
add_executable(${PROJECT_NAME} "")
target_sources(${PROJECT_NAME} PRIVATE
	main.c cosmo.c master.c pst.c fio.c illinois.c param.c
	pkd.c analysis.c smooth.c smoothfcn.c outtype.c output.c
	walk2.c grav2.c ewald.cxx ic.cxx tree.c opening.cxx pp.cxx pc.cxx cl.c
	lst.c moments.c ilp.c ilc.c iomodule.c
	fof.c hop.c group.c groupstats.c RngStream.c listcomp.c healpix.c
)
if(USE_BT)
target_sources(${PROJECT_NAME} PRIVATE bt.c)
endif()


#if USE_SIMD
#if USE_SIMD_FMM
target_sources(${PROJECT_NAME} PRIVATE vmoments.cxx)
#endif
#if USE_SIMD_LC
target_sources(${PROJECT_NAME} PRIVATE lightcone.cxx)
#endif
#endif


CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/pkd_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/pkd_config.h)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} mdl2 openpa)
target_include_directories(${PROJECT_NAME} PRIVATE ${GSL_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GSL_LIBRARIES})

