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

#include <sys/types.h>

#include <errno.h>
#include <ncurses.h>
#include <menu.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tool_instances.h"

#include "viewer.h"

static int
fill_menu(struct tool_instance *list, ITEM ***items, MENU **menu)
{
    int                     listlen = 0;
    struct tool_instance    *cur = list;

    while (cur)
    {
        ++listlen;
        cur = cur->next;
    }

    *items = (ITEM **)calloc(listlen+1, sizeof(**items));
    if (*items == NULL)
    {
        fprintf(stderr, "cloudmig-view: Could not allocate memory.\n");
        return EXIT_FAILURE;
    }

    cur = list;
    for(int i = 0; i < listlen && cur != NULL; ++i, cur = cur->next)
    {
        (*items)[i] = new_item(cur->desc, cur->dirpath);
        if ((*items)[i] == NULL)
        {
            fprintf(stderr, "cloudmig-view: Could not create menu item : %i.\n",
                    errno);
            return EXIT_FAILURE;
        }
    }
	(*items)[listlen] = (ITEM *)NULL;

	*menu = new_menu((ITEM **)(*items));
    if (*menu == NULL)
    {
        fprintf(stderr, "cloudmig-view: Could not create curses menu.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void
clear_menu(MENU* my_menu, ITEM** my_items)
{
    unpost_menu(my_menu);
    free_menu(my_menu);
    for (int i =0; my_items[i]; ++i)
        free_item(my_items[i]);
    free(my_items);
}

int
main_menu(void)
{
	MENU                    *my_menu = NULL;
 	ITEM                    **my_items = NULL;
    struct tool_instance    *list = NULL;
    const char              *ret_msg = NULL;
    // Init at refresh for first loop.
    int                     c = 'R';

    do
    {
        switch (c)
        {
        // Navigate in the menu
        case KEY_DOWN:
        case KEY_RIGHT:
            menu_driver(my_menu, REQ_DOWN_ITEM);
            break ;
        case KEY_UP:
        case KEY_LEFT:
            menu_driver(my_menu, REQ_UP_ITEM);
            break ;
        // Select a migration to monitor
        case ' ':
        case 13: // Enter key
            {
                if (item_count(my_menu) > 0)
                {
                    ITEM *curitem = current_item(my_menu);
                    if (view_instance(item_description(curitem))
                        != EXIT_SUCCESS)
                    {
                        ret_msg = "An error occured while trying to display the tool's data.";
                    }
                }
            }
            // No break here to allow refreshing the list
            // when coming out of the instance display.
        // Refresh the list.
        case 'r':
        case 'R':
            if (list)
                clear_instance_list(list);
            list = get_instance_list();
            if (list == NULL)
                mvprintw(0, 0,
                "cloudmig-view: No cloudmig tool is running at the moment.\n");

            if (my_menu)
                clear_menu(my_menu, my_items);
            my_items = NULL;
            my_menu = NULL;
            
            if (fill_menu(list, &my_items, &my_menu))
                return EXIT_FAILURE;
            post_menu(my_menu);
            break;
        // Leave the monitor
        case 'q':
        case 'Q':
            break ;
        // Ignore the others
        default:
            break ;
        }
        mvprintw(LINES - 4, 0,
             "Use <SPACE> or <ENTER> to select the process to monitor.");
        mvprintw(LINES - 3, 0,
                 "    <q> or <Q> to quit this program.");
        mvprintw(LINES - 2, 0,
                 "    <r> or <R> to see refresh the list manually.");
        if (ret_msg)
        {
            mvprintw(LINES -6, 0, "%.*s", COLS, ret_msg);
            ret_msg = NULL;
        }
        refresh();
    } while ((c = getch()) != 'q');

    return EXIT_FAILURE;
}
