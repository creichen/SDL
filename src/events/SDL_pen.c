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
#include "../SDL_internal.h"

/* Pressure-sensitive pen handling code for SDL */

#include <stdlib.h>
#include "SDL_hints.h"
#include "SDL_pen.h"
#include "SDL_events_c.h"
#include "SDL_pen_c.h"
#include "../SDL_hints_c.h"

#define PEN_MOUSE_EMULATE       0       /* pen behaves like mouse */
#define PEN_MOUSE_STATELESS     1       /* pen does not update mouse state */
#define PEN_MOUSE_DISABLED      2       /* pen does not send mouse events */

/* flags that are not SDL_PEN_FLAG_ */
#define PEN_FLAGS_CAPABILITIES (~(SDL_PEN_FLAG_NEW | SDL_PEN_FLAG_DETACHED | SDL_PEN_FLAG_STALE))

#define PEN_GET_ERASER_MASK(pen) (((pen)->header.flags & SDL_PEN_ERASER_MASK))

static int pen_mouse_emulation_mode = PEN_MOUSE_EMULATE;

static struct {
    SDL_Pen *pens; /* if "sorted" is SDL_TRUE:
                      sorted by: (is-attached, id):
                      - first all attached pens, in ascending ID order
                      - then all detached pens, in ascending ID order */
    size_t pens_allocated; /* # entries allocated to "pens" */
    size_t pens_known;     /* <= pens_allocated; this includes detached pens */
    size_t pens_attached;  /* <= pens_known; all attached pens are at the beginning of "pens" */
    SDL_bool sorted;       /* This is SDL_FALSE in the period between SDL_PenGCMark() and SDL_PenGCSWeep() */
} pen_handler;

static SDL_PenID pen_invalid = { SDL_PENID_ID_INVALID };

static SDL_PenGUID pen_guid_error = { { 0 } };

#define PEN_LOAD(penvar, penid, err_return)              \
    SDL_Pen *penvar;                                     \
    if (!(SDL_PENID_VALID(penid))) {                     \
        SDL_SetError("Invalid SDL_PenID");               \
        return (err_return);                             \
    }                                                    \
    penvar = SDL_GetPen(penid.id);                       \
    if (!(penvar)) {                                     \
        SDL_SetError("Stale SDL_PenID");                 \
        return (err_return);                             \
    }                                                    \


int
SDL_PenGUIDCompare(SDL_PenGUID lhs, SDL_PenGUID rhs)
{
    return SDL_memcmp(lhs.data, rhs.data, sizeof(lhs.data));
}

static int
pen_compare(const SDL_Pen *lhs, const SDL_Pen *rhs)
{
    int left_inactive = (lhs->header.flags & SDL_PEN_FLAG_DETACHED);
    int right_inactive = (rhs->header.flags & SDL_PEN_FLAG_DETACHED);
    if (left_inactive && !right_inactive) {
        return 1;
    }
    if (!left_inactive && right_inactive) {
        return -1;
    }
    return lhs->header.id.id - rhs->header.id.id;
}

static int
pen_header_compare(const void *lhs, const void *rhs)
{
    const struct SDL_Pen_header *l = lhs;
    const struct SDL_Pen_header *r = rhs;
    int l_detached = l->flags & SDL_PEN_FLAG_DETACHED;
    int r_detached = r->flags & SDL_PEN_FLAG_DETACHED;

    if (l_detached != r_detached) {
        if (l_detached) {
            return -1;
        }
        return 1;
    }

    return l->id.id - r->id.id;
}

SDL_Pen *
SDL_GetPen(Uint32 penid_id)
{
    int i;

    if (!pen_handler.pens) {
        return NULL;
    }

    if (pen_handler.sorted) {
        //SDL_Pen *pen = pen_bsearch(penid_id, pen_handler.pens, pen_handler.pens_known);
        struct SDL_Pen_header key = { { penid_id }, 0 };
        SDL_Pen *pen = SDL_bsearch(&key, pen_handler.pens,
                                   pen_handler.pens_known, sizeof(SDL_Pen),
                                   pen_header_compare);
        if (pen) {
            return pen;
        }
        /* If the pen is not active, fall through */
    }

    /* fall back to linear search */
    for (i = 0; i < pen_handler.pens_known; ++i) {
        if (pen_handler.pens[i].header.id.id == penid_id) {
            return &pen_handler.pens[i];
        }
    }
    return NULL;
}

