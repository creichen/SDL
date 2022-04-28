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

#include "SDL_pen.h"
#include "SDL_x11video.h"
#include "SDL_x11pen.h"
#include "SDL_x11xinput2.h"
#include "../../events/SDL_pen_c.h"

#define PEN_ERASER_ID_MAXLEN 256      /* Max # characters of device name to scan */
#define PEN_ERASER_NAME_TAG "eraser"  /* String constant to identify erasers */

#define DEBUG_PEN 0


#define SDL_PEN_AXIS_VALUATOR_MISSING   -1

typedef struct xinput2_pen {
    float axis_shift[SDL_PEN_NUM_AXES];
    float axis_min[SDL_PEN_NUM_AXES];
    float axis_max[SDL_PEN_NUM_AXES];
    Sint8 valuator_for_axis[SDL_PEN_NUM_AXES]; /* SDL_PEN_AXIS_VALUATOR_MISSING if not supported */
}  xinput2_pen;


static struct {
    int initialized;        /* initialised to 0 */
    Atom device_product_id;
    Atom abs_pressure;
    Atom abs_tilt_x;
    Atom abs_tilt_y;
    Atom wacom_serial_ids;
    Atom wacom_tool_type;
} pen_atoms;


static void
pen_atoms_ensure_initialized(_THIS) {
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;

    if (pen_atoms.initialized) {
        return;
    }
    /* Create atoms if they don't exist yet to pre-empt hotplugging updates */
    pen_atoms.device_product_id = X11_XInternAtom(data->display, "Device Product ID", False);
    pen_atoms.wacom_serial_ids = X11_XInternAtom(data->display, "Wacom Serial IDs", False);
    pen_atoms.wacom_tool_type = X11_XInternAtom(data->display, "Wacom Tool Type", False);
    pen_atoms.abs_pressure = X11_XInternAtom(data->display, "Abs Pressure", True);
    pen_atoms.abs_tilt_x = X11_XInternAtom(data->display, "Abs Tilt X", True);
    pen_atoms.abs_tilt_y = X11_XInternAtom(data->display, "Abs Tilt Y", True);

    pen_atoms.initialized = 1;
}

/* Read out an integer property and store into a preallocated Sint32 array, extending 8 and 16 bit values suitably.
   Returns number of Sint32s written (<= max_words), or 0 on error. */
static size_t
xinput2_pen_get_int_property(_THIS, int deviceid, Atom property, Sint32* dest, size_t max_words)
{
    const SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    Atom type_return;
    int format_return;
    unsigned long num_items_return;
    unsigned long bytes_after_return;
    unsigned char *output;

    if (property == None) {
        return 0;
    }

    if (Success != X11_XIGetProperty(data->display, deviceid,
                                     property,
                                     0, max_words, False,
                                     XA_INTEGER, &type_return, &format_return,
                                     &num_items_return, &bytes_after_return,
                                     &output)
        || num_items_return == 0
        || output == NULL) {
        return 0;
    }

    if (type_return == XA_INTEGER) {
        int k;
        const int to_copy = SDL_min(max_words, num_items_return);

        if (format_return == 8) {
            Sint8 *numdata = (Sint8 *) output;
            for (k = 0; k < to_copy; ++k) {
                dest[k] = numdata[k];
            }
        } else if (format_return == 16) {
            Sint16 *numdata = (Sint16 *) output;
            for (k = 0; k < to_copy; ++k) {
                dest[k] = numdata[k];
            }
        } else {
            SDL_memcpy(dest, output, sizeof(Sint32) * to_copy);
        }
        X11_XFree(output);
        return to_copy;
    }
    return 0; /* type mismatch */
}

/* 32 bit vendor + device ID from evdev */
static Uint32
xinput2_pen_evdevid(_THIS, int deviceid)
{
    Sint32 ids[2];

    pen_atoms_ensure_initialized(_this);

    if (2 != xinput2_pen_get_int_property(_this, deviceid, pen_atoms.device_product_id, ids, 2)) {
        return 0;
    }
    return ((ids[0] << 16) | (ids[1] & 0xffff));
}


