// Copyright (c) 2011, David Pineau
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER AND CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#include <curses.h>
#include <stdio.h>
#include <stdlib.h>


// Those a global data for restorating the colors and pairs
// to the original values
static short colors[3][3] = {
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0}
};
static short pairs[3][2] = {
    {0, 0},
    {0, 0},
    {0, 0}
};

static void
viewer_save_original_colors(void)
{
    color_content(COLORS-1, &colors[0][0], &colors[0][1], &colors[0][2]);
    color_content(COLORS-2, &colors[1][0], &colors[1][1], &colors[1][2]);
    color_content(COLORS-3, &colors[2][0], &colors[2][1], &colors[2][2]);

    pair_content(1, &pairs[0][0], &pairs[0][1]);
    pair_content(2, &pairs[1][0], &pairs[1][1]);
    pair_content(3, &pairs[2][0], &pairs[2][1]);
}

/*
 * This function inits the colors used for the progress bars.
 *  * darkyellow for the background of the progress bar.
 *  * lightred for the background of error messages.
 *  * lightgreen for the background of confirmation messages.
 */
int
viewer_init_colors(void)
{
    // The last color, white is at idx 7 by default
    short       darkyellow_idx  = COLORS-1,
                lightred_idx    = COLORS-2,
                lightgreen_idx  = COLORS-3,
                progressbar_idx = 1,
                errormsg_idx    = 2,
                confirmmsg_idx  = 3;

    viewer_save_original_colors();

    if (init_color(darkyellow_idx, 1000, 600, 000) == ERR)
    {
        fprintf(stderr,
            "cloudmig-view: Falling back to YELLOW instead of darkyellow.\n");
        darkyellow_idx = COLOR_YELLOW;
    }
    if (init_color(lightred_idx, 900, 200, 200) == ERR)
    {
        fprintf(stderr,
            "cloudmig-view: Falling back to YELLOW instead of darkyellow.\n");
        lightred_idx = COLOR_RED;
    }
    if (init_color(lightgreen_idx, 0, 1000, 0) == ERR)
    {
        fprintf(stderr,
            "cloudmig-view: Falling back to YELLOW instead of darkyellow.\n");
        lightgreen_idx = COLOR_GREEN;
    }

    if (init_pair(progressbar_idx, COLOR_BLACK, darkyellow_idx) == ERR
        || init_pair(errormsg_idx, COLOR_BLACK, lightred_idx) == ERR
        || init_pair(confirmmsg_idx, COLOR_BLACK, lightgreen_idx) == ERR)
    {
        fprintf(stderr, "cloudmig-view: Could not initialize the colors.\n");
        return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
}

/*
 * This function restores the terminal's original colors.
 */
void
viewer_restore_colors(void)
{
    init_color(COLORS-1, colors[0][0], colors[0][1], colors[0][2]);
    init_color(COLORS-2, colors[1][0], colors[1][1], colors[1][2]);
    init_color(COLORS-3, colors[2][0], colors[2][1], colors[2][2]);

    init_pair(1, pairs[0][0], pairs[0][1]);
    init_pair(2, pairs[1][0], pairs[1][1]);
    init_pair(3, pairs[2][0], pairs[2][1]);
}
