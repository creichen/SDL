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

#if SDL_VIDEO_DRIVER_X11_XINPUT2

#include "SDL_x11video.h"

#include "SDL_x11pen.h"
#include "SDL_x11xinput2.h"
#include "../../events/SDL_pen_c.h"

#include <stdio.h>

//#define DEBUG_PEN 1

void
X11_InitPen(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    int i;
    XIDeviceInfo *device_info;
    int num_device_info;

    /* We assume that a device is a PEN or an ERASER if it has atom_pressure: */
    const Atom atom_pressure = X11_XInternAtom(data->display, "Abs Pressure", 1);
    const Atom atom_xtilt = X11_XInternAtom(data->display, "Abs Tilt X", 1);
    const Atom atom_ytilt = X11_XInternAtom(data->display, "Abs Tilt Y", 1);

    /* We assume that a device is an ERASER if it has atom_pressure and contains the string "eraser".
     * Unfortunately there doesn't seem to be a clean way to distinguish these cases (as of 2022-03)
     * but this hack (which is also what GDK uses) seems to work (?) */
    static const char *eraser_name_tag = "eraser";

    data->num_pens = 0;

    device_info = X11_XIQueryDevice(data->display, XIAllDevices, &num_device_info);
    if (!device_info) {
        return;
    }
    for (i = 0; i < num_device_info; ++i) {
        const XIDeviceInfo *dev = &device_info[i];
        int classct;
        int k;

        /* Check for pen or eraser and set properties suitably */
        SDL_bool is_pen_or_eraser = SDL_FALSE;
        struct SDL_X11Pen pen_device;

        pen_device.deviceid = dev->deviceid;

        for (k = 0; k < SDL_PEN_NUM_AXIS; ++k) {
            pen_device.valuator_for_axis[k] = SDL_PEN_AXIS_VALUATOR_MISSING;
        }

        /* printf("Device %d name = '%s'\n", dev->deviceid, dev->name); */
        for (classct = 0; classct < dev->num_classes; ++classct) {
            const XIAnyClassInfo *classinfo = dev->classes[classct];

            switch (classinfo->type) {
            case XIValuatorClass: {
                XIValuatorClassInfo *val_classinfo = (XIValuatorClassInfo*) classinfo;
                Atom vname = val_classinfo->label;
                int axis = -1;

                if (vname == atom_pressure) {
                    /* pressure-sensitive is the sole requirement for being pen or eraser */
                    is_pen_or_eraser = SDL_TRUE;

                    axis = SDL_PEN_AXIS_PRESSURE;
                } else if (vname == atom_xtilt) {
                    axis = SDL_PEN_AXIS_XTILT;
                } else if (vname == atom_ytilt) {
                    axis = SDL_PEN_AXIS_YTILT;
                }

                if (axis >= 0) {
                    Sint8 valuator_nr = val_classinfo->number;

                    pen_device.valuator_for_axis[axis] = valuator_nr;
                    pen_device.valuator_min[axis] = val_classinfo->min;
                    pen_device.valuator_max[axis] = val_classinfo->max;
                }
                break;
            }
            default:
                break;
            }
        }

        /* Success */
        if (is_pen_or_eraser) {
#define DEV_NAME_MAXLEN 256
            char dev_name[DEV_NAME_MAXLEN];

            if (data->num_pens == SDL_MAX_PEN_DEVICES) {
                SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "More than %d pen or eraser devices detected: not supported\n",
                    SDL_MAX_PEN_DEVICES);

                X11_XIFreeDeviceInfo(device_info);
                return;
            }

            /* Check: is this an eraser? */
            strncpy(dev_name, dev->name, DEV_NAME_MAXLEN - 1);
            dev_name[DEV_NAME_MAXLEN - 1] = 0;
#undef DEV_NAME_MAXLEN
            for (k = 0; dev_name[k]; ++k) {
                dev_name[k] = tolower(dev_name[k]);
            }

            pen_device.flags = 0;
            if (SDL_strstr(dev_name, eraser_name_tag)) {
                pen_device.flags |= SDL_PEN_FLAG_ERASER;
            }

            /* Done collecting data, write to device info */
            data->pens[data->num_pens++] = pen_device;
#ifdef DEBUG_PEN
            printf("[pen] pen #%d, deviceid=%d: %s; valuators pressure=%d, xtilt=%d, ytilt=%d\n",
                   data->num_pens - 1,
                   pen_device.deviceid, (pen_device.flags & SDL_PEN_FLAG_ERASER) ? "eraser" : "pen",
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_PRESSURE],
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_XTILT],
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_YTILT]);
#endif
        }

    }
    X11_XIFreeDeviceInfo(device_info);
    X11_PenXinput2SelectEvents(_this);
}

