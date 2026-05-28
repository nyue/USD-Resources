set(SCHEMA_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/usd)
file(MAKE_DIRECTORY ${SCHEMA_OUTPUT_DIR})

# Invoke usdGenSchema at configure time to generate C++ schema boilerplate.
# For Houdini builds we call hython directly (bypassing the .cmd/.sh wrapper)
# to avoid any dependency on the Houdini shell environment (PATH, HFS, etc.).
# CMake sets HFS in the child process environment so hython initialises correctly.
if (BUILD_HOUDINI_PLUGIN AND DEFINED HOUDINI_INSTALL_ROOT)
    find_program(HYTHON_EXECUTABLE
        NAMES hython hython3.11 hython.exe
        HINTS "${HOUDINI_INSTALL_ROOT}/bin"
        NO_DEFAULT_PATH
        REQUIRED)
    set(_usdgenschema_invoke
        "${HYTHON_EXECUTABLE}"
        "${HOUDINI_INSTALL_ROOT}/bin/usdGenSchema")
else()
    set(_usdgenschema_hints)
    if (BUILD_USD_PLUGIN AND DEFINED USD_INSTALL_ROOT)
        list(APPEND _usdgenschema_hints "${USD_INSTALL_ROOT}/bin")
    endif()
    find_program(USDGENSCHEMA_EXECUTABLE
        NAMES usdGenSchema
        HINTS ${_usdgenschema_hints}
        REQUIRED)
    set(_usdgenschema_invoke "${USDGENSCHEMA_EXECUTABLE}")
endif()

execute_process(
    COMMAND ${_usdgenschema_invoke}
            "${CMAKE_CURRENT_SOURCE_DIR}/schema.usda"
            "${SCHEMA_OUTPUT_DIR}"
    RESULT_VARIABLE _gen_result
)
if(NOT _gen_result EQUAL 0)
    message(FATAL_ERROR "usdGenSchema failed: ${_gen_result}")
endif()

# Place generated resource files where create_target() can find them
file(COPY ${SCHEMA_OUTPUT_DIR}/plugInfo.json
          ${SCHEMA_OUTPUT_DIR}/generatedSchema.usda
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

file(GLOB SCHEMA_CPP_SOURCES CONFIGURE_DEPENDS
    ${SCHEMA_OUTPUT_DIR}/hairProcedural*.cpp
    ${SCHEMA_OUTPUT_DIR}/tokens.cpp)
file(GLOB SCHEMA_WRAP_SOURCES CONFIGURE_DEPENDS
    ${SCHEMA_OUTPUT_DIR}/wrap*.cpp)
file(GLOB SCHEMA_HEADERS CONFIGURE_DEPENDS
    ${SCHEMA_OUTPUT_DIR}/*.h)

file(GLOB sources "*.cpp")
file(GLOB headers "*.h")
list(FILTER sources EXCLUDE REGEX "./module*|./wrap*")
file(GLOB py_sources "[module|wrap]*.cpp")

find_package(peasyocl REQUIRED)
find_package(OpenGL REQUIRED)

if (${BUILD_USD_PLUGIN})
find_package(Python3 REQUIRED)
    set(CMAKE_FIND_PACKAGE_REDIRECTS_DIR ${USD_INSTALL_ROOT})
    find_package(pxr REQUIRED)

    create_target(
            ${USDPLUGIN_NAME}
        PYPACKAGE_NAME
            ${PYPACKAGE_NAME}
        HEADERS
            ${headers}
            ${SCHEMA_HEADERS}
        SOURCES
            ${sources}
            ${SCHEMA_CPP_SOURCES}
        PY_SOURCES
            ${py_sources}
            ${SCHEMA_WRAP_SOURCES}
        INCLUDE_DIRS
            ${PYTHON_INCLUDE_PATH}
            ${PXR_INCLUDE_DIRS}
            ${peasyocl_INCLUDE_DIR}
            ${SCHEMA_OUTPUT_DIR}
        LIBS
            ${PXR_LIBRARIES}
            peasyocl
    )
endif()


if (${BUILD_HOUDINI_PLUGIN})
    # HOUDINI LIBRARY

    # RPATH is a Linux/macOS concept; skip on Windows.
    if (NOT WIN32)
        set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
        set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    endif()

    set(HPLUGIN_NAME ${USDPLUGIN_NAME}Houdini)

    list(APPEND CMAKE_PREFIX_PATH "${HOUDINI_INSTALL_ROOT}/toolkit/cmake")

    find_package(Houdini REQUIRED)

    set(PYTHON_LIB python3.11)
    set(PYTHON_LIB_NUMBER python311)
    set(H_INCLUDE_DIR "${HOUDINI_INSTALL_ROOT}/toolkit/include")

    if (APPLE)
        set(H_LIB_DIR "${HOUDINI_INSTALL_ROOT}/../Libraries")
        set(PYTHON_LIB_DIR "${HOUDINI_INSTALL_ROOT}/Frameworks/Python.framework/Versions/3.11")
        set(PYTHON_FRAMEWORK "${HOUDINI_INSTALL_ROOT}/Frameworks/Python.framework")
    elseif (WIN32)
        set(H_LIB_DIR "${HOUDINI_INSTALL_ROOT}/custom/houdini/dsolib")
        set(PYTHON_LIB_DIR "${HOUDINI_INSTALL_ROOT}/python311/libs")
        set(PYTHON_FRAMEWORK "")
    else()
        set(H_LIB_DIR "${HOUDINI_INSTALL_ROOT}/dsolib")
        set(PYTHON_LIB_DIR "${HOUDINI_INSTALL_ROOT}/python/lib")
        set(PYTHON_FRAMEWORK "")
    endif()

    set(PYTHON_INCLUDE_PATH "${H_INCLUDE_DIR}/${PYTHON_LIB}")
    set(PXR_INCLUDE_DIR ${H_INCLUDE_DIR})

    set(BOOST_NAMESPACE hboost)
    set(BOOST_PYTHON_LIB ${BOOST_NAMESPACE}_${PYTHON_LIB_NUMBER})
    set(BOOST_INCLUDE_DIR ${H_INCLUDE_DIR}/${BOOST_NAMESPACE})

    link_directories(${H_LIB_DIR})
    link_directories(${PYTHON_LIB_DIR})

    create_target(
        ${HPLUGIN_NAME}
        BUILD_HOUDINI
        PYPACKAGE_NAME
            ${PYPACKAGE_NAME}
        HEADERS
            ${headers}
            ${SCHEMA_HEADERS}
        SOURCES
            ${sources}
            ${SCHEMA_CPP_SOURCES}
        PY_SOURCES
            ${py_sources}
            ${SCHEMA_WRAP_SOURCES}
        INCLUDE_DIRS
            ${PYTHON_INCLUDE_PATH}
            ${PXR_INCLUDE_DIR}
            ${peasyocl_INCLUDE_DIR}
            ${SCHEMA_OUTPUT_DIR}
        LIBS
            Houdini
            # Windows: pxr/boost/etc. are in HoudiniThirdParty, not Houdini.
            # On Linux they load transitively from the Houdini DSOs at runtime.
            $<$<PLATFORM_ID:Windows>:HoudiniThirdParty>
            ${PYTHON_FRAMEWORK}
            peasyocl
        )

    # CMake auto-generates hairProcHoudini_EXPORTS but api.h checks HAIRPROC_EXPORTS.
    # Without this the symbols get dllimport instead of dllexport on Windows.
    if (WIN32)
        target_compile_definitions(${HPLUGIN_NAME} PRIVATE HAIRPROC_EXPORTS)
    endif()

    endif()
