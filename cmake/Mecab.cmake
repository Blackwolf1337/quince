# MeCab (BSD option of its GPL/LGPL/BSD license) with the IPADIC dictionary:
# readings of Japanese words for romanized lyrics. Built as a static library
# from the release sources of the maintained fork, without its autotools;
# IPADIC is compiled to a UTF-8 binary dictionary at build time and goes to
# mecab/ipadic next to the binary (share/quince/mecab/ipadic when installed,
# Contents/Resources in the macOS bundle).

FetchContent_Declare(mecab
    URL https://github.com/shogo82148/mecab/releases/download/v0.996.13/mecab-0.996.13.tar.gz
    URL_HASH SHA256=3da28541062d6d1cfbb92ab09481779f2ad735c89705d981eb66f072ef776538
)
FetchContent_Declare(mecab_ipadic
    URL https://github.com/shogo82148/mecab/releases/download/v0.996.13/mecab-ipadic-2.7.0-20070801.tar.gz
    URL_HASH SHA256=74130f44264ce5b8cfa51e498b99345f71d2f854b74a9e9dfb6489e13e479e67
)
# sources only: neither has a CMakeLists.txt
FetchContent_MakeAvailable(mecab mecab_ipadic)

find_package(Iconv REQUIRED)
find_package(Threads REQUIRED)
include(CheckIncludeFileCXX)
include(CheckTypeSize)

set(MECAB_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/mecab-config)
set(MECAB_HEADERS ctype.h dirent.h fcntl.h inttypes.h pthread.h stdint.h stdlib.h string.h sys/mman.h
                  sys/stat.h sys/types.h unistd.h windows.h)
string(CONCAT MECAB_CONFIG "#define PACKAGE \"mecab\"\n#define VERSION \"0.996.13\"\n#define HAVE_CXX11_ATOMIC_OPS 1\n"
                           "#define HAVE_CXX11_TLS_KEYWORD 1\n#define HAVE_GETENV 1\n#define HAVE_ICONV 1\n#define ICONV_CONST\n")
foreach(header IN LISTS MECAB_HEADERS)
    string(MAKE_C_IDENTIFIER "HAVE_${header}" var)
    string(TOUPPER ${var} var)
    check_include_file_cxx(${header} ${var})
    if(${var})
        string(APPEND MECAB_CONFIG "#define ${var} 1\n")
    endif()
endforeach()
if(HAVE_SYS_MMAN_H)
    string(APPEND MECAB_CONFIG "#define HAVE_MMAP 1\n")
endif()
foreach(type char short int long "long long" size_t)
    string(MAKE_C_IDENTIFIER "SIZEOF_${type}" var)
    string(TOUPPER ${var} var)
    check_type_size("${type}" ${var} LANGUAGE CXX)
    string(APPEND MECAB_CONFIG "#define ${var} ${${var}}\n")
endforeach()
file(CONFIGURE OUTPUT ${MECAB_CONFIG_DIR}/config.h CONTENT "${MECAB_CONFIG}")

set(MECAB_SRC ${mecab_SOURCE_DIR}/src)
add_library(mecab STATIC
    ${MECAB_SRC}/char_property.cpp ${MECAB_SRC}/connector.cpp ${MECAB_SRC}/context_id.cpp
    ${MECAB_SRC}/dictionary.cpp ${MECAB_SRC}/dictionary_compiler.cpp ${MECAB_SRC}/dictionary_generator.cpp
    ${MECAB_SRC}/dictionary_rewriter.cpp ${MECAB_SRC}/eval.cpp ${MECAB_SRC}/feature_index.cpp
    ${MECAB_SRC}/iconv_utils.cpp ${MECAB_SRC}/lbfgs.cpp ${MECAB_SRC}/learner.cpp ${MECAB_SRC}/learner_tagger.cpp
    ${MECAB_SRC}/libmecab.cpp ${MECAB_SRC}/nbest_generator.cpp ${MECAB_SRC}/param.cpp ${MECAB_SRC}/shlex.cpp
    ${MECAB_SRC}/string_buffer.cpp ${MECAB_SRC}/tagger.cpp ${MECAB_SRC}/tokenizer.cpp ${MECAB_SRC}/utils.cpp
    ${MECAB_SRC}/viterbi.cpp ${MECAB_SRC}/writer.cpp
)
# old code: its own warnings are not ours to fix
target_compile_options(mecab PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
# its error messages carry __FILE__: keep the build machine's paths out of the binary
if(NOT MSVC)
    target_compile_options(mecab PRIVATE -ffile-prefix-map=${mecab_SOURCE_DIR}/=mecab/)
endif()
target_compile_definitions(mecab PRIVATE HAVE_CONFIG_H DIC_VERSION=102 MECAB_DEFAULT_RC="mecabrc")
target_include_directories(mecab PRIVATE ${MECAB_CONFIG_DIR} PUBLIC ${MECAB_SRC})
target_link_libraries(mecab PUBLIC Iconv::Iconv Threads::Threads)
set_target_properties(mecab PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(mecab-dict-index ${MECAB_SRC}/mecab-dict-index.cpp)
target_compile_options(mecab-dict-index PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
target_compile_definitions(mecab-dict-index PRIVATE HAVE_CONFIG_H)
target_include_directories(mecab-dict-index PRIVATE ${MECAB_CONFIG_DIR})
target_link_libraries(mecab-dict-index PRIVATE mecab)

# the binary dictionary (about 50 MB), plus its dicrc and an empty mecabrc
# that the tagger is pointed at (MeCab insists on reading one)
set(MECAB_DIC_DIR ${CMAKE_BINARY_DIR}/mecab/ipadic)
add_custom_command(
    OUTPUT ${MECAB_DIC_DIR}/sys.dic ${MECAB_DIC_DIR}/unk.dic ${MECAB_DIC_DIR}/matrix.bin
           ${MECAB_DIC_DIR}/char.bin ${MECAB_DIC_DIR}/dicrc ${MECAB_DIC_DIR}/mecabrc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MECAB_DIC_DIR}
    COMMAND mecab-dict-index -d ${mecab_ipadic_SOURCE_DIR} -o ${MECAB_DIC_DIR} -f EUC-JP -t UTF-8
    COMMAND ${CMAKE_COMMAND} -E copy ${mecab_ipadic_SOURCE_DIR}/dicrc ${MECAB_DIC_DIR}/dicrc
    COMMAND ${CMAKE_COMMAND} -E touch ${MECAB_DIC_DIR}/mecabrc
    DEPENDS mecab-dict-index
    COMMENT "Compiling the IPADIC dictionary for MeCab"
    VERBATIM
)
add_custom_target(mecab-ipadic DEPENDS ${MECAB_DIC_DIR}/sys.dic)

# the licenses under the names they are installed as
configure_file(${mecab_SOURCE_DIR}/BSD ${CMAKE_BINARY_DIR}/licenses/MECAB-BSD COPYONLY)
configure_file(${mecab_ipadic_SOURCE_DIR}/COPYING ${CMAKE_BINARY_DIR}/licenses/IPADIC-COPYING COPYONLY)
set(MECAB_LICENSE_FILES ${CMAKE_BINARY_DIR}/licenses/MECAB-BSD ${CMAKE_BINARY_DIR}/licenses/IPADIC-COPYING)

# on macOS the bundle carries them (see CMakeLists.txt)
if(NOT APPLE)
    install(DIRECTORY ${MECAB_DIC_DIR} DESTINATION ${QUINCE_DATA_DESTINATION}/mecab)
    install(FILES ${MECAB_LICENSE_FILES} DESTINATION ${QUINCE_LICENSE_DESTINATION})
endif()
