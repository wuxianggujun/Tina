# Creates a Tina game project from the bundled template. Run in script mode:
#
#   cmake -DNAME=Wuxia -DDEST=~/Wuxia -P <sdk>/lib/cmake/Tina/TinaNewProject.cmake
#
# Two things happen. The template is copied, and its two identifiers are rewritten: `MyGame`
# is the C++ namespace and the CMake project name, `mygame` is the target name prefix. They
# are substituted separately because the two follow different casing conventions -- a single
# case-insensitive pass would name the executable `Wuxia.exe` on a case-sensitive filesystem
# and break nothing visibly until a Linux build.
#
# It also writes a CMakePresets.json carrying the configure arguments this SDK needs. That is
# the more valuable half: an installed Tina package cannot be configured with `-DTina_DIR=`
# alone, and each of the three missing arguments fails somewhere that does not name it. See
# the generated presets file for which and why.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED NAME OR NAME STREQUAL "")
    message(FATAL_ERROR
        "NAME is required: cmake -DNAME=<ProjectName> -DDEST=<directory> -P TinaNewProject.cmake")
endif()

# The name becomes a C++ namespace and a CMake target, so it has to be an identifier in both.
# Rejected here rather than at the consumer's first build, where the error would arrive as a
# syntax error inside a generated file the user did not write.
if(NOT NAME MATCHES "^[A-Za-z][A-Za-z0-9_]*$")
    message(FATAL_ERROR
        "NAME must be a C++ identifier (letter first, then letters, digits or underscore): '${NAME}'")
endif()

if(NOT DEFINED DEST OR DEST STREQUAL "")
    # Defaulting to ./<NAME> rather than erroring, because that is the only destination that
    # cannot collide with something the caller cares about.
    set(DEST "${CMAKE_CURRENT_BINARY_DIR}/${NAME}")
endif()
get_filename_component(DEST "${DEST}" ABSOLUTE)