int
SDL_NumPens(void)
{
    return (int) pen_handler.pens_attached;
}

SDL_PenID
SDL_PenIDForIndex(int device_index)
{
    if (device_index < 0 || device_index >= pen_handler.pens_attached) {
        SDL_SetError("Invalid pen index %d", device_index);
        return pen_invalid;
    }
    return pen_handler.pens[device_index].header.id;
}

SDL_PenID
SDL_PenIDForGUID(SDL_PenGUID guid)
{
    int i;
    /* Must do linear search */
    for (i = 0; i < pen_handler.pens_known; ++i) {
        SDL_Pen *pen = &pen_handler.pens[i];

        if (0 == SDL_PenGUIDCompare(guid, pen->guid)) {
            return pen->header.id;
        }
    }
    SDL_SetError("Could not find pen with specified GUID");
    return pen_invalid;
}

SDL_bool
SDL_PenAttached(SDL_PenID penid)
{
    SDL_Pen *pen;

    if (!(SDL_PENID_VALID(penid))) {
        return SDL_FALSE;
    }

    pen = SDL_GetPen(penid.id);
    if (!pen) {
        return SDL_FALSE;
    }
    return (pen->header.flags & SDL_PEN_FLAG_DETACHED) ? SDL_FALSE : SDL_TRUE;
}

SDL_PenGUID
SDL_PenGUIDForPenID(SDL_PenID penid)
{
    PEN_LOAD(pen, penid, pen_guid_error);
    return pen->guid;
}

const char *
SDL_PenName(SDL_PenID penid)
{
    PEN_LOAD(pen, penid, NULL);
    return pen->name;
}

Uint32
SDL_PenType(SDL_PenID penid)
{
    PEN_LOAD(pen, penid, 0u);
    return pen->type;
}

Uint32
SDL_PenCapabilities(SDL_PenID penid, SDL_PenCapabilityInfo * info)
{
    PEN_LOAD(pen, penid, 0u);
    if (info) {
        *info = pen->info;
    }
    return pen->header.flags & PEN_FLAGS_CAPABILITIES;
}

Uint32
SDL_PenStatus(SDL_PenID penid, float * x, float * y, float * axes, size_t num_axes)
{
    PEN_LOAD(pen, penid, 0u);

    if (x) {
        *x = pen->last.x;
    }
    if (y) {
        *y = pen->last.y;
    }
    if (axes && num_axes) {
        size_t axes_to_copy = SDL_min(num_axes, SDL_PEN_NUM_AXES);
        SDL_memcpy(axes, pen->last.axes, sizeof(float) * axes_to_copy);
    }
    return pen->last.buttons | (pen->header.flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK));
}

void
SDL_PenStringForGUID(SDL_PenGUID guid, char *pszGUID, int cbGUID)
{
    SDL_JoystickGetGUIDString(guid, pszGUID, cbGUID);
}

SDL_PenGUID
SDL_PenGUIDForString(const char *pchGUID)
{
    return SDL_JoystickGetGUIDFromString(pchGUID);
}

/* Backend functionality */

/* sort all pens */
static void
pen_sort(void)
{
    SDL_qsort(pen_handler.pens,
              pen_handler.pens_known,
              sizeof(SDL_Pen),
              (int (*)(const void *, const void *))pen_compare);
    pen_handler.sorted = SDL_TRUE;
}

SDL_Pen *
SDL_PenModifyBegin(Uint32 penid_id)
{
    SDL_PenID id = { penid_id };

    const size_t alloc_growth_constant = 1;  /* Expect few pens */
    SDL_Pen *pen;

    if (id.id == SDL_PENID_ID_INVALID) {
        SDL_SetError("Invalid SDL_PenID");
        return NULL;
    }

    pen = SDL_GetPen(id.id);

    if (!pen) {
        if (!pen_handler.pens || pen_handler.pens_known == pen_handler.pens_allocated) {
            size_t pens_to_allocate = pen_handler.pens_allocated + alloc_growth_constant;
            SDL_Pen *pens;
            if (pen_handler.pens) {
                pens = SDL_realloc(pen_handler.pens, sizeof(SDL_Pen) * pens_to_allocate);
                SDL_memset(pens + pen_handler.pens_known, 0,
                           sizeof(SDL_Pen) * (pens_to_allocate - pen_handler.pens_allocated));
            } else {
                pens = SDL_calloc(sizeof(SDL_Pen), pens_to_allocate);
            }
            pen_handler.pens = pens;
            pen_handler.pens_allocated = pens_to_allocate;
        }
        pen = &pen_handler.pens[pen_handler.pens_known];
        pen_handler.pens_known += 1;

        /* Default pen initialisation */
        pen->header.id = id;
        pen->header.flags = SDL_PEN_FLAG_NEW;
        pen->info.num_buttons = SDL_PEN_INFO_UNKNOWN;
        pen->info.max_tilt = SDL_PEN_INFO_UNKNOWN;
        pen->type = SDL_PEN_TYPE_PEN;
    }
    return pen;
}

