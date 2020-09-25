list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
find_package(RE2C REQUIRED)
RE2C_TARGET(
  NAME
  parser
  INPUT
  parser.c
  OUTPUT
  parser-gen.c
  DEPENDS
  parser.h)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  add_custom_target(parser-gen DEPENDS parser.c)
  add_dependencies(${COMPONENT_LIB} parser-gen)
  set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY
    ADDITIONAL_MAKE_CLEAN_FILES parser-gen.c)
  set_source_files_properties(parser-gen.c PROPERTIES GENERATED TRUE)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  include(FetchContent)
  FetchContent_Declare(
    Nanopb
    GIT_REPOSITORY "https://github.com/nanopb/nanopb.git"
    GIT_TAG 0.4.2
  )
  FetchContent_MakeAvailable(Nanopb)
  set(NANOPB_SRC_ROOT_FOLDER ${nanopb_SOURCE_DIR})

  find_package(Nanopb REQUIRED)
  nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_LIST_DIR}/battery.proto)
  include_directories(${NANOPB_INCLUDE_DIRS})

  set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS}
    PROPERTIES GENERATED TRUE)
endif()

idf_component_register(
  SRCS
  parser.h
  parser-gen.c
  ${PROTO_SRCS}
  ${PROTO_HDRS}
  INCLUDE_DIRS .
)
