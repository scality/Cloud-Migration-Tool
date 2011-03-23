#include <ncurses.h>
#include <stdio.h>
#include <unistd.h>

int main()
{
    int i = 0;
    WINDOW *sp = initscr();

    while (i < 100)
    {
        usleep(40000);
        ++i;
        mvprintw(2, 0, "Loaded : %-3i%%\n", i);
        mvprintw(0, 1, "XMAX =%i", sp->_maxx);
        mvprintw(1, 1, "YMAX=%i", sp->_maxy);
        refresh();
    }
    endwin();
    return (0);
}