void
SDL_PenModifyAddCapabilities(SDL_Pen * pen, Uint32 capabilities)
{
    pen->header.flags |= (capabilities & PEN_FLAGS_CAPABILITIES);
}

void
SDL_PenModifyEnd(SDL_Pen * pen, SDL_bool attach)
{
    SDL_bool is_new = pen->header.flags & SDL_PEN_FLAG_NEW;
    SDL_bool was_attached = !(pen->header.flags & (SDL_PEN_FLAG_DETACHED | SDL_PEN_FLAG_NEW));
    SDL_bool broke_sort_order = SDL_FALSE;

    if (pen->type == SDL_PEN_TYPE_NONE) {
        /* remove pen */
        if (!is_new) {
            SDL_Log("Error: attempt to remove known pen %d", pen->header.id.id);

            /* Treat as detached pen instead */
            pen->type = SDL_PEN_TYPE_PEN;
            attach = SDL_FALSE;
        } else {
            pen_handler.pens_known -= 1;
            SDL_memset(pen, 0, sizeof(SDL_Pen));
            return;
        }
    }

    pen->header.flags &= ~(SDL_PEN_FLAG_NEW | SDL_PEN_FLAG_STALE | SDL_PEN_FLAG_DETACHED);
    if (attach == SDL_FALSE) {
        pen->header.flags |= SDL_PEN_FLAG_DETACHED;
        if (was_attached) {
            broke_sort_order = SDL_TRUE;
            if (!is_new) {
                pen_handler.pens_attached -= 1;
            }
        }
    } else if (!was_attached || is_new) {
        broke_sort_order = SDL_TRUE;
        pen_handler.pens_attached += 1;
    }

    if (is_new) {
        /* default: name */
        if (!pen->name[0]) {
            sprintf(pen->name, "%s %d", pen->type == SDL_PEN_TYPE_ERASER ? "Eraser" : "Pen",
                    pen->header.id.id);
        }

        /* default: enabled axes */
	if (!(pen->header.flags & (SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK))) {
	    pen->info.max_tilt = 0; /* no tilt => no max_tilt */
        }

        /* sanity-check GUID */
        if (0 == SDL_PenGUIDCompare(pen->guid, pen_guid_error)) {
            SDL_Log("Error: pen %d: has GUID 0", pen->header.id.id);
        }

        /* pen or eraser? */
        if (pen->type == SDL_PEN_TYPE_ERASER || pen->header.flags & SDL_PEN_ERASER_MASK) {
            pen->header.flags = (pen->header.flags & ~SDL_PEN_INK_MASK) | SDL_PEN_ERASER_MASK;
            pen->type = SDL_PEN_TYPE_ERASER;
        } else {
            pen->header.flags = (pen->header.flags & ~SDL_PEN_ERASER_MASK) | SDL_PEN_INK_MASK;
        }

        broke_sort_order = SDL_TRUE;
    }
    if (broke_sort_order && pen_handler.sorted) {
        /* Maintain sortedness invariant */
        pen_sort();
    }
}

void
SDL_PenGCMark(void)
{
    int i;
    for (i = 0; i < pen_handler.pens_known; ++i) {
        SDL_Pen *pen = &pen_handler.pens[i];
        pen->header.flags |= SDL_PEN_FLAG_STALE;
    }
    pen_handler.sorted = SDL_FALSE;
}

