# Optional code-hygiene targets: `format`, `format-check`, `tidy`.
#
# These operate ONLY on hand-written sources. Generated files
# (usdGenSchema output: module.cpp, moduleDeps.cpp, and any
# hairProceduralAPI/tokens/wrap* if regenerated) are intentionally
# excluded so reformatting never fights codegen.
#
# Targets are created only when the corresponding tool is found, so
# this never blocks a build on a machine without clang tooling.

set(LINT_SOURCES
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralAPIAdapter.cpp
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralAPIAdapter.h
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralDataSources.cpp
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralDataSources.h
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralDeformer.cpp
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralDeformer.h
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralSceneIndex.cpp
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralSceneIndex.h
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralSceneIndexPlugin.cpp
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralSceneIndexPlugin.h
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralSchema.cpp
    ${CMAKE_SOURCE_DIR}/usd/hairProceduralSchema.h
)

# --- clang-format: `format` (in place) and `format-check` (CI-style gate) ---
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if (CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i --style=file ${LINT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format: rewriting sources in place"
        VERBATIM)

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror --style=file ${LINT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format: checking formatting (fails on any diff)"
        VERBATIM)
else()
    message(STATUS "clang-format not found - `format`/`format-check` targets disabled")
endif()

# --- clang-tidy: `tidy` (needs compile_commands.json in the build dir) ---
find_program(RUN_CLANG_TIDY_EXE NAMES run-clang-tidy)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if (RUN_CLANG_TIDY_EXE)
    add_custom_target(tidy
        COMMAND ${RUN_CLANG_TIDY_EXE} -p ${CMAKE_BINARY_DIR} ${LINT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy: analysing sources (uses compile_commands.json)"
        VERBATIM)
elseif (CLANG_TIDY_EXE)
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY_EXE} -p ${CMAKE_BINARY_DIR} ${LINT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy: analysing sources (uses compile_commands.json)"
        VERBATIM)
else()
    message(STATUS "clang-tidy not found - `tidy` target disabled")
endif()
