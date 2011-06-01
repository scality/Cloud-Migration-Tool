## Copyright (c) 2011, David Pineau
## All rights reserved.
##
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
##
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

IF (WIN)
    MESSAGE(ERROR "This tool is not supported for windows.")
ENDIF (WIN)

###############################################################################
#   Define some projet-wide values...
###############################################################################
SET(CLOUDMIG_VERSION_MAJOR 0)
SET(CLOUDMIG_VERSION_MINOR 9)
SET(CLOUDMIG_VERSION_PATCH 1)

SET(CPACK_PACKAGE_VERSION_MAJOR ${CLOUDMIG_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${CLOUDMIG_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${CLOUDMIG_VERSION_PATCH})
SET(CLOUDMIG_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

# Change the module path in order to include own's
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CLOUDMIG_SOURCE_DIR}/CMakeScripts)
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)


###############################################################################
#   Additional definition of values and Installer setup with CPACK
###############################################################################
# First, check if the user wants a specific type of package.
IF (CLDMIG_PKG_TYPE)
    SET(CPACK_GENERATOR ${CLDMIG_PKG_TYPE})
ELSE (CLDMIG_PKG_TYPE)
    SET(CPACK_GENERATOR "DEB")
ENDIF (CLDMIG_PKG_TYPE)

# Next, check an user-defined installation prefix.
IF (CLDMIG_INSTALL_PREFIX)
    SET(CMAKE_INSTALL_PREFIX ${CLDMIG_INSTALL_PREFIX})
    SET(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
ENDIF (CLDMIG_INSTALL_PREFIX)

# And finally, every global CPACK variable :
SET(CPACK_PACKAGE_NAME "cloudmig")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "cloudmig is a tool for migrating data between cloud storage accounts and cloudmig-view is it's status viewer.")
#SET(CPACK_PACKAGE_INSTALL_DIRECTORY "cloudmig")

# set the maintener's contact information.
SET(CPACK_PACKAGE_CONTACT "David Pineau <dav.pineau@gmail.com>")


###############################################################################
#   Add a bit of verbose for the cmake build
###############################################################################
MESSAGE(STATUS "Cmake Version : ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
MESSAGE(STATUS "Cloudmig Version : ${CLOUDMIG_VERSION}")
MESSAGE(STATUS "Build Type : ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "System : ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
MESSAGE(STATUS "Source Directory : ${CLOUDMIG_SOURCE_DIR}")
MESSAGE(STATUS "Install Prefix : ${CMAKE_INSTALL_PREFIX}")
MESSAGE(STATUS "Package Type : ${CPACK_GENERATOR}")
MESSAGE(STATUS "Package maintainer : ${CPACK_PACKAGE_CONTACT}")


###############################################################################
# Manage dependencies
###############################################################################
# Find the (own) Droplet package in order to build the project.
FIND_PACKAGE(Droplet REQUIRED)
FIND_PACKAGE(LibXml2 REQUIRED)
FIND_PACKAGE(Curses)            #libcurses / libncurses for the viewer tool
FIND_PACKAGE(Menu)              #libmenu, for the viewer tool


#
# Generate the documentation (manpages mainly)
#
ADD_SUBDIRECTORY(doc)

#
# Compile the code
#
ADD_DEFINITIONS(-W -Wall -Werror -std=c99)
# The _GNU_SOURCE define has multiple uses : strdup in c99, asprintf, ...
ADD_DEFINITIONS(-D_GNU_SOURCE)

CONFIGURE_FILE(${PROJECT_SOURCE_DIR}/inc/cloudmig.h.in
               ${PROJECT_BINARY_DIR}/inc/cloudmig.h
)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/inc
                    ${PROJECT_BINARY_DIR}/inc
)

ADD_SUBDIRECTORY(src)

INCLUDE(CPack)
