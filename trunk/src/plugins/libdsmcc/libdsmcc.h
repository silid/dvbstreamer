#ifndef LIBDSMCC_H
#define LIBDSMCC_H

#ifdef __cplusplus
extern "C"
{
#endif
#include "dsmcc-receiver.h"
#include "dsmcc-carousel.h"
    struct stream
    {
        int pid;
        unsigned int assoc_tag;
        struct stream *next, *prev;;
    };

    struct dsmcc_status
    {
        int rec_files, total_files;
        int rec_dirs,  total_dirs;
        int gzip_size, total_size;
        enum cachestate { EMPTY, LISTINGS, FILLING, FULL } state;
        enum running { NOTRUNNING, RUNNINGSOON, PAUSED, RUNNING } runstate;

        void *private;

        /* Private Information (ish) */

        struct obj_carousel carousels[MAXCAROUSELS];


        FILE *debug_fd;
    };

#ifdef __cplusplus
}
#endif
#endif