# The template lives beside this script, since both are installed into the same package
# directory. Resolved relative to the script rather than to a variable so this works from any
# working directory.
get_filename_component(tina_new_project_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(tina_template_dir "${tina_new_project_script_dir}/templates/game-project")
if(NOT EXISTS "${tina_template_dir}/CMakeLists.txt")
    # An in-tree run finds the template two levels up instead, which is how this script is
    # tested before it is ever installed.
    set(tina_template_dir "${tina_new_project_script_dir}/../templates/game-project")
    get_filename_component(tina_template_dir "${tina_template_dir}" ABSOLUTE)
endif()
if(NOT EXISTS "${tina_template_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "No project template beside this script. Expected "
        "'${tina_new_project_script_dir}/templates/game-project'. A package built before "
        "templates were installed does not carry one; use the engine checkout instead.")
endif()

# Refusing a non-empty destination rather than merging into it: this writes several files and
# a partial overwrite of someone's existing project is not recoverable from here.
if(EXISTS "${DEST}")
    file(GLOB tina_dest_entries "${DEST}/*")
    if(tina_dest_entries)
        message(FATAL_ERROR "DEST is not empty, refusing to overwrite: ${DEST}")
    endif()
endif()

string(TOLOWER "${NAME}" tina_project_target_prefix)

file(COPY "${tina_template_dir}/" DESTINATION "${DEST}")

# Rewrite in every text file rather than in a hardcoded list, so a file added to the template
# later is covered without touching this script.
file(GLOB_RECURSE tina_generated_files RELATIVE "${DEST}" "${DEST}/*")
set(tina_rewritten_files "")
foreach(relative_file IN LISTS tina_generated_files)
    set(absolute_file "${DEST}/${relative_file}")
    if(IS_DIRECTORY "${absolute_file}")
        continue()
    endif()
    file(READ "${absolute_file}" contents)
    set(original_contents "${contents}")
    string(REPLACE "MyGame" "${NAME}" contents "${contents}")
    string(REPLACE "mygame" "${tina_project_target_prefix}" contents "${contents}")
    if(NOT contents STREQUAL original_contents)
        file(WRITE "${absolute_file}" "${contents}")
        list(APPEND tina_rewritten_files "${relative_file}")
    endif()
endforeach()

# The SDK prefix, derived from where this script was installed: <prefix>/lib/cmake/Tina. The
# include/ probe is what distinguishes an installed package from a run out of the engine
# checkout, where there is no prefix to point a consumer at.
get_filename_component(tina_sdk_prefix "${tina_new_project_script_dir}/../../.." ABSOLUTE)
set(tina_sdk_prefix_known TRUE)
if(NOT EXISTS "${tina_sdk_prefix}/include/tina")
    set(tina_sdk_prefix_known FALSE)
    set(tina_sdk_prefix "REPLACE-WITH-TINA-SDK-PREFIX")
endif()

if(NOT DEFINED CONFIG OR CONFIG STREQUAL "")
    set(CONFIG "Debug")
endif()

# The window/text backend packages behind the DesktopBootstrap component. glfw3 and Freetype are
# vcpkg (or system) packages that Tina links PRIVATE into its adapters, so a consumer of that
# component still resolves them at link time even though its own code never names them.
#
# This is per-component, not unconditional: a project using only Tina::GameSDK needs no prefix
# here at all. Left as a placeholder when not given, because guessing a path would produce
# presets that fail later and less clearly than presets that are visibly incomplete.
set(tina_dependency_prefix "")
if(DEFINED DEPS AND NOT DEPS STREQUAL "")
    file(TO_CMAKE_PATH "${DEPS}" tina_dependency_prefix)
    get_filename_component(tina_dependency_prefix "${tina_dependency_prefix}" ABSOLUTE)
else()
    set(tina_dependency_prefix "REPLACE-WITH-VCPKG-INSTALLED-DIR/x64-windows")
endif()

file(TO_CMAKE_PATH "${tina_sdk_prefix}" tina_sdk_prefix)

# No "generator" key on purpose. Naming one pins a Visual Studio version, and a version this
# machine does not have fails while probing VCTargetsPath -- an MSB8020 that never mentions
# the generator argument that caused it. Omitted, CMake picks an installed one.
#
# CMAKE_CONFIGURATION_TYPES is narrowed to the single config the SDK was installed with. A
# multi-config generator otherwise asks for four, and the imported targets carry
# IMPORTED_LOCATION for only the installed one, so the generate step fails once per module
# with "IMPORTED_LOCATION not set for imported target Tina::Runtime" -- a message that points
# at the engine rather than at the install.
file(WRITE "${DEST}/CMakePresets.json"
"{
  \"version\": 6,
  \"cmakeMinimumRequired\": { \"major\": 3, \"minor\": 25, \"patch\": 0 },
  \"configurePresets\": [
    {
      \"name\": \"default\",
      \"displayName\": \"${NAME} (${CONFIG}, installed Tina SDK)\",
      \"binaryDir\": \"\${sourceDir}/build\",
      \"cacheVariables\": {
        \"CMAKE_PREFIX_PATH\": \"${tina_sdk_prefix};${tina_dependency_prefix}\",
        \"CMAKE_CONFIGURATION_TYPES\": \"${CONFIG}\",
        \"CMAKE_BUILD_TYPE\": \"${CONFIG}\"
      }
    }
  ],
  \"buildPresets\": [
    {
      \"name\": \"default\",
      \"configurePreset\": \"default\",
      \"configuration\": \"${CONFIG}\"
    },
    {
      \"name\": \"content\",
      \"displayName\": \"content library only\",
      \"configurePreset\": \"default\",
      \"configuration\": \"${CONFIG}\",
      \"targets\": [ \"${tina_project_target_prefix}_content\" ]
    }
  ]
}
")

list(LENGTH tina_rewritten_files tina_rewritten_count)
message(STATUS "Created ${NAME} in ${DEST} (${tina_rewritten_count} files rewritten)")
if(NOT tina_sdk_prefix_known)
    message(STATUS
        "  CMakePresets.json needs CMAKE_PREFIX_PATH filled in: this script ran from an engine "
        "checkout, not an installed SDK, so there is no prefix to record.")
endif()
if(NOT DEFINED DEPS OR DEPS STREQUAL "")
    message(STATUS
        "  CMakePresets.json needs the dependency prefix filled in. Pass -DDEPS=<vcpkg "
        "installed dir>/<triplet> to have it written, or edit CMAKE_PREFIX_PATH: the "
        "DesktopBootstrap component links glfw3 and Freetype, which are not part of the package.")
endif()
message(STATUS "  cmake --preset default && cmake --build --preset default")
