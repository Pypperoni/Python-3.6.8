cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(python3.6)

find_package(OpenSSL REQUIRED)
if(NOT OPENSSL_FOUND)
    message(FATAL_ERROR "You need OpenSSL headers to build this.")
endif()

find_package(ZLIB REQUIRED)
if(NOT ZLIB_FOUND)
    message(FATAL_ERROR "You need ZLIB headers to build this.")
endif()

include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${ZLIB_INCLUDE_DIR})

if(WIN32)
    add_definitions(-DWIN32)
    add_definitions(-D_WIN32)
    add_definitions(-D_USRDLL)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "-O3")
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(OSX TRUE)
endif()

add_definitions(-DNDEBUG)
add_definitions(-DPy_BUILD_CORE)
add_definitions(-g)

set(OBJECTS_FILES
    Objects/abstract.c
    Objects/accu.c
    Objects/boolobject.c
    Objects/bytearrayobject.c
    Objects/bytes_methods.c
    Objects/bytesobject.c
    Objects/capsule.c
    Objects/cellobject.c
    Objects/classobject.c
    Objects/codeobject.c
    Objects/complexobject.c
    Objects/descrobject.c
    Objects/dictobject.c
    Objects/enumobject.c
    Objects/exceptions.c
    Objects/fileobject.c
    Objects/floatobject.c
    Objects/frameobject.c
    Objects/funcobject.c
    Objects/genobject.c
    Objects/iterobject.c
    Objects/listobject.c
    Objects/longobject.c
    Objects/memoryobject.c
    Objects/methodobject.c
    Objects/moduleobject.c
    Objects/namespaceobject.c
    Objects/object.c
    Objects/obmalloc.c
    Objects/odictobject.c
    Objects/rangeobject.c
    Objects/setobject.c
    Objects/sliceobject.c
    Objects/structseq.c
    Objects/tupleobject.c
    Objects/typeobject.c
    Objects/unicodectype.c
    Objects/unicodeobject.c
    Objects/weakrefobject.c
)

if(WIN32)
    set(PC_FILES
        PC/getpathp.c
        PC/invalid_parameter_handler.c
        PC/msvcrtmodule.c
        PC/winreg.c
        PC/winsound.c
        Modules/_winapi.c
    )
else()
    set(PC_FILES) # Empty
endif()

set(PYTHON_FILES
    Python/bltinmodule.c
    Python/ceval.c
    Python/codecs.c
    Python/dtoa.c
    Python/dup2.c
    Python/dynamic_annotations.c
    Python/dynload_stub.c
    Python/errors.c
    Python/fileutils.c
    Python/formatter_unicode.c
    Python/frozen.c
    Python/getargs.c
    Python/getcompiler.c
    Python/getcopyright.c
    Python/getopt.c
    Python/getplatform.c
    Python/getversion.c
    Python/import.c
    Python/importdl.c
    Python/marshal.c
    Python/modsupport.c
    Python/myreadline.c
    Python/mysnprintf.c
    Python/mystrtoul.c
    Python/pyctype.c
    Python/pyfpe.c
    Python/pyhash.c
    Python/pylifecycle.c
    Python/pymath.c
    Python/pystate.c
    Python/pystrcmp.c
    Python/pystrhex.c
    Python/pystrtod.c
    Python/pythonrun.c
    Python/pytime.c
    Python/random.c
    Python/strdup.c
    Python/structmember.c
    Python/sysmodule.c
    Python/thread.c
    Python/traceback.c
    Python/_warnings.c
)

