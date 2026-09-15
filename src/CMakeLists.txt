find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNL REQUIRED libnl-3.0)
pkg_check_modules(LIBNL_GENL REQUIRED libnl-genl-3.0)

add_executable(nlnfc-init nlnfc-init.c npc300-config.c npc300-config.h)

include_directories(${LIBNL_INCLUDE_DIRS} ${LIBNL_GENL_INCLUDE_DIRS})

SET(CMAKE_C_FLAGS_DEBUG "-g -O0")

target_link_libraries(nlnfc-init ${LIBNL_LIBRARIES} ${LIBNL_GENL_LIBRARIES})
target_link_directories(nlnfc-init PRIVATE ${LIBNL_LIBRARY_DIRS} ${LIBNL_GENL_LIBRARY_DIRS})
target_compile_options(nlnfc-init PRIVATE ${LIBNL_CFLAGS_OTHER} ${LIBNL_GENL_CFLAGS_OTHER})

install(TARGETS nlnfc-init RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
