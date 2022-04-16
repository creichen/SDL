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
    float valuator_min[SDL_PEN_NUM_AXES];
    float valuator_max[SDL_PEN_NUM_AXES];
    Sint8 valuator_for_axis[SDL_PEN_NUM_AXES]; /* SDL_PEN_AXIS_VALUATOR_MISSING if not supported */
}  xinput2_pen;


static struct {
    int initialized;        /* initialised to 0 */
    Atom device_product_id;
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
    pen_atoms.initialized = 1;
}

void
SDL_PenGetGUIDString(SDL_PenGUID guid, char* dest, int maxlen)
{
    unsigned int k;
    size_t available = (SDL_max(0, maxlen - 1) >> 1); /* number bytes we can serialise */
    size_t to_write = SDL_min(available, sizeof(guid.data));

    for (k = 0; k < to_write; ++k) {
        sprintf(dest + (k * 2), "%02x", guid.data[k]);
    }
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


/* Gets unique device ID (which will be shared for pen / eraser pairs) */
static SDL_PenGUID
xinput2_pen_get_guid(_THIS, int deviceid)
{
    SDL_PenGUID guid;
    Uint32 evdevid = xinput2_pen_evdevid(_this, deviceid); /* also initialises pen_atoms  */

    Uint32 guid_words_base[sizeof(guid.data) / sizeof(Uint32)];
    Uint32 *guid_words = guid_words_base;
    const size_t guid_words_total = sizeof(guid.data) / sizeof(Uint32);
    Uint32 *guid_words_end = guid_words + guid_words_total;

    /* pen_atoms was initialised by xinput2_pen_evdevid() earlier */
    Atom vendor_guid_properties[] = { /* List of vendor-specific GUID sources */
        /* Atom must not be None */
        pen_atoms.wacom_serial_ids,
        None /* terminator */
    };
    Atom *vendor_guid_it;

    SDL_memset(guid_words, 0, sizeof(guid));

    /* Always put the evdevid in the first four bytes */
    *guid_words = evdevid;
    guid_words += 1;

    /* XInput2 does not offer a general-purpose GUID, so we try vendor-specific GUID information */
    for (vendor_guid_it = vendor_guid_properties;
         guid_words_end != guid_words /* space left? */
             && *vendor_guid_it != None;
         ++vendor_guid_it) {

        const Atom property = *vendor_guid_it;
        const int words_written = xinput2_pen_get_int_property(_this, deviceid, property, (Sint32 *) guid_words, guid_words_end - guid_words);

        guid_words += words_written;
    }

    memcpy(guid.data, guid_words_base, sizeof(guid_words_base));
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

void
X11_InitPen(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    int i;
    XIDeviceInfo *device_info;
    int num_device_info;

    /* We assume that a device is a PEN or an ERASER if it has atom_pressure: */
    const Atom atom_pressure = X11_XInternAtom(data->display, "Abs Pressure", True);
    const Atom atom_xtilt = X11_XInternAtom(data->display, "Abs Tilt X", True);
    const Atom atom_ytilt = X11_XInternAtom(data->display, "Abs Tilt Y", True);

    device_info = X11_XIQueryDevice(data->display, XIAllDevices, &num_device_info);
    if (!device_info) {
        return;
    }

    SDL_PenGCMark();

    for (i = 0; i < num_device_info; ++i) {
        const XIDeviceInfo *dev = &device_info[i];
        int classct;
        int k;

        /* Check for pen or eraser and set properties suitably */
        xinput2_pen pen_device;
        Uint32 capabilities = 0;

        /* Only track physical devices that are enabled */
        if (dev->use != XISlavePointer || dev->enabled == 0) {
            continue;
        }

        for (k = 0; k < SDL_PEN_NUM_AXES; ++k) {
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
                    axis = SDL_PEN_AXIS_PRESSURE;
                } else if (vname == atom_xtilt) {
                    axis = SDL_PEN_AXIS_XTILT;
                } else if (vname == atom_ytilt) {
                    axis = SDL_PEN_AXIS_YTILT;
                }

                if (axis >= 0) {
                    Sint8 valuator_nr = val_classinfo->number;
                    capabilities |= SDL_PEN_AXIS_CAPABILITY(axis);

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

        /* We have a pen if and only if the device measures pressure */
        if (capabilities & SDL_PEN_AXIS_PRESSURE_MASK) {
            SDL_PenID penid = { dev->deviceid };
            SDL_PenGUID guid = xinput2_pen_get_guid(_this, dev->deviceid);
            SDL_Pen *pen;
            xinput2_pen *xinput2_deviceinfo;

            if (xinput2_pen_is_eraser(_this, dev->deviceid, dev->name)) {
                capabilities |= SDL_PEN_ERASER_MASK;
            } else {
                capabilities |= SDL_PEN_INK_MASK;
            }

            pen = SDL_PenRegister(penid, guid, dev->name, capabilities);
            if (!pen->deviceinfo) {
                pen->deviceinfo = xinput2_deviceinfo = SDL_malloc(sizeof(xinput2_pen));
                SDL_memcpy(xinput2_deviceinfo, &pen_device, sizeof(xinput2_pen));
            } else {
                xinput2_deviceinfo = (xinput2_pen*) pen->deviceinfo;
                xinput2_merge_deviceinfo(xinput2_deviceinfo, &pen_device);
            }

            /* Done collecting data, write to device info */
#if DEBUG_PEN
            printf("[pen] pen %d [%04x] valuators pressure=%d, xtilt=%d, ytilt=%d [%s]\n",
                   pen->id.id, pen->flags,
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_PRESSURE],
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_XTILT],
                   pen_device.valuator_for_axis[SDL_PEN_AXIS_YTILT],
                   pen->name);
#endif
        }

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