void
SDL_PenGCSweep(void *context, void (*free_deviceinfo)(Uint32, void*, void*))
{
    int i;
    pen_handler.pens_attached = 0;

    /* We don't actually free the SDL_Pen entries, so that we can still answer queries about
       formerly active SDL_PenIDs later.  */
    for (i = 0; i < pen_handler.pens_known; ++i) {
        SDL_Pen *pen = &pen_handler.pens[i];

        if (pen->header.flags & SDL_PEN_FLAG_STALE) {
            pen->header.flags |= SDL_PEN_FLAG_DETACHED;
            if (pen->deviceinfo) {
                free_deviceinfo(pen->header.id.id, pen->deviceinfo, context);
                pen->deviceinfo = NULL;
            }
        } else {
            pen_handler.pens_attached += 1;
        }

        pen->header.flags &= ~SDL_PEN_FLAG_STALE;
    }
    pen_sort();
    /* We could test for changes in the above and send a hotplugging event here */
}

static void
pen_relative_coordinates(SDL_Window *window, SDL_bool window_relative, float *x, float *y)
{
    int win_x, win_y;

    if (window_relative) {
        return;
    }
    SDL_GetWindowPosition(window, &win_x, &win_y);
    *x -= win_x;
    *y -= win_y;
}

int
SDL_SendPenMotion(SDL_Window *window, SDL_PenID penid,
                  SDL_bool window_relative,
                  const SDL_PenStatusInfo *status)
{
    const SDL_Mouse *mouse = SDL_GetMouse();
    int i ;
    SDL_Pen *pen = SDL_GetPen(penid.id);
    SDL_Event event;
    SDL_bool posted;
    float x = status->x;
    float y = status->y;
    float last_x = pen->last.x;
    float last_y = pen->last.y;
    Uint16 last_buttons = pen->last.buttons;
    /* Suppress mouse updates for axis changes or sub-pixel movement: */
    SDL_bool send_mouse_update;
    SDL_bool axes_changed = SDL_FALSE;

    pen_relative_coordinates(window, window_relative, &x, &y);

    /* Check if the event actually modifies any cached axis or location, update as neeed */
    if (x != last_x || y != last_y) {
        axes_changed = SDL_TRUE;
        pen->last.x = status->x;
        pen->last.y = status->y;
    }
    for (i = 0; i < SDL_PEN_NUM_AXES; ++i) {
        if ((pen->header.flags & SDL_PEN_AXIS_CAPABILITY(i))
            && (status->axes[i] != pen->last.axes[i])) {
            axes_changed = SDL_TRUE;
            pen->last.axes[i] = status->axes[i];
        }
    }
    if (!axes_changed) {
        /* No-op event */
        return SDL_FALSE;
    }

    send_mouse_update = ((int) x) != ((int)(last_x)) || ((int) y) != ((int)(last_y));

    if (!(SDL_IsMousePositionInWindow(mouse->focus, mouse->mouseID, (int) x, (int) y))) {
        return SDL_FALSE;
    }

    event.pmotion.type = SDL_PENMOTION;
    event.pmotion.windowID = mouse->focus ? mouse->focus->id : 0;
    event.pmotion.which = penid;
    event.pmotion.pen_state = (Uint16) last_buttons | PEN_GET_ERASER_MASK(pen);
    event.pmotion.x = x;
    event.pmotion.y = y;
    SDL_memcpy(event.pmotion.axes, status->axes, SDL_PEN_NUM_AXES * sizeof(float));

    posted = SDL_PushEvent(&event) > 0;

    if (!posted) {
        return SDL_FALSE;
    }

    if (send_mouse_update) {
        switch (pen_mouse_emulation_mode) {
        case PEN_MOUSE_EMULATE:
            return SDL_SendMouseMotion(window, SDL_PEN_MOUSEID, 0, (int) x, (int) y);

        case PEN_MOUSE_STATELESS:
            /* Report mouse event but don't update mouse state */
            event.motion.windowID = event.pmotion.windowID;
            event.motion.which = SDL_PEN_MOUSEID;
            event.motion.type = SDL_MOUSEMOTION;
            event.motion.state = pen->last.buttons | PEN_GET_ERASER_MASK(pen);
            event.motion.x = (int) x;
            event.motion.y = (int) y;
            event.motion.xrel = (int) (last_x - x);
            event.motion.yrel = (int) (last_y - y);
            return SDL_PushEvent(&event) > 0 ? SDL_TRUE : SDL_FALSE;

        default:
            return SDL_TRUE;
        }
    }
    return SDL_TRUE;
}

