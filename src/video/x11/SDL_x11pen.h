/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifndef SDL_x11pen_h_
#define SDL_x11pen_h_

#if SDL_VIDEO_DRIVER_X11_XINPUT2

#include "../../events/SDL_pen_c.h"

/* Pressure-sensitive pen */

typedef struct SDL_X11Pen {
    int deviceid;
    Sint8 valuator_for_axis[SDL_PEN_NUM_AXIS]; /* SDL_PEN_AXIS_VALUATOR_MISSING if not supported */
    Uint8 flags;  /* SDL_PEN_FLAG_* */
    float valuator_min[SDL_PEN_NUM_AXIS];
    float valuator_max[SDL_PEN_NUM_AXIS];
}  SDL_X11Pen;

/* Forward definition for SDL_x11video.h */
struct SDL_VideoData;


/* Max # of pens supported */
#define SDL_MAX_PEN_DEVICES 4

/* Flags with detail information on pens */
#define SDL_PEN_FLAG_ERASER     (1 << 0) /* Pen is actually an eraser */

#define SDL_PEN_AXIS_VALUATOR_MISSING   -1


/* Function definitions */

/* Detect XINPUT2 devices that are pens or erasers */
extern void X11_InitPen(_THIS);

/* Find pen/eraser by device ID */
extern SDL_X11Pen * X11_FindPen(struct SDL_VideoData *videodata, int deviceid);

/* Converts XINPUT2 valuators into pen axis information, including normalisation */
extern void X11_PenAxesFromValuators(const SDL_X11Pen *pen,
                                     const double *input_values, const unsigned char *mask, const int mask_len,
                                     /* out-mode parameters: */
                                     float axis_values[SDL_PEN_NUM_AXIS]);

/* Request XI_Motion, XI_ButtonPress and XIButtonRelease events for all pens */
extern void X11_PenXinput2SelectEvents(_THIS);

#endif /* SDL_VIDEO_DRIVER_X11_XINPUT2 */

#endif /* SDL_x11pen_h_ */

/* vi: set ts=4 sw=4 expandtab: */