set(MODULES_FILES
    Modules/config.c

    Modules/arraymodule.c
    Modules/_asynciomodule.c
    Modules/atexitmodule.c
    Modules/audioop.c
    Modules/binascii.c
    Modules/_bisectmodule.c
    Modules/_bz2module.c
    Modules/cmathmodule.c
    Modules/_codecsmodule.c
    Modules/_collectionsmodule.c
    Modules/_cryptmodule.c
    Modules/_csv.c
    Modules/_datetimemodule.c
    Modules/_elementtree.c
    Modules/errnomodule.c
    Modules/faulthandler.c
    Modules/fcntlmodule.c
    Modules/fpectlmodule.c
    Modules/fpetestmodule.c
    Modules/_functoolsmodule.c
    Modules/gcmodule.c
    Modules/_gdbmmodule.c
    Modules/getbuildinfo.c
    Modules/grpmodule.c
    Modules/_hashopenssl.c
    Modules/hashtable.c
    Modules/_heapqmodule.c
    Modules/itertoolsmodule.c
    Modules/_json.c
    Modules/_localemodule.c
    Modules/_lsprof.c
    Modules/_lzmamodule.c
    Modules/_math.c
    Modules/mathmodule.c
    Modules/md5module.c
    Modules/mmapmodule.c
    Modules/nismodule.c
    Modules/_opcode.c
    Modules/_operator.c
    Modules/ossaudiodev.c
    Modules/_pickle.c
    Modules/posixmodule.c
    Modules/_posixsubprocess.c
    Modules/pwdmodule.c
    Modules/pyexpat.c
    Modules/_randommodule.c
    Modules/readline.c
    Modules/resource.c
    Modules/rotatingtree.c
    Modules/selectmodule.c
    Modules/sha1module.c
    Modules/sha256module.c
    Modules/sha512module.c
    Modules/signalmodule.c
    Modules/socketmodule.c
    Modules/spwdmodule.c
    Modules/_sre.c
    Modules/_ssl.c
    Modules/_stat.c
    Modules/_struct.c
    Modules/syslogmodule.c
    Modules/termios.c
    Modules/_threadmodule.c
    Modules/timemodule.c
    Modules/_tracemalloc.c
    Modules/unicodedata.c
    Modules/_weakref.c
    Modules/xxlimited.c
    Modules/xxmodule.c
    Modules/xxsubtype.c
    Modules/zlibmodule.c

    Modules/_blake2/blake2module.c
    Modules/_blake2/blake2b_impl.c
    Modules/_blake2/blake2s_impl.c

    Modules/cjkcodecs/_codecs_cn.c
    Modules/cjkcodecs/_codecs_hk.c
    Modules/cjkcodecs/_codecs_iso2022.c
    Modules/cjkcodecs/_codecs_jp.c
    Modules/cjkcodecs/_codecs_kr.c
    Modules/cjkcodecs/_codecs_tw.c
    Modules/cjkcodecs/multibytecodec.c

    Modules/_io/_iomodule.c
    Modules/_io/iobase.c
    Modules/_io/fileio.c
    Modules/_io/bytesio.c
    Modules/_io/bufferedio.c
    Modules/_io/textio.c
    Modules/_io/stringio.c

    Modules/_multiprocessing/multiprocessing.c
    Modules/_multiprocessing/semaphore.c

    Modules/_sha3/sha3module.c

    Modules/_sqlite/cache.h
    Modules/_sqlite/connection.h
    Modules/_sqlite/cursor.h
    Modules/_sqlite/microprotocols.h
    Modules/_sqlite/module.h
    Modules/_sqlite/prepare_protocol.h
    Modules/_sqlite/row.h
    Modules/_sqlite/statement.h
    Modules/_sqlite/util.h
)

if(OSX)
    list(APPEND MODULES_FILES
        Modules/_scproxy.c
    )
endif()

if(NOT WIN32)
    list(APPEND MODULES_FILES
        Modules/getpath.c
    )
endif()

include_directories(Include)
include_directories(Python)
if(WIN32)
    include_directories(PC)
endif()

source_group("Objects" FILES ${OBJECTS_FILES})

if(WIN32)
    source_group("PC" FILES ${PC_FILES})
endif()

source_group("Python" FILES ${PYTHON_FILES})
source_group("Modules" FILES ${MODULES_FILES})

set(_FILES
    ${OBJECTS_FILES}
    ${PYTHON_FILES}
    ${MODULES_FILES}
)

if(WIN32)
    LIST(APPEND _FILES ${PC_FILES})
endif()

add_library(python3.6 STATIC ${_FILES})