int
SDL_SendPenButton(SDL_Window *window, SDL_PenID penid,
                  Uint8 state, Uint8 button)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Pen *pen = SDL_GetPen(penid.id);
    SDL_Event event;
    SDL_bool posted;
    SDL_PenStatusInfo *last = &pen->last;

    if (!(SDL_IsMousePositionInWindow(mouse->focus, mouse->mouseID, (int) last->x, (int) last->y))) {
        return SDL_FALSE;
    }

    if (state == SDL_PRESSED) {
        event.pbutton.type = SDL_PENBUTTONDOWN;
        pen->last.buttons |= (1 << (button - 1));
    } else {
        event.pbutton.type = SDL_PENBUTTONUP;
        pen->last.buttons &= ~(1 << (button - 1));
    }

    event.pbutton.windowID = mouse->focus ? mouse->focus->id : 0;
    event.pbutton.which = penid;
    event.pbutton.button = button;
    event.pbutton.state = state == SDL_PRESSED ? SDL_PRESSED : SDL_RELEASED;
    event.pmotion.pen_state = (Uint16) pen->last.buttons | PEN_GET_ERASER_MASK(pen);
    event.pbutton.x = last->x;
    event.pbutton.y = last->y;
    SDL_memcpy(event.pbutton.axes, last->axes, SDL_PEN_NUM_AXES * sizeof(float));

    posted = SDL_PushEvent(&event) > 0;

    if (!posted) {
        return SDL_FALSE;
    }

    switch (pen_mouse_emulation_mode) {
    case PEN_MOUSE_EMULATE:
        return SDL_SendMouseButton(window, SDL_PEN_MOUSEID, state, button);

    case PEN_MOUSE_STATELESS:
        /* Report mouse event without updating mouse state */
        event.button.windowID = event.pbutton.windowID;
        event.button.type = state == SDL_PRESSED ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.which = SDL_PEN_MOUSEID;
        event.button.state = state;
        event.button.button = button;
        event.button.clicks = 1;
        event.button.x = (int) last->x;
        event.button.y = (int) last->y;
        return SDL_PushEvent(&event) > 0;
        break;

    default:
        return SDL_TRUE;
    }
}

static void
SDL_PenUpdateHint(void *userdata, const char *name, const char *oldvalue, const char *newvalue)
{
    if (newvalue == NULL) {
        return;
    }

    if (0 == SDL_strcmp("2", newvalue)) {
        pen_mouse_emulation_mode = PEN_MOUSE_DISABLED;
    } else if (0 == SDL_strcmp("1", newvalue)) {
        pen_mouse_emulation_mode = PEN_MOUSE_STATELESS;
    } else if (0 == SDL_strcmp("0", newvalue)) {
        pen_mouse_emulation_mode = PEN_MOUSE_EMULATE;
    } else {
        SDL_Log("Unexpected value for %s: '%s'", SDL_HINT_PEN_NOT_MOUSE, newvalue);
    }
}

int
SDL_PenInit(void)
{
    SDL_AddHintCallback(SDL_HINT_PEN_NOT_MOUSE,
                        SDL_PenUpdateHint, NULL);

    return 0;
}

/* Vendor-specific bits */

/* Default pen names */
#define PEN_NAME_AES      0
#define PEN_NAME_ART      1
#define PEN_NAME_AIRBRUSH 2
#define PEN_NAME_GENERAL  3
#define PEN_NAME_GRIP     4
#define PEN_NAME_INKING   5
#define PEN_NAME_PRO      6
#define PEN_NAME_PRO2     7
#define PEN_NAME_PRO3D    8
#define PEN_NAME_PRO_SLIM 9
#define PEN_NAME_STROKE   10

#define PEN_NAME_LAST     PEN_NAME_STROKE
#define PEN_NUM_NAMES     (PEN_NAME_LAST + 1)

const static char* default_pen_names[PEN_NUM_NAMES] = {
    /* PEN_NAME_AES */
    "AES Pen",
    /* PEN_NAME_ART */
    "Art Pen",
    /* PEN_NAME_AIRBRUSH */
    "Airbrush Pen",
    /* PEN_NAME_GENERAL */
    "Pen",
    /* PEN_NAME_GRIP */
    "Grip Pen",
    /* PEN_NAME_INKING */
    "Inking Pen",
    /* PEN_NAME_PRO */
    "Pro Pen",
    /* PEN_NAME_PRO2 */
    "Pro Pen 2",
    /* PEN_NAME_PRO3D */
    "Pro Pen 3D",
    /* PEN_NAME_PRO_SLIM */
    "Pro Pen Slim",
    /* PEN_NAME_STROKE */
    "Stroke Pen"
};