SDL_X11Pen *
X11_FindPen(SDL_VideoData *videodata, int deviceid)
{
    int i;

    // List is short enough for linear search
    for (i = 0; i < videodata->num_pens; ++i) {
        if (videodata->pens[i].deviceid == deviceid) {
            return &videodata->pens[i];
        }
    }
    return NULL;
}

void xinput2_normalise_pen_axes(const SDL_X11Pen *pen,
                                /* inout-mode paramters: */
                                float *coords) {
    int axis;

    /* Normalise axes */
    for (axis = 0; axis < SDL_PEN_NUM_AXIS; ++axis) {
        if (pen->valuator_for_axis[axis] != SDL_PEN_AXIS_VALUATOR_MISSING) {
            float value = coords[axis];

            float min = pen->valuator_min[axis];
            float max = pen->valuator_max[axis];

            /* min ... 0 ... max */
            if (min < 0.0) {
                /* Normalise so that 0 remains 0.0 */
                if (value < 0) {
                    value = value / (-min);
                } else {
                    if (max != 0.0) {
                        value = value / max;
                    } else {
                        value = 0.0f;
                    }
                }
            } else {
                /* 0 ... min ... max */
                /* including 0.0 = min */
                if (max == 0.0) {
                    value = 0.0f;
                } else {
                    value = (value - min) / max;
                }
            }

            coords[axis] = value;
        }
    }
}

void
X11_PenAxesFromValuators(const SDL_X11Pen *pen,
                         const double *input_values, const unsigned char *mask, const int mask_len,
                         /* out-mode parameters: */
                         float axis_values[SDL_PEN_NUM_AXIS]) {
    int i;

    for (i = 0; i < SDL_PEN_NUM_AXIS; ++i) {
        const int valuator = pen->valuator_for_axis[i];
        if (valuator == SDL_PEN_AXIS_VALUATOR_MISSING
            || valuator >= mask_len * 8
            || !(XIMaskIsSet(mask, valuator))) {
            axis_values[i] = 0.0f;
        } else {
            axis_values[i] = input_values[valuator];
        }
    }
    xinput2_normalise_pen_axes(pen, axis_values);
}

void
X11_PenXinput2SelectEvents(_THIS)
{
    const SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    int axis;
    XIEventMask eventmask;
    unsigned char mask[3] = { 0,0,0 };

    eventmask.mask_len = sizeof(mask);
    eventmask.mask = mask;

    XISetMask(mask, XI_RawButtonPress);
    XISetMask(mask, XI_RawButtonRelease);
    XISetMask(mask, XI_RawMotion);

    XISetMask(mask, XI_ButtonPress);
    XISetMask(mask, XI_ButtonRelease);
    XISetMask(mask, XI_Motion);

    for (axis = 0; axis < data->num_pens; ++axis) {
        const SDL_X11Pen *pen = &data->pens[axis];

        /* Enable XI Motion and Button events for pens */
        eventmask.deviceid = pen->deviceid;

        if (X11_XISelectEvents(data->display,DefaultRootWindow(data->display), &eventmask, 1) != Success) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Could enable motion and button tracking for pen %d\n",
                        pen->deviceid);
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_X11_XINPUT2 */

/* vi: set ts=4 sw=4 expandtab: */
