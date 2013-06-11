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

IF (NOT DROPLET_FOUND)
    # Based on the current install rules of the droplet library,
    # the includes have a "droplet" prefix and no prefix is used for
    # the said library.

    # First, we need to find the path to droplet includes
    # the "-1.0" part is the version part (kind of bugging for the search),
    # adding the user-defined variable DROPLET_PATH may help...
    FIND_PATH(DROPLET_INCLUDE_DIR
              PATHS ${DROPLET_PATH}/include
              NAMES droplet.h
              PATH_SUFFIXES droplet droplet-1.0 droplet-2.0 droplet-3.0
    )

    # If it was found, we should be able to find the library too...
    # Same here, the user-defined path may help
    # We need to find a dynlib because otherwise the dependencies will
    # not be linked (Xml, SSL), and will make the linking fail
    FIND_LIBRARY(DROPLET_LIBRARY droplet
                 # droplet stores its dynlib in .libs in its source dir
                 PATHS ${DROPLET_PATH}/.libs ${DROPLET_PATH}/lib
    )

    IF (DROPLET_INCLUDE_DIR AND DROPLET_LIBRARY)
        SET (DROPLET_FOUND TRUE)
        MESSAGE(STATUS "Droplet include path found : ${DROPLET_INCLUDE_DIR}")
        MESSAGE(STATUS "Droplet library found : ${DROPLET_LIBRARY}")
    ENDIF (DROPLET_INCLUDE_DIR AND DROPLET_LIBRARY)


ENDIF (NOT DROPLET_FOUND)