#define PEN_SPEC_TYPE_SHIFT    0
#define PEN_SPEC_TYPE_MASK     0x0000000fu
#define PEN_SPEC_BUTTONS_SHIFT 4
#define PEN_SPEC_BUTTONS_MASK  0x000000f0u
#define PEN_SPEC_NAME_SHIFT    8
#define PEN_SPEC_NAME_MASK     0x00000f00u
#define PEN_SPEC_AXES_SHIFT    0
#define PEN_SPEC_AXES_MASK     0xffff0000u

#define PEN_SPEC(name, buttons, type, axes) (0                          \
    | (PEN_SPEC_NAME_MASK & ((name) << PEN_SPEC_NAME_SHIFT))            \
    | (PEN_SPEC_BUTTONS_MASK & ((buttons) << PEN_SPEC_BUTTONS_SHIFT))   \
    | (PEN_SPEC_TYPE_MASK & ((type) << PEN_SPEC_TYPE_SHIFT))            \
    | (PEN_SPEC_AXES_MASK & ((axes) << PEN_SPEC_AXES_SHIFT))            \
    )                                                                   \

/* Returns a suitable pen name string from default_pen_names on success, otherwise NULL. */
static const char *
pen_wacom_identify_tool(Uint32 requested_wacom_id, int *num_buttons, int *tool_type, int *axes)
{
    int i;

    /* List of known Wacom pens, extracted from libwacom.stylus and warom_wac.c in the Linux kernel.
       Could be complemented by dlopen()ing libwacom, in the future (if new pens get added).  */
    struct {
        /* Compress properties to 8 bytes per device in order to keep memory cost well below 1k.
           Could be compressed further with more complex code.  */
        Uint32 wacom_id;
        Uint32 properties;
    } tools[] = {
        {  0x0001, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0011, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0019, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0021, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0031, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0039, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0049, PEN_SPEC(PEN_NAME_GENERAL,  1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0071, PEN_SPEC(PEN_NAME_GENERAL,  1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0221, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0231, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0271, PEN_SPEC(PEN_NAME_GENERAL,  1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0421, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0431, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0621, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0631, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK) },
        {  0x0801, PEN_SPEC(PEN_NAME_INKING,   0, SDL_PEN_TYPE_PENCIL,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0802, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0804, PEN_SPEC(PEN_NAME_ART,      2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_ROTATION_MASK) },
        {  0x080a, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x080c, PEN_SPEC(PEN_NAME_ART,      2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0812, PEN_SPEC(PEN_NAME_INKING,   0, SDL_PEN_TYPE_PENCIL,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0813, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x081b, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0822, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0823, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x082a, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x082b, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0832, PEN_SPEC(PEN_NAME_STROKE,   0, SDL_PEN_TYPE_BRUSH,    SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0842, PEN_SPEC(PEN_NAME_PRO2,     2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x084a, PEN_SPEC(PEN_NAME_PRO2,     2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0852, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x085a, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0862, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0885, PEN_SPEC(PEN_NAME_ART,      0, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_ROTATION_MASK) },
        {  0x08e2, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0902, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_AIRBRUSH, SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_THROTTLE_MASK) },
        {  0x090a, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0912, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_AIRBRUSH, SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_THROTTLE_MASK) },
        {  0x0913, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_AIRBRUSH, SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x091a, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x091b, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x0d12, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_AIRBRUSH, SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_THROTTLE_MASK) },
        {  0x0d1a, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x8051, PEN_SPEC(PEN_NAME_AES,      0, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK) },
        {  0x805b, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK) },
        {  0x806b, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK) },
        {  0x807b, PEN_SPEC(PEN_NAME_GENERAL,  1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK) },
        {  0x826b, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK) },
        {  0x846b, PEN_SPEC(PEN_NAME_AES,      1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK) },
        {  0x2802, PEN_SPEC(PEN_NAME_INKING,   0, SDL_PEN_TYPE_PENCIL,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x4802, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x480a, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        {  0x8842, PEN_SPEC(PEN_NAME_PRO3D,    3, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x10802, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x10804, PEN_SPEC(PEN_NAME_ART,      2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_ROTATION_MASK) },
        { 0x1080a, PEN_SPEC(PEN_NAME_GRIP,     2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x1080c, PEN_SPEC(PEN_NAME_ART,      2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x10842, PEN_SPEC(PEN_NAME_PRO_SLIM, 2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x1084a, PEN_SPEC(PEN_NAME_PRO_SLIM, 2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x10902, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_AIRBRUSH, SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_THROTTLE_MASK) },
        { 0x1090a, PEN_SPEC(PEN_NAME_AIRBRUSH, 1, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x12802, PEN_SPEC(PEN_NAME_INKING,   0, SDL_PEN_TYPE_PENCIL,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x14802, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x1480a, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x16802, PEN_SPEC(PEN_NAME_PRO,      2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x1680a, PEN_SPEC(PEN_NAME_PRO,      2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x18802, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_PEN,      SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0x1880a, PEN_SPEC(PEN_NAME_GENERAL,  2, SDL_PEN_TYPE_ERASER,   SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK) },
        { 0, 0 } };

    /* The list of pens is sorted, so we could do binary search, but this call should be pretty rare. */
    for (i = 0; tools[i].wacom_id; ++i) {
        if (tools[i].wacom_id == requested_wacom_id) {
            Uint32 properties = tools[i].properties;
            int name_index = (properties & PEN_SPEC_NAME_MASK) >> PEN_SPEC_NAME_SHIFT;

            *num_buttons = (properties & PEN_SPEC_BUTTONS_MASK) >> PEN_SPEC_BUTTONS_SHIFT;
            *tool_type = (properties & PEN_SPEC_TYPE_MASK) >> PEN_SPEC_TYPE_SHIFT;
            *axes =  (properties & PEN_SPEC_AXES_MASK) >> PEN_SPEC_AXES_SHIFT;

            return default_pen_names[name_index];
        }
    }
    return NULL;
}

int
SDL_PenModifyFromWacomID(SDL_Pen *pen, Uint32 wacom_devicetype_id, Uint32 wacom_serial_id, Uint32 * axis_flags)
{
    const char *name = NULL;
    int num_buttons;
    int tool_type;
    int axes;
    int i;

#if defined(__LINUX__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    /* According to Ping Cheng, the curent Wacom for Linux maintainer, device IDs on Linux
       squeeze a "0" nibble after the 3rd (least significant) nibble.
       This may also affect the *BSDs, so they are heuristically included here.
       On those platforms, we first try the "patched" ID: */
    if (0 == (wacom_devicetype_id & 0x0000f000u)) {
        const Uint32 lower_mask = 0xfffu;
        int wacom_devicetype_alt_id = ((wacom_devicetype_id & ~lower_mask) << 4)
                                    |  (wacom_devicetype_id &  lower_mask);

        name = pen_wacom_identify_tool(wacom_devicetype_alt_id, &num_buttons, &tool_type, &axes);
        if (name) {
            wacom_devicetype_id = wacom_devicetype_alt_id;
        }
    }
#endif
    name = pen_wacom_identify_tool(wacom_devicetype_id, &num_buttons, &tool_type, &axes);

    if (!name) {
        return SDL_FALSE;
    }

    *axis_flags = axes;

    /* Always set GUID (highest-entropy data first) */
    for (i = 0; i < 4; ++i) {
        pen->guid.data[0 + i] = (wacom_serial_id >> (i * 8)) & 0xff;
    }
    for (i = 0; i < 4; ++i) {
        pen->guid.data[4 + i] = (wacom_devicetype_id >> (i * 8)) & 0xff;
    }
    SDL_memcpy(&pen->guid.data[8], "WACM", 4);

    /* Override defaults */
    if (pen->info.num_buttons == SDL_PEN_INFO_UNKNOWN) {
        pen->info.num_buttons = num_buttons;
    }
    if (pen->type == SDL_PEN_TYPE_PEN) {
        pen->type = tool_type;
    }
    if (pen->info.max_tilt == SDL_PEN_INFO_UNKNOWN) {
        pen->info.max_tilt = 64; /* supposedly: 64 degrees left, 63 right, as reported by the Wacom X11 driver */
    }

    if (0 == pen->name[0]) {
        sprintf(pen->name, "Wacom %s%s", name, (tool_type == SDL_PEN_TYPE_ERASER)? " Eraser" : "");
    }
    return SDL_TRUE;
}
