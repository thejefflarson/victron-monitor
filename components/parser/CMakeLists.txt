list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  include(FetchContent)
  FetchContent_Declare(
    Nanopb
    GIT_REPOSITORY "https://github.com/nanopb/nanopb.git"
    GIT_TAG 0.4.3
  )
  FetchContent_MakeAvailable(Nanopb)
  set(NANOPB_SRC_ROOT_FOLDER ${nanopb_SOURCE_DIR})
  message(WARNING, ${nanopb_SOURCE_DIR})
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${NANOPB_SRC_ROOT_FOLDER}/extra)
  find_package(Nanopb REQUIRED)
  include_directories(${NANOPB_INCLUDE_DIRS})
  nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS battery.proto)
  include_directories(${CMAKE_CURRENT_BINARY_DIR})
  set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS}
    PROPERTIES GENERATED TRUE)
endif()

idf_component_register(
  SRCS
  ${PROTO_SRCS}
  ${PROTO_HDRS}
  INCLUDE_DIRS "."
)

find_package(RE2C REQUIRED)
RE2C_TARGET(
  NAME
  parser-re2c
  INPUT
  ${COMPONENT_DIR}/parser.c
  OUTPUT
  parser-gen.c
  DEPENDS
  ${COMPONENT_DIR}/parser.h
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  add_custom_target(parser-gen DEPENDS parser-gen.c)
  add_dependencies(${COMPONENT_LIB} parser-gen)
  set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY
    ADDITIONAL_MAKE_CLEAN_FILES parser-gen.c)
  set_source_files_properties(parser-gen.c PROPERTIES GENERATED TRUE)
endif()
