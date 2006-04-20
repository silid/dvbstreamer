/* 
Copyright (C) 2006  Adam Charrett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

logging.h

Logging functions.

*/
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "logging.h"

int verbosity = 0;

/*
 * Print out a log message to stderr depending on verbosity
 */
void printlog(int level, char *format, ...)
{
    if (level <= verbosity)
    {
        va_list valist;
        char *logline;
        va_start(valist, format);
        vasprintf(&logline, format, valist);
        fprintf(stderr, logline);
        va_end(valist);
        free(logline);
    }
}
