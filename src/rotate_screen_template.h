#include <unistd.h>
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "logging.h"

#define CONCAT_GLUE3(a, b, c) a ## b ## c
#define CONCAT3(a, b, c)     CONCAT_GLUE3(a, b, c)
#define DATA_TYPE CONCAT3(uint, BPP_TYPE, _t)

#define CONCAT_GLUE2(a, b)     a ## b
#define CONCAT(a, b)   CONCAT_GLUE2(a, b)
#define FUNC_NAME CONCAT(RotateScreenBPP, BPP_TYPE)

extern unsigned short int *fbmmap;
extern unsigned short int *vncbuf;
extern unsigned short int *fbbuf;

extern int vnc_rotate;
extern rfbScreenInfoPtr server;
extern size_t bytespp;
extern unsigned int bits_per_pixel;
extern unsigned int frame_size;
extern unsigned int fb_xres_line;
extern unsigned int fb_xres;
extern unsigned int fb_yres;

static void FUNC_NAME(){
    DATA_TYPE *f = (DATA_TYPE *)fbmmap; /* -> framebuffer         */
    DATA_TYPE *c = (DATA_TYPE *)fbbuf;  /* -> compare framebuffer */
    DATA_TYPE *r = (DATA_TYPE *)vncbuf; /* -> remote framebuffer  */

    switch (vnc_rotate)
    {
    case 0:
    case 180:
        server->width = fb_xres;
        server->height = fb_yres;
        server->paddedWidthInBytes = fb_xres * bytespp;
        break;

    case 90:
    case 270:
        server->width = fb_yres;
        server->height = fb_xres;
        server->paddedWidthInBytes = fb_yres * bytespp;
        break;
    }

    if (memcmp(fbmmap, fbbuf, frame_size) != 0)
    {
        int fb_offset = 0;
        int y;
        for (y = 0; y < (int)fb_yres; y++)
        {
            f = (DATA_TYPE *)fbmmap + fb_offset;
            c = (DATA_TYPE *)fbbuf  + fb_offset;
            /* Compare every pixels at a time */
            int x;
            for (x = 0; x < (int)fb_xres; x++)
            {
                DATA_TYPE pixel = *f;

                if (pixel != *c)
                {
                    int x2, y2;

                    *c = pixel;
                    switch (vnc_rotate)
                    {
                    case 0:
                        x2 = x;
                        y2 = y;
                        break;

                    case 90:
                        x2 = fb_yres - 1 - y;
                        y2 = x;
                        break;

                    case 180:
                        x2 = fb_xres - 1 - x;
                        y2 = fb_yres - 1 - y;
                        break;

                    case 270:
                        x2 = y;
                        y2 = fb_xres - 1 - x;
                        break;
                    default:
                        error_print("rotation is invalid\n");
                        exit(EXIT_FAILURE);
                    }

                    r[y2 * server->width + x2] = PIXEL_FB_TO_RFB(pixel, varblock.r_offset, varblock.g_offset, varblock.b_offset);

                    if (x2 < varblock.min_i)
                        varblock.min_i = x2;
                    else
                    {
                        if (x2 > varblock.max_i)
                            varblock.max_i = x2;

                        if (y2 > varblock.max_j)
                            varblock.max_j = y2;
                        else if (y2 < varblock.min_j)
                            varblock.min_j = y2;
                    }
                }

                f++;
                c++;
            }
            fb_offset += fb_xres_line;
        }
    }
}

#undef FUNC_NAME
#undef DATA_TYPE
#undef BPP_TYPE