/* Gets unique generic device ID */
static SDL_PenGUID
xinput2_pen_get_generic_guid(_THIS, int deviceid)
{
    SDL_PenGUID guid;
    Uint32 evdevid = xinput2_pen_evdevid(_this, deviceid); /* also initialises pen_atoms  */
    SDL_memset(guid.data, 0, sizeof(guid));
    SDL_memcpy(guid.data, &evdevid, 4);

    return guid;
}

/* Heuristically determines if device is an eraser */
static SDL_bool
xinput2_pen_is_eraser(_THIS, int deviceid, char* devicename)
{
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    char dev_name[PEN_ERASER_ID_MAXLEN];
    int k;

    pen_atoms_ensure_initialized(_this);

    if (pen_atoms.wacom_tool_type != None) {
        Atom type_return;
        int format_return;
        unsigned long num_items_return;
        unsigned long bytes_after_return;
        unsigned char *tooltype_name_info;

        /* Try Wacom-specific method */
        if (Success == X11_XIGetProperty(data->display, deviceid,
                                         pen_atoms.wacom_tool_type,
                                         0, 32, False,
                                         AnyPropertyType, &type_return, &format_return,
                                         &num_items_return, &bytes_after_return,
                                         &tooltype_name_info)
            && tooltype_name_info != NULL
            && num_items_return > 0) {

            SDL_bool result = SDL_FALSE;
            char *tooltype_name = NULL;

            if (type_return == XA_ATOM) {
                /* Atom instead of string?  Un-intern */
                Atom atom = *((Atom *) tooltype_name_info);
                if (atom != None) {
                    tooltype_name = X11_XGetAtomName(data->display, atom);
                }
            } else if (type_return == XA_STRING && format_return == 8) {
                tooltype_name = (char*) tooltype_name_info;
            }

            if (tooltype_name) {
                if (0 == SDL_strcasecmp(tooltype_name, PEN_ERASER_NAME_TAG)) {
                    result = SDL_TRUE;
                }
                X11_XFree(tooltype_name_info);

                return result;
            }
        }
    }
    /* Non-Wacom device? */

    /* We assume that a device is an eraser if its name contains the string "eraser".
     * Unfortunately there doesn't seem to be a clean way to distinguish these cases (as of 2022-03). */

    SDL_strlcpy(dev_name, devicename, PEN_ERASER_ID_MAXLEN);
    /* lowercase device name string so we can use strstr() */
    for (k = 0; dev_name[k]; ++k) {
        dev_name[k] = tolower(dev_name[k]);
    }

    return (SDL_strstr(dev_name, PEN_ERASER_NAME_TAG)) ? SDL_TRUE : SDL_FALSE;
}

static void
xinput2_pen_free_deviceinfo(Uint32 deviceid, void *x11_peninfo, void* context)
{
    SDL_free(x11_peninfo);
}

static void
xinput2_merge_deviceinfo(xinput2_pen *dest, xinput2_pen *src)
{
    *dest = *src;
}

/**
 * For Wacom pens: identify number of buttons and extra axis (if present)
 *
 * \param _this Global state
 * \param pen The pen to initialise
 * \param deviceid XInput2 device ID
 * \param[out] valuator_5 Meaning of the valuator with offset 5, if any
 *   (written only if known and if the device has a 6th axis,
 *   e.g., for the Wacom Art Pen and Wacom Airbrush Pen)
 * \param[out] axes Bitmask of all possibly supported axes
 * \returns SDL_TRUE if the device is a Wacom device
 *
 * This function identifies Wacom device types through a Wacom-specific device ID.
 * It then fills in pen details from an internal database.
 * If the device seems to be a Wacom pen/eraser but can't be identified, the function
 * leaves "axes" untouched and sets the other outputs to common defaults.
 */
