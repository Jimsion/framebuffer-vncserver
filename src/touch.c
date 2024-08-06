#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h> /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>

#include <assert.h>
#include <errno.h>

/* libvncserver */
#include "rfb/rfb.h"

#include "touch.h"
#include "logging.h"

struct tslib_cali_param{
    int64_t param[7];
    int64_t Xres;
    int64_t Yres;
    int64_t rot;
    int64_t scale;
    int64_t xoff;
    int64_t yoff;
};

//static char TOUCH_DEVICE[256] = "/dev/input/event2";
static int touchfd = -1;

static int xmin, xmax;
static int ymin, ymax;
static int rotate;
static int trkg_id = -1;
static struct tslib_cali_param cali_param = {};

#ifndef input_event_sec
#define input_event_sec time.tv_sec
#define input_event_usec time.tv_usec
#endif

int init_touch(const char *touch_device, int vnc_rotate, const char *tslib_calibfile)
{
    info_print("Initializing touch device %s ...\n", touch_device);
    struct input_absinfo info;
    if ((touchfd = open(touch_device, O_RDWR)) == -1)
    {
        error_print("cannot open touch device %s\n", touch_device);
        return 0;
    }
    // Get the Range of X and Y
    if (ioctl(touchfd, EVIOCGABS(ABS_X), &info))
    {
        error_print("cannot get ABS_X info, %s\n", strerror(errno));
        return 0;
    }
    xmin = info.minimum;
    xmax = info.maximum;
    if (ioctl(touchfd, EVIOCGABS(ABS_Y), &info))
    {
        error_print("cannot get ABS_Y, %s\n", strerror(errno));
        return 0;
    }
    ymin = info.minimum;
    ymax = info.maximum;
    rotate = vnc_rotate;

    info_print("  x:(%d %d)  y:(%d %d) \n", xmin, xmax, ymin, ymax);

    info_print("tslib_calibfile: Init tslib calibration file.\n");

	FILE *pcal_fd;
	int index;
    
	/* Use default values that leave ts numbers unchanged after transform */
	cali_param.param[0] = 1;
	cali_param.param[1] = 0;
	cali_param.param[2] = 0;
	cali_param.param[3] = 0;
	cali_param.param[4] = 1;
	cali_param.param[5] = 0;
	cali_param.param[6] = 1;
    cali_param.Xres     = 0;
    cali_param.Yres     = 0;
    cali_param.rot      = 0;
    cali_param.scale    = 1;
    cali_param.xoff     = 0;
    cali_param.yoff     = 0;

	if (tslib_calibfile != NULL){
        pcal_fd = fopen(tslib_calibfile, "r");
		if (pcal_fd) {
            for (index = 0; index < 7; index++){
                if (fscanf(pcal_fd, "%ld", &cali_param.param[index]) != 1){
                    info_print("tslib_calibfile: read parameter fail.\n");
                    break;
                }
            }   

            if (!fscanf(pcal_fd, "%ld %ld", &cali_param.Xres, &cali_param.Yres)){
                info_print("tslib_calibfile: read resolution fail.\n");
            }

            if (!fscanf(pcal_fd, "%ld", &cali_param.rot)){
                info_print("tslib_calibfile: read rotation fail.\n");
            }            
            fclose(pcal_fd);

            // preprocess
            cali_param.scale = cali_param.param[0]*cali_param.param[4] - cali_param.param[1]*cali_param.param[3];
            cali_param.xoff = cali_param.param[1]*cali_param.param[5] - cali_param.param[2]*cali_param.param[4];
            cali_param.yoff = cali_param.param[2]*cali_param.param[3] - cali_param.param[0]*cali_param.param[5];

            info_print("tslib_calibfile: scale:%ld, xoff:%ld, yoff:%ld.\n", cali_param.scale, cali_param.xoff, cali_param.yoff);
		}else{            
            info_print("tslib_calibfile: fopen fail.\n");
        }
    }else{
        info_print("tslib_calibfile not found.\n");
    }
    return 1;
}

void cleanup_touch()
{
    if (touchfd != -1)
    {
        close(touchfd);
    }
}

void injectTouchEvent(enum MouseAction mouseAction, int x, int y, struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;
    int64_t xin = x;
    int64_t yin = y;
            
    const int64_t dX = (cali_param.param[6]*(cali_param.param[4]*xin - cali_param.param[1]*yin) + cali_param.xoff) / cali_param.scale;
    const int64_t dY = (cali_param.param[6]*(cali_param.param[0]*yin - cali_param.param[3]*xin) + cali_param.yoff) / cali_param.scale;

    x = (int)dX;
    y = (int)dY;

    memset(&ev, 0, sizeof(ev));

    bool sendPos;
    bool sendTouch;
    int trkIdValue;
    int touchValue;
    int pressure;
    struct timeval time;

    switch (mouseAction)
    {
    case MousePress:
        sendPos = true;
        sendTouch = true;
        trkIdValue = ++trkg_id;
        touchValue = 1;
        pressure = 255;
        break;
    case MouseRelease:
        sendPos = false;
        sendTouch = true;
        trkIdValue = -1;
        touchValue = 0;
        pressure = 0;
        break;
    case MouseDrag:
        sendPos = true;
        sendTouch = false;
        pressure = 255;
        break;
    default:
        error_print("invalid mouse action\n");
        exit(EXIT_FAILURE);
    }

    if (sendTouch)
    {
        // Then send a ABS_MT_TRACKING_ID
        gettimeofday(&time, 0);
        ev.input_event_sec = time.tv_sec;

        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = trkIdValue;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send a BTN_TOUCH
        gettimeofday(&time, 0);
        ev.input_event_sec = time.tv_sec;
        ev.input_event_usec = time.tv_usec;
        ev.type = EV_KEY;
        ev.code = BTN_TOUCH;
        ev.value = touchValue;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }
    }

    if (sendPos)
    {
        // Then send a ABS_MT_POSITION_X
        gettimeofday(&time, 0);
        ev.input_event_sec = time.tv_sec;
        ev.input_event_usec = time.tv_usec;
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = x;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send a ABS_MT_POSITION_Y
        gettimeofday(&time, 0);
        ev.input_event_sec = time.tv_sec;
        ev.input_event_usec = time.tv_usec;
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = y;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send the X
        gettimeofday(&time, 0);
        ev.input_event_sec = time.tv_sec;
        ev.input_event_usec = time.tv_usec;
        ev.type = EV_ABS;
        ev.code = ABS_X;
        ev.value = x;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send the Y
        gettimeofday(&time, 0);
        ev.input_event_sec = time.tv_sec;
        ev.input_event_usec = time.tv_usec;
        ev.type = EV_ABS;
        ev.code = ABS_Y;
        ev.value = y;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }
    }

    // Finally send the SYN
    gettimeofday(&time, 0);
    ev.input_event_sec = time.tv_sec;
    ev.input_event_usec = time.tv_usec;
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", xin, yin, x, y, mouseAction);
}
