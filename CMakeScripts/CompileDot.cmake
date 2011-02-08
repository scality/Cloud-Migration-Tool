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

MACRO (COMPILE_DOT DotFile)
    GET_FILENAME_COMPONENT(DotFileNoExt ${DotFile} NAME_WE)
    GET_FILENAME_COMPONENT(DotFileName ${DotFile} NAME)
    GET_FILENAME_COMPONENT(DotFilePath ${DotFile} PATH)
    ADD_CUSTOM_TARGET("${DotFileNoExt}.png")
    ADD_CUSTOM_COMMAND(SOURCE ${DotFileNoExt}.png
        COMMAND sh
        ARGS -c '${DOT_EXECUTABLE} -Tpng -o ${CLOUDMIG_BINARY_DIR}/doc/${DotFileNoExt}.png ${DotFile}'
        TARGET ${DotFileNoExt}.png
    )
    # This allows reuniting each DotFile compilation under the
    # "dots" target.
    IF (NOT DOTS_COMMAND)
        SET (DOTS_COMMAND TRUE)
        # Creates a make rule allowing to make all dot files
        ADD_CUSTOM_TARGET(dots)
        # Creates the doc/ directory in the build tree
        FILE(MAKE_DIRECTORY ${CLOUDMIG_BINARY_DIR}/doc/)
    ENDIF (NOT DOTS_COMMAND)
    # makes the general dots rule depend on the newly generated file
    ADD_DEPENDENCIES(dots "${DotFileNoExt}.png")
ENDMACRO (COMPILE_DOT DotFile)