static SDL_bool
xinput2_wacom_peninfo(_THIS, SDL_Pen *pen, int deviceid, int * valuator_5, Uint32 * axes)
{
    Sint32 serial_id_buf[3];
    int result;

    pen_atoms_ensure_initialized(_this);

    if ((result = xinput2_pen_get_int_property(_this, deviceid, pen_atoms.wacom_serial_ids, serial_id_buf, 3)) == 3) {
        Uint32 wacom_devicetype_id = serial_id_buf[2];
        Uint32 wacom_serial = serial_id_buf[1];

#if DEBUG_PEN
        printf("[pen] Pen %d reports Wacom device_id %x\n",
               deviceid, wacom_devicetype_id);
#endif

        if (SDL_PenModifyFromWacomID(pen, wacom_devicetype_id, wacom_serial, axes)) {
            if (*axes & SDL_PEN_AXIS_THROTTLE_MASK) {
                /* Air Brush Pen or eraser */
                *valuator_5 = SDL_PEN_AXIS_THROTTLE;
            } else if (*axes & SDL_PEN_AXIS_ROTATION_MASK) {
                /* Art Pen or eraser, or 6D Art Pen */
                *valuator_5 = SDL_PEN_AXIS_ROTATION;
            }
        } else {
#if DEBUG_PEN
            printf("[pen] Could not identify %d with %x, using default settings\n",
                   deviceid, wacom_devicetype_id);
#endif
        }

        return SDL_TRUE;
    } else {
#if DEBUG_PEN
        printf("[pen] Pen %d is not a Wacom device: %d\n", deviceid, result);
#endif
    }
    return SDL_FALSE;
}

void
X11_InitPen(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    int i;
    XIDeviceInfo *device_info;
    int num_device_info;

    device_info = X11_XIQueryDevice(data->display, XIAllDevices, &num_device_info);
    if (!device_info) {
        return;
    }

    SDL_PenGCMark();

    for (i = 0; i < num_device_info; ++i) {
        const XIDeviceInfo *dev = &device_info[i];
        int classct;
        xinput2_pen pen_device;
        Uint32 capabilities = 0;
        Uint32 axis_mask = ~0; /* Permitted axes (default: all) */
        int valuator_5_axis = -1; /* For Wacom devices, the 6th valuator (offset 5) has a model-specific meaning */
        SDL_Pen *pen;
	SDL_bool have_vendor_guid = SDL_FALSE;

        /* Only track physical devices that are enabled */
        if (dev->use != XISlavePointer || dev->enabled == 0) {
            continue;
        }

        pen = SDL_PenModifyBegin(dev->deviceid);

        /* Complement XF86 driver information with vendor-specific details */
        have_vendor_guid = xinput2_wacom_peninfo(_this, pen, dev->deviceid, &valuator_5_axis, &axis_mask);

        for (classct = 0; classct < dev->num_classes; ++classct) {
            const XIAnyClassInfo *classinfo = dev->classes[classct];

            switch (classinfo->type) {
            case XIValuatorClass: {
                XIValuatorClassInfo *val_classinfo = (XIValuatorClassInfo*) classinfo;
                Sint8 valuator_nr = val_classinfo->number;
                Atom vname = val_classinfo->label;
                int axis = -1;
                SDL_bool force_positive_axis = SDL_FALSE;

                if (vname == pen_atoms.abs_pressure) {
                    axis = SDL_PEN_AXIS_PRESSURE;
                } else if (vname == pen_atoms.abs_tilt_x) {
                    axis = SDL_PEN_AXIS_XTILT;
                } else if (vname == pen_atoms.abs_tilt_y) {
                    axis = SDL_PEN_AXIS_YTILT;
                }

                if (axis == -1 && valuator_nr == 5) {
                    /* Wacom model-specific axis support */
                    axis = valuator_5_axis;

                    /* cf. xinput2_wacom_peninfo for how this axis is used.
                       In all current cases, our API wants this value in 0..1, but the xf86 driver
                       starts at a negative offset, so we normalise here. */

                    force_positive_axis = SDL_TRUE;
                }

                if (axis >= 0) {
                    float min = val_classinfo->min;
                    float max = val_classinfo->max;
                    float shift = (force_positive_axis) ? -val_classinfo->min : 0.0f;
                    capabilities |= SDL_PEN_AXIS_CAPABILITY(axis);

                    pen_device.valuator_for_axis[axis] = valuator_nr;
                    pen_device.axis_min[axis] = min;
                    pen_device.axis_max[axis] = max;
                    pen_device.axis_shift[axis] = shift;
		    if (axis == SDL_PEN_AXIS_XTILT
			|| axis == SDL_PEN_AXIS_YTILT) {
			pen->info.max_tilt = (Sint8) (-min > max) ? -min : max;
		    }
                }
                break;
            }
            default:
                break;
            }
        }

        /* We have a pen if and only if the device measures pressure */
        if (capabilities & SDL_PEN_AXIS_PRESSURE_MASK) {
            xinput2_pen *xinput2_deviceinfo;

	    if (!have_vendor_guid) {
		pen->guid = xinput2_pen_get_generic_guid(_this, dev->deviceid);
	    }

            if (xinput2_pen_is_eraser(_this, dev->deviceid, dev->name)) {
                pen->type = SDL_PEN_TYPE_ERASER;
            } else {
                pen->type = SDL_PEN_TYPE_PEN;
            }

            /* Done collecting data, write to pen */
            SDL_PenModifyAddCapabilities(pen, capabilities);
            SDL_strlcpy(pen->name, dev->name, SDL_PEN_MAX_NAME);

            if (pen->deviceinfo) {
                /* Updating a known pen */
                xinput2_deviceinfo = (xinput2_pen*) pen->deviceinfo;
                xinput2_merge_deviceinfo(xinput2_deviceinfo, &pen_device);
            } else {
                /* Registering a new pen */
                xinput2_deviceinfo = SDL_malloc(sizeof(xinput2_pen));
                SDL_memcpy(xinput2_deviceinfo, &pen_device, sizeof(xinput2_pen));
            }
            pen->deviceinfo = xinput2_deviceinfo;

#if DEBUG_PEN
            printf("[pen] pen %d [%04x] valuators pressure=%d, xtilt=%d, ytilt=%d [%s]\n",
                   pen->header.id.id, pen->header.flags,
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_PRESSURE],
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_XTILT],
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_YTILT],
                   pen->name);
