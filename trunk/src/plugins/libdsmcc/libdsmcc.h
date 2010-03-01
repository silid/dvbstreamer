#ifndef LIBDSMCC_H
#define LIBDSMCC_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#include "dsmcc-receiver.h"
#include "dsmcc-carousel.h"
    struct stream_request
    {
        uint32_t carouselId;
        uint16_t assoc_tag;
        struct stream_request *next;
    };

    struct dsmcc_status
    {
        int rec_files, total_files;
        int rec_dirs,  total_dirs;
        int gzip_size, total_size;
        enum cachestate { EMPTY, LISTINGS, FILLING, FULL } state;
        enum running { NOTRUNNING, RUNNINGSOON, PAUSED, RUNNING } runstate;

        /* must check to see if any new streams to subscribe to after calling
           receive each time (new stream info comes from within dsmcc */

        struct stream_request *newstreams;

        /* Private Information (ish) */

        struct obj_carousel carousels[MAXCAROUSELS];


        FILE *debug_fd;
    };
extern char LIBDSMCC[];
#ifdef __cplusplus
}
#endif
#endif
