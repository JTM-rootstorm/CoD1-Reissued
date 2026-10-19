cmake_minimum_required(VERSION 3.25)
project(cod1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# x86recomp's runtime and HLE modules are shared libraries: rebuilding one
# does not relink the game.
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)

# x86recomp: the lifter, x87 model and runtime (a sibling checkout).
set(X86RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../x86recomp"
    CACHE PATH "x86recomp source tree")
# Graphics go through x86recomp's SDL_GPU backend (Vulkan, Metal on macOS).
# dxvk-native (d3d9 only, SDL3 WSI) is opt-in and not built by default:
# configure with -DDXVK_NATIVE_DIR=<its install prefix> (as the Dungeon
# Siege project builds it, ../../Dungeon Siege/tools_ext/dxvk-native) to add the d3d9
# reference backend (COD1_GFX=d3d9).
set(DXVK_NATIVE_DIR "" CACHE PATH "dxvk-native install prefix (opt-in d3d9 backend)")

enable_testing()
add_subdirectory("${X86RECOMP_DIR}" x86recomp)

# Host libraries in place of the EXE's statically linked copies.
find_package(ZLIB REQUIRED)

# The game: runtime + HLE + lifted code (generated/, produced by
# recomp_lift; see CLAUDE.md for the command).
recomp_add_lifted(cod1_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(TARGET cod1_lifted)
  add_executable(cod1 src/main.cpp src/cinematic.cpp src/skinning.cpp src/cm_trace.cpp
                 src/fov.cpp src/video_mode.cpp src/frame_pacing.cpp
                 src/screen2d.cpp
                 src/pmove_fixed.cpp src/pmove.cpp src/unzip.cpp
                 src/zlib.cpp src/jpeg_idct.cpp src/crt.cpp src/xanim.cpp
                 $<TARGET_OBJECTS:cod1_lifted>)
  target_link_libraries(cod1 PRIVATE recomp_runtime recomp_mss32 recomp_d3d7
                        recomp_opengl ZLIB::ZLIB)
  set_target_properties(cod1 PROPERTIES ENABLE_EXPORTS ON)  # symbol names in fault backtraces
  if(DXVK_NATIVE_DIR)
    set_target_properties(cod1 PROPERTIES BUILD_RPATH "${DXVK_NATIVE_DIR}/lib64")
  endif()
  # Dev only: records calls of the CPU skinning functions for
  # skinning_oracle (tests/skinning_capture.cpp).
  option(COD1_SKINNING_CAPTURE "record CPU skinning calls (COD1_SKINNING_CAPTURE=file)" OFF)
  if(COD1_SKINNING_CAPTURE)
    target_sources(cod1 PRIVATE tests/skinning_capture.cpp)
    target_link_options(cod1 PRIVATE
      "LINKER:--wrap=_ZN6recomp3gen10f_005070E0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_00507D20ERNS_3CtxE")
  endif()
  # Dev only: records collision traces, point contents and the clip map for
  # cm_trace_oracle (tests/cm_trace_capture.cpp).
  option(COD1_CM_CAPTURE "record collision traces (COD1_CM_CAPTURE=file)" OFF)
  if(COD1_CM_CAPTURE)
    target_sources(cod1 PRIVATE tests/cm_trace_capture.cpp)
    target_link_options(cod1 PRIVATE
      "LINKER:--wrap=_ZN6recomp3gen10f_0041CD60ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041D380ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041D3B0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041D870ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041A730ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0041A820ERNS_3CtxE")
  endif()
  # Dev only: records Pmove calls with their traces for pmove_oracle
  # (tests/pmove_capture.cpp).
  option(COD1_PMOVE_CAPTURE "record Pmove calls (COD1_PMOVE_CAPTURE=file)" OFF)
  if(COD1_PMOVE_CAPTURE)
    target_sources(cod1 PRIVATE tests/pmove_capture.cpp)
    target_link_options(cod1 PRIVATE
      "LINKER:--wrap=_ZN6recomp3gen10f_20008580ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_30007C30ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_20042B00ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_20042C70ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_3001EC30ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_3001ECC0ERNS_3CtxE")
  endif()
  # Dev only: records XAnim evaluation calls for xanim_oracle
  # (tests/xanim_capture.cpp).
  option(COD1_XANIM_CAPTURE "record XAnim evaluation calls (COD1_XANIM_CAPTURE=file)" OFF)
  if(COD1_XANIM_CAPTURE)
    target_sources(cod1 PRIVATE tests/xanim_capture.cpp)
    target_link_options(cod1 PRIVATE
      "LINKER:--wrap=_ZN6recomp3gen10f_0047CD80ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_004766E0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_00478FC0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0045D580ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_004761F0ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0047CC90ERNS_3CtxE"
      "LINKER:--wrap=_ZN6recomp3gen10f_0045D530ERNS_3CtxE")
  endif()
endif()
# The CPU skinning is bit-exact only without contraction into FMA, and is
# per-vertex work: optimised in every build type. It reads guest memory
# like lifted code, so a fault in it may unwind the same way
# (-fnon-call-exceptions and, on macOS, frame records in leaf functions:
# see recomp_add_lifted).
set(_skinning_options -O2 -ffp-contract=off -fnon-call-exceptions
    ${RECOMP_FAULT_FRAME_FLAGS})
set_source_files_properties(src/skinning.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options}")
# The collision traces likewise (x87 results in host doubles, src/x87_value.h),
# reading the clip map through overlays of guest memory.
set_source_files_properties(src/cm_trace.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options};-fno-strict-aliasing")
# The float IDCT likewise (x87 results in host doubles).
set_source_files_properties(src/jpeg_idct.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options};-fno-strict-aliasing")
# The XAnim evaluation likewise (x87 results in host doubles, guest memory
# overlays).
set_source_files_properties(src/xanim.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options};-fno-strict-aliasing")
# The pk3 reads overlay unzip's structures on guest memory.
set_source_files_properties(src/unzip.cpp PROPERTIES COMPILE_OPTIONS
  "-fnon-call-exceptions;${RECOMP_FAULT_FRAME_FLAGS};-fno-strict-aliasing")
# Player movement as the traces (x87 results in host doubles, guest memory
# overlays), with the originals' wrapping integer arithmetic.
set_source_files_properties(src/pmove.cpp PROPERTIES COMPILE_OPTIONS
  "${_skinning_options};-fno-strict-aliasing;-fwrapv")

# Oracle tests over every instruction form in the EXE. Configure with
#   -DCOD1_GAME_EXE=unpacked/CoDSP.exe
set(COD1_GAME_EXE "" CACHE FILEPATH "unpacked CoDSP.exe for EXE-derived tests")
recomp_add_lift_tests(EXE "${COD1_GAME_EXE}"
  SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
  MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest")

# The native CPU skinning (src/skinning.cpp) against the original
# functions, lifted from COD1_GAME_EXE at build time with --only (into the
# build tree) and run on the SSE model: tests/skinning_test.cpp. Captures
# of real calls (tests/skinning_capture.cpp) can be passed as arguments.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/skinning_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            --only 005070E0,00507D20,0051CA3C,0051D2B0,0051D2C1,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
    COMMENT "Lifting the CPU skinning functions for skinning_oracle_test")
  add_executable(skinning_oracle_test tests/skinning_test.cpp src/skinning.cpp ${_gen})
  target_include_directories(skinning_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(skinning_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  add_test(NAME skinning_oracle COMMAND skinning_oracle_test)
endif()

# The native collision traces (src/cm_trace.cpp) against the original
# functions, lifted from COD1_GAME_EXE like skinning_oracle's (listed too:
# the leaf callback 0x41A4D0, called through a pointer, and the cases of
# 0x429800, which fills its own jump table):
# tests/cm_trace_test.cpp, on random clip maps, and on captured calls
# (tests/cm_trace_capture.cpp) listed in COD1_CM_CAPTURES.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/cm_trace_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            --only 0041CD60,0041D380,0041D3B0,0041D870,0041A730,0041A820,0041A4D0,00429888,004298BB,004298EE,00429921,00429954,00429987,004299B7,004299E7,0051CA3C,0051D2B0,0051D2C1,0052A7C2,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
    COMMENT "Lifting the collision traces for cm_trace_oracle_test")
  add_executable(cm_trace_oracle_test tests/cm_trace_test.cpp tests/cm_trace_native.cpp ${_gen})
  target_include_directories(cm_trace_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(cm_trace_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  set_source_files_properties(tests/cm_trace_native.cpp PROPERTIES COMPILE_OPTIONS
    "${_skinning_options};-fno-strict-aliasing")
  set(COD1_CM_CAPTURES "" CACHE STRING "captured traces for cm_trace_oracle (a list of files)")
  add_test(NAME cm_trace_oracle COMMAND cm_trace_oracle_test "${COD1_GAME_EXE}" ${COD1_CM_CAPTURES})
endif()

# The native pk3 reads (src/unzip.cpp) against the original unzip and
# inflate, lifted from COD1_GAME_EXE like skinning_oracle's (with unzOpen,
# unzGoToFirstFile/NextFile and the swap functions they call through
# pointers, and inflate's allocators; the CRT calls under them are stubs,
# tests/crt_stubs.manifest): tests/unzip_test.cpp, over a zip it makes and
# every pk3 in COD1_GAME_DIR/Main.
set(COD1_GAME_DIR "" CACHE PATH "the game's install, for the pk3 oracles")
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/unzip_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  set(_manifest "${_dir}/unzip_test.manifest")
  add_custom_command(OUTPUT ${_gen}
    COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/crt_stubs.manifest" > "${_manifest}"
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${_manifest}"
            --only 0049DB90,0049DA10,0049DE20,0049E2A0,0049E2E0,0049E4D0,0049E6D0,0049E7F0,0049EAA0,004403D0,00440410,00497DC0,004996A0,00499710,004997A0,0049A2E0,0049A3A0,0049A3E0,0049AAF0,0049AE90,0049AED0,0049AF20,0049B030,0049B5A0,0049BA90,0049BB30,0049BCE0,0049EBB0,0049EBD0,0051CA3C,0051D2B0,0051D2C1,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/crt_stubs.manifest"
    COMMENT "Lifting unzip and inflate for unzip_oracle_test")
  add_executable(unzip_oracle_test tests/unzip_test.cpp tests/unzip_native.cpp ${_gen})
  target_include_directories(unzip_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(unzip_oracle_test PRIVATE recomp_x87 recomp_image ZLIB::ZLIB)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  set_source_files_properties(tests/unzip_native.cpp PROPERTIES COMPILE_OPTIONS
    "-fno-strict-aliasing")
  file(GLOB _pk3s "${COD1_GAME_DIR}/Main/*.pk3")
  add_test(NAME unzip_oracle COMMAND unzip_oracle_test "${COD1_GAME_EXE}" ${_pk3s})
endif()

# compress2 and uncompress (src/zlib.cpp) against the original zlib, lifted
# the same way (with deflate_stored/fast/slow, adler32 and the allocators,
# called through pointers): tests/zlib_test.cpp, on generated buffers and
# the contents of the pk3 entries in COD1_GAME_DIR/Main.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/zlib_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  set(_manifest "${_dir}/zlib_test.manifest")
  add_custom_command(OUTPUT ${_gen}
    COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/crt_stubs.manifest" > "${_manifest}"
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${_manifest}"
            --only 00497D00,0049D8A0,00498E00,00498FC0,004992C0,00497BD0,0049EBB0,0049EBD0,0051CA3C,0051D2B0,0051D2C1,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/crt_stubs.manifest"
    COMMENT "Lifting zlib for zlib_oracle_test")
  add_executable(zlib_oracle_test tests/zlib_test.cpp tests/zlib_native.cpp ${_gen})
  target_include_directories(zlib_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(zlib_oracle_test PRIVATE recomp_x87 recomp_image ZLIB::ZLIB)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  file(GLOB _pk3s "${COD1_GAME_DIR}/Main/*.pk3")
  add_test(NAME zlib_oracle COMMAND zlib_oracle_test "${COD1_GAME_EXE}" ${_pk3s})
endif()

# The native CRT functions (src/crt.cpp) against the originals, lifted the
# same way: tests/crt_test.cpp.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/crt_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            --only 0051B3DD,0051D2C1 --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
    COMMENT "Lifting tolower for crt_oracle_test")
  add_executable(crt_oracle_test tests/crt_test.cpp tests/crt_native.cpp ${_gen})
  target_include_directories(crt_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(crt_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  add_test(NAME crt_oracle COMMAND crt_oracle_test "${COD1_GAME_EXE}")
endif()

# The native float IDCT (src/jpeg_idct.cpp) against the original
# jpeg_idct_float, lifted like skinning_oracle's: tests/jpeg_idct_test.cpp,
# on random blocks and every block of the JPEGs in COD1_GAME_DIR/Main's
# pk3s, whose coefficients it reads with the host's libjpeg.
if(EXISTS "${COD1_GAME_EXE}")
  find_package(JPEG REQUIRED)
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/jpeg_idct_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            --only 00519140 --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
    COMMENT "Lifting jpeg_idct_float for jpeg_idct_oracle_test")
  add_executable(jpeg_idct_oracle_test tests/jpeg_idct_test.cpp tests/jpeg_idct_native.cpp
                 ${_gen})
  target_include_directories(jpeg_idct_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(jpeg_idct_oracle_test PRIVATE recomp_x87 recomp_image JPEG::JPEG
                        ZLIB::ZLIB)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  set_source_files_properties(tests/jpeg_idct_native.cpp PROPERTIES COMPILE_OPTIONS
    "${_skinning_options};-fno-strict-aliasing")
  file(GLOB _pk3s "${COD1_GAME_DIR}/Main/*.pk3")
  add_test(NAME jpeg_idct_oracle COMMAND jpeg_idct_oracle_test "${COD1_GAME_EXE}" ${_pk3s})
endif()

# The native XAnim evaluation (src/xanim.cpp) against the original
# functions, lifted like skinning_oracle's, with the lifted functions they
# call stubbed (tests/xanim_stubs.manifest): tests/xanim_test.cpp, on random
# DObjs and anim trees, and on captured calls (tests/xanim_capture.cpp)
# listed in COD1_XANIM_CAPTURES.
if(EXISTS "${COD1_GAME_EXE}")
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/xanim_oracle")
  set(_gen "${_dir}/gen/gen_000.cpp" "${_dir}/gen/table.cpp")
  set(_manifest "${_dir}/xanim_test.manifest")
  add_custom_command(OUTPUT ${_gen}
    COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/xanim_stubs.manifest" > "${_manifest}"
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}"
            --symbols "${CMAKE_CURRENT_SOURCE_DIR}/symbols/CoDSP"
            --manifest "${_manifest}"
            --only 0047CD80,004766E0,0047CC90,0051CA3C,0051D2B0,0051D2C1,0052CDBF --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/CoDSP.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/xanim_stubs.manifest"
    COMMENT "Lifting the XAnim evaluation for xanim_oracle_test")
  add_executable(xanim_oracle_test tests/xanim_test.cpp tests/xanim_native.cpp ${_gen})
  target_include_directories(xanim_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(xanim_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  set_source_files_properties(tests/xanim_native.cpp PROPERTIES COMPILE_OPTIONS
    "${_skinning_options};-fno-strict-aliasing")
  set(COD1_XANIM_CAPTURES "" CACHE STRING "captured XAnim calls for xanim_oracle (a list of files)")
  add_test(NAME xanim_oracle COMMAND xanim_oracle_test "${COD1_GAME_EXE}" ${COD1_XANIM_CAPTURES})
endif()

# The native player movement (src/pmove.cpp) against the original Pmove of
# both game DLLs, next to COD1_MSS32_DLL in the game directory, lifted with
# their manifests (PM_Footsteps stays native: src/pmove.cpp) like
# skinning_oracle's, with the view angle functions for the earlier
# pmove_fixed steps, the CRT's initialisation of its float formatting (for
# the debug prints, with the functions it installs) and the cases of its
# atan2 for special operands (its descriptor's table, read by
# __trandisp2): tests/pmove_test.cpp, on random states, and on captured
# calls (tests/pmove_capture.cpp) listed in COD1_PMOVE_CAPTURES.
if(EXISTS "${COD1_GAME_EXE}" AND COD1_MSS32_DLL)
  get_filename_component(_game_dir "${COD1_MSS32_DLL}" DIRECTORY)
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/pmove_oracle")
  set(_gen "${_dir}/gen/gen_gamex86_000.cpp" "${_dir}/gen/gen_cgamex86_000.cpp"
      "${_dir}/gen/table.cpp")
  set(_sym "${CMAKE_CURRENT_SOURCE_DIR}/symbols")
  set(_man "${CMAKE_CURRENT_SOURCE_DIR}/manifest")
  add_custom_command(OUTPUT ${_gen}
    COMMAND recomp_lift --exe "${COD1_GAME_EXE}" --symbols "${_sym}/CoDSP"
            --manifest "${_man}/CoDSP.manifest"
            --module "${_game_dir}/gamex86.dll" --symbols "${_sym}/gamex86"
            --manifest "${_man}/gamex86.manifest"
            --module "${_game_dir}/cgamex86.dll" --symbols "${_sym}/cgamex86"
            --manifest "${_man}/cgamex86.manifest"
            --only 20008580,30007C30,20006FD0,30006710,20078B38,300407BC,2007F59A,2007F214,2007F279,2007F1D8,2007F25F,300471D5,30046E4F,30046EB4,30046E13,30046E9A,2008561D,2008561F,20085647,20085686,20085A8C,20085AE5,20085B08,3004E75D,3004E75F,3004E787,3004E7C6,3004EBCC,3004EC25,3004EC48 --insns-per-file 1000000 --out "${_dir}/gen"
    DEPENDS recomp_lift "${COD1_GAME_EXE}" "${_man}/gamex86.manifest" "${_man}/cgamex86.manifest"
    COMMENT "Lifting Pmove for pmove_oracle_test")
  add_executable(pmove_oracle_test tests/pmove_test.cpp tests/pmove_native.cpp ${_gen})
  target_include_directories(pmove_oracle_test PRIVATE "${X86RECOMP_DIR}/src")
  target_link_libraries(pmove_oracle_test PRIVATE recomp_x87 recomp_image)
  set_source_files_properties(${_gen} PROPERTIES COMPILE_OPTIONS
    "-O2;-g0;-fno-strict-aliasing;-fnon-call-exceptions")
  set_source_files_properties(tests/pmove_native.cpp PROPERTIES COMPILE_OPTIONS
    "${_skinning_options};-fno-strict-aliasing;-fwrapv")
  set(COD1_PMOVE_CAPTURES "" CACHE STRING "captured Pmove calls for pmove_oracle (a list of files)")
  add_test(NAME pmove_oracle COMMAND pmove_oracle_test "${_game_dir}" ${COD1_PMOVE_CAPTURES})
endif()

# x86recomp's Miles-compatible digital reverb, and its Miles 6.5 sample
# levels and Fast 2D, against the original routines of the installed
# game's mss32.dll (Miles 6.5e) and miles/msssoft.m3d next to it, lifted
# into the build tree at build time. Configure with
#   -DCOD1_MSS32_DLL="<Call of Duty>/mss32.dll"
set(COD1_MSS32_DLL "" CACHE FILEPATH
    "the game's mss32.dll for digital_reverb_oracle and fast2d_oracle")
recomp_add_digital_reverb_test(DLL "${COD1_MSS32_DLL}")
if(COD1_MSS32_DLL)
  get_filename_component(_miles_dir "${COD1_MSS32_DLL}" DIRECTORY)
  recomp_add_fast2d_test(MSS32 "${COD1_MSS32_DLL}" FAST2D "${_miles_dir}/miles/msssoft.m3d")
endif()
