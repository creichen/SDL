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

#define PEN_MOUSE_EMULATE	0	/* pen behaves like mouse */
#define PEN_MOUSE_STATELESS	1	/* pen does not update mouse state */
#define PEN_MOUSE_DISABLED	2	/* pen does not send mouse events */

static int pen_mouse_emulation_mode = PEN_MOUSE_EMULATE;

static struct {
    SDL_Pen *pens;
    size_t pens_allocated;
    size_t pens_used;
    SDL_bool unsorted; /* SDL_PenGCMark() has been called but SDL_PenGCSWeep() has not */
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
pen_id_compare(const SDL_PenID *lhs, const SDL_PenID *rhs)
{
    return lhs->id - rhs->id;
}

/* binary search for pen */ /* FIXME: replace by SDL_bsearch() once available */
static SDL_Pen*
pen_bsearch(Uint32 penid_id, SDL_Pen *pens, size_t size)
{
    while (size) {
        size_t midpoint = size >> 1;
        Uint32 midpoint_penid_id = pens[midpoint].id.id;

        if (midpoint_penid_id == penid_id) {
            return &pens[midpoint];
        } else if (midpoint_penid_id < penid_id) {
            pens += midpoint + 1;
            size -= midpoint + 1; /* mindpoint < size, since size >= 1 */
        } else {
            size = midpoint;
        }
    }
    return NULL;
}

SDL_Pen *
SDL_GetPen(Uint32 penid_id)
{
    if (!pen_handler.pens) {
        return NULL;
    }

    if (pen_handler.unsorted) {
        /* fall back to linear search */
        int i;
        for (i = 0; i < pen_handler.pens_used; ++i) {
            if (pen_handler.pens[i].id.id == penid_id) {
                return &pen_handler.pens[i];
            }
        }
        return NULL;
    }

    return pen_bsearch(penid_id, pen_handler.pens, pen_handler.pens_used);
}

int
SDL_NumPens(void)
{
    return (int) pen_handler.pens_used;
}

SDL_PenID
SDL_PenIDForIndex(int device_index)
{
    if (device_index < 0 || device_index >= pen_handler.pens_used) {
        SDL_SetError("Invalid pen index %d", device_index);
        return pen_invalid;
    }
    return pen_handler.pens[device_index].id;
}

SDL_PenID
SDL_PenIDForGUID(SDL_PenGUID guid)
{
    int i;
    /* Must do linear search */
    for (i = 0; i < pen_handler.pens_used; ++i) {
        SDL_Pen *pen = &pen_handler.pens[i];

        if (0 == SDL_PenGUIDCompare(guid, pen->guid)) {
            return pen->id;
        }
    }
    SDL_SetError("Could not find pen with specified GUID");
    return pen_invalid;
}

SDL_bool
SDL_PenConnected(SDL_PenID penid)
{
    SDL_Pen *pen;

    if (!(SDL_PENID_VALID(penid))) {
        return SDL_FALSE;
    }

    pen = SDL_GetPen(penid.id);
    if (!pen) {
        return SDL_FALSE;
    }
    return (pen->flags & SDL_PEN_FLAG_INACTIVE) ? SDL_FALSE : SDL_TRUE;
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
SDL_PenCapabilities(SDL_PenID penid)
{
    PEN_LOAD(pen, penid, 0u);
    return pen->flags & ~(SDL_PEN_FLAG_STALE | SDL_PEN_FLAG_INACTIVE);
}

Uint32
SDL_PenStatus(SDL_PenID penid, float * x, float * y, float * axes, size_t num_axes)
{
    PEN_LOAD(pen, penid, 0u);

    if (x) {
        *x = pen->last_x;
    }
    if (y) {
        *y = pen->last_y;
    }
    if (axes && num_axes) {
        size_t axes_to_copy = SDL_min(num_axes, SDL_PEN_NUM_AXES);
        SDL_memcpy(axes, pen->last_axes, sizeof(float) * axes_to_copy);
    }
    return pen->last_status;
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

void
SDL_PenGCMark(void)
{
    int i;
    for (i = 0; i < pen_handler.pens_used; ++i) {
        SDL_Pen *pen = &pen_handler.pens[i];
        pen->flags |= SDL_PEN_FLAG_STALE;
    }
    pen_handler.unsorted = SDL_TRUE;
}

SDL_Pen *
SDL_PenRegister(SDL_PenID id, SDL_PenGUID guid, char *name, Uint32 capabilities)
{
    const size_t alloc_growth_constant = 1;  /* Expect few pens */

    SDL_Pen *pen = SDL_GetPen(id.id);
    if (!pen) {
        if (!pen_handler.pens || pen_handler.pens_used == pen_handler.pens_allocated) {
            size_t pens_to_allocate = pen_handler.pens_allocated + alloc_growth_constant;
            SDL_Pen *pens;
            if (pen_handler.pens) {
                pens = SDL_realloc(pen_handler.pens, sizeof(SDL_Pen) * pens_to_allocate);
                SDL_memset(pens + pen_handler.pens_used, 0,
                           sizeof(SDL_Pen) * (pens_to_allocate - pen_handler.pens_allocated));
            } else {
                pens = SDL_calloc(sizeof(SDL_Pen), pens_to_allocate);
            }
            pen_handler.pens = pens;
            pen_handler.pens_allocated = pens_to_allocate;
        }
        pen = &pen_handler.pens[pen_handler.pens_used];
        pen_handler.pens_used += 1;
    }
    pen->id = id;
    pen->flags = capabilities & (~SDL_PEN_FLAG_STALE);
    pen->guid = guid;
    SDL_strlcpy(pen->name, name, SDL_PEN_MAX_NAME);

    return pen;
}

void
SDL_PenGCSweep(void *context, void (*free_deviceinfo)(Uint32, void*, void*))
{
    int i;
    /* We don't actually free the SDL_Pen entries, so that we can still answer queries about
       formerly active SDL_PenIDs later.  */
    for (i = 0; i < pen_handler.pens_used; ++i) {
        SDL_Pen *pen = &pen_handler.pens[i];
        if (pen->flags & SDL_PEN_FLAG_STALE) {
            pen->flags |= SDL_PEN_FLAG_INACTIVE;
            if (pen->deviceinfo) {
		free_deviceinfo(pen->id.id, pen->deviceinfo, context);
                pen->deviceinfo = NULL;
            }
        }
        pen->flags &= ~SDL_PEN_FLAG_STALE;
    }
    SDL_qsort(pen_handler.pens,
              pen_handler.pens_used,
              sizeof(SDL_Pen),
              (int (*)(const void *, const void *))pen_id_compare);
    pen_handler.unsorted = SDL_FALSE;
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

static void
pen_update_state(SDL_Pen *pen, const float x, const float y, const float axes[SDL_PEN_NUM_AXES])
{
    pen->last_x = x;
    pen->last_y = y;
    SDL_memcpy(pen->last_axes, axes, sizeof(pen->last_axes));
}

int
SDL_SendPenMotion(SDL_Window *window, SDL_PenID penid,
                  SDL_bool window_relative,
                  float x, float y, const float axes[SDL_PEN_NUM_AXES])
{
    const SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Pen *pen = SDL_GetPen(penid.id);
    SDL_Event event;
    SDL_bool posted;
    float last_x = pen->last_x;
    float last_y = pen->last_y;
    /* Suppress mouse updates for axis changes or sub-pixel movement: */
    SDL_bool send_mouse_update = ((int) x) != ((int)(last_x)) || ((int) y) != ((int)(last_y));

    pen_relative_coordinates(window, window_relative, &x, &y);

    if (!(SDL_IsMousePositionInWindow(mouse->focus, mouse->mouseID, (int) x, (int) y))) {
        return SDL_FALSE;
    }

    event.pmotion.type = SDL_PENMOTION;
    event.pmotion.windowID = mouse->focus ? mouse->focus->id : 0;
    event.pmotion.which = penid;
    event.pmotion.pen_state = (Uint16) (pen->last_status | (pen->flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK)));
    event.pmotion.x = x;
    event.pmotion.y = y;
    SDL_memcpy(event.pmotion.axes, axes, SDL_PEN_NUM_AXES * sizeof(float));

    posted = SDL_PushEvent(&event) > 0;

    if (!posted) {
        return SDL_FALSE;
    }

    pen_update_state(pen, x, y, axes);

    if (send_mouse_update) {
	switch (pen_mouse_emulation_mode) {
	case PEN_MOUSE_EMULATE:
	    return SDL_SendMouseMotion(window, SDL_PEN_MOUSEID, 0, (int) x, (int) y);

	case PEN_MOUSE_STATELESS:
	    /* Report mouse event but don't update mouse state */
	    event.motion.windowID = event.pmotion.windowID;
	    event.motion.which = SDL_PEN_MOUSEID;
	    event.motion.type = SDL_MOUSEMOTION;
	    event.motion.state = pen->last_status;
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
                  Uint8 state, Uint8 button,
                  SDL_bool window_relative,
                  float x, float y, const float axes[SDL_PEN_NUM_AXES])
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Pen *pen = SDL_GetPen(penid.id);
    SDL_Event event;
    SDL_bool posted;

    pen_relative_coordinates(window, window_relative, &x, &y);

    if (!(SDL_IsMousePositionInWindow(mouse->focus, mouse->mouseID, (int) x, (int) y))) {
        return SDL_FALSE;
    }

    if (state == SDL_PRESSED) {
        event.pbutton.type = SDL_PENBUTTONDOWN;
        pen->last_status |= (1 << (button - 1));
    } else {
        event.pbutton.type = SDL_PENBUTTONUP;
        pen->last_status &= ~(1 << (button - 1));
    }

    pen_update_state(pen, x, y, axes);

    event.pbutton.windowID = mouse->focus ? mouse->focus->id : 0;
    event.pbutton.which = penid;
    event.pbutton.button = button;
    event.pbutton.state = state == SDL_PRESSED ? SDL_PRESSED : SDL_RELEASED;
    event.pmotion.pen_state = (Uint16) pen->last_status | (pen->flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK));
    event.pbutton.x = x;
    event.pbutton.y = y;
    SDL_memcpy(event.pbutton.axes, axes, SDL_PEN_NUM_AXES * sizeof(float));

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
        event.button.x = (int) x;
        event.button.y = (int) y;
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
