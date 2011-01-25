CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(CLOUDMIG C)

SET(CLOUDMIG_VERSION_MAJOR 0)
SET(CLOUDMIG_VERSION_MINOR 1)

# Change the module path in order to include own's
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CLOUDMIG_SOURCE_DIR}/CMakeScripts)

# Find the (own) Dot package in order to build png files from dot files (doc).
FIND_PACKAGE(Dot)

# Find the (own) Droplet package in order to build the project.
FIND_PACKAGE(Droplet REQUIRED)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

#
# Generate the documentation
#
IF (DOT_EXECUTABLE_FOUND)
    # Adds a macro COMPILE_DOT allowing to build png from dot files
    INCLUDE (CMakeScripts/CompileDot.cmake)
    ADD_SUBDIRECTORY(doc/)
ENDIF (DOT_EXECUTABLE_FOUND)

#
# Compile the code
#
ADD_DEFINITIONS(-W -Wall -Werror)
CONFIGURE_FILE(${PROJECT_SOURCE_DIR}/inc/cloudmig.h.in
               ${PROJECT_BINARY_DIR}/inc/cloudmig.h
)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/inc
                    ${PROJECT_BINARY_DIR}/inc
                    ${DROPLET_INCLUDE_DIR}
)


SET(CLOUDMIG_SRC    src/main.c
                    src/options.c
)

ADD_EXECUTABLE(cloudmig ${CLOUDMIG_SRC})
TARGET_LINK_LIBRARIES(cloudmig ${DROPLET_LIBRARY})
