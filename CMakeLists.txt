## Copyright (c) 2011, David Pineau
## All rights reserved.

## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are met:
##  * Redistributions of source code must retain the above copyright
##    notice, this list of conditions and the following disclaimer.
##  * Redistributions in binary form must reproduce the above copyright
##    notice, this list of conditions and the following disclaimer in the
##    documentation and/or other materials provided with the distribution.
##  * Neither the name of the copyright holder nor the names of its contributors
##    may be used to endorse or promote products derived from this software
##    without specific prior written permission.

## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
## AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
## IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
## ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER AND CONTRIBUTORS BE
## LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
## CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
## SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
## INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
## CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
## ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
## POSSIBILITY OF SUCH DAMAGE.


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
FIND_PACKAGE(LibXml2 REQUIRED)

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
ADD_DEFINITIONS(-W -Wall -Werror -std=c99)
ADD_DEFINITIONS(-D_GNU_SOURCE)              # activate code in C99 (strdup...)

CONFIGURE_FILE(${PROJECT_SOURCE_DIR}/inc/cloudmig.h.in
               ${PROJECT_BINARY_DIR}/inc/cloudmig.h
)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/inc
                    ${PROJECT_BINARY_DIR}/inc
                    ${DROPLET_INCLUDE_DIR}
                    ${LIBXML2_INCLUDE_DIR}
)


SET(CLOUDMIG_SRC    src/main.c
                    src/options.c
                    src/log.c
                    src/load_profiles.c
                    src/transfer.c
                    src/status.c
)

ADD_EXECUTABLE(cloudmig ${CLOUDMIG_SRC})
TARGET_LINK_LIBRARIES(cloudmig ${DROPLET_LIBRARY})