#endif
        } else {
            /* Mark for deletion */
            pen->type = SDL_PEN_TYPE_NONE;
        }
        SDL_PenModifyEnd(pen, SDL_TRUE);

    }
    X11_XIFreeDeviceInfo(device_info);

    SDL_PenGCSweep(NULL, xinput2_pen_free_deviceinfo);
}

static void
xinput2_normalise_pen_axes(const xinput2_pen *pen,
                           /* inout-mode paramters: */
                           float *coords) {
    int axis;

    /* Normalise axes */
    for (axis = 0; axis < SDL_PEN_NUM_AXES; ++axis) {
        int valuator = pen->valuator_for_axis[axis];
        if (valuator != SDL_PEN_AXIS_VALUATOR_MISSING) {
            float value = coords[axis] + pen->axis_shift[axis];
            float min = pen->axis_min[axis];
            float max = pen->axis_max[axis];

            /* min ... 0 ... max */
            if (min < 0.0) {
                /* Normalise so that 0 remains 0.0 */
                if (value < 0) {
                    value = value / (-min);
                } else {
                    if (max == 0.0) {
                        value = 0.0f;
                    } else {
                        value = value / max;
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
X11_PenAxesFromValuators(const SDL_Pen *peninfo,
                         const double *input_values, const unsigned char *mask, const int mask_len,
                         /* out-mode parameters: */
                         float axis_values[SDL_PEN_NUM_AXES]) {
    const xinput2_pen *pen = (xinput2_pen *) peninfo->deviceinfo;
    int i;

    for (i = 0; i < SDL_PEN_NUM_AXES; ++i) {
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

#endif /* SDL_VIDEO_DRIVER_X11_XINPUT2 */

/* vi: set ts=4 sw=4 expandtab: */
