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

/**
 *  \file SDL_test.h
 *
 *  Include file for SDL test framework.
 *
 *  This code is a part of the SDL2_test library, not the main SDL library.
 */

#ifndef SDL_test_h_
#define SDL_test_h_

#include "SDL.h"
#include "SDL_test_assert.h"
#include "SDL_test_common.h"
#include "SDL_test_compare.h"
#include "SDL_test_crc32.h"
#include "SDL_test_font.h"
#include "SDL_test_fuzzer.h"
#include "SDL_test_harness.h"
#include "SDL_test_images.h"
#include "SDL_test_log.h"
#include "SDL_test_md5.h"
#include "SDL_test_memory.h"
#include "SDL_test_random.h"
#define SDL_PEN_MAX_NAME 128
typedef struct SDL_Pen {
    SDL_PenID id;      /* PenID determines sort order */
    Uint32 flags;      /* capabilities */
    SDL_PenGUID guid;
    Uint32 last_status;
    float last_x, last_y;
    float last_axes[SDL_PEN_NUM_AXES];
    char name[SDL_PEN_MAX_NAME];
    void *deviceinfo;  /* implementation-specific information */
} SDL_Pen;


extern SDL_Pen * SDL_GetPen(Uint32 penid_id);

/* Mark all current pens for garbage collection.
   To handle a hotplug event in a pen implementation:
   - SDL_PenGCMark()
   - SDL_PenRegister() for all pens (this will retain existing state for old pens)
   - SDL_PenGCSweep()  (will now delete all pens that were not re-registered).  */
extern void SDL_PenGCMark(void);

/* Register a pen with the pen API.
   - If the PenID already exists, overwrite name, guid, and capabilities
   - Otherwise initialise fresh SDL_Pen
   The returned SDL_Pen pointer is only valid until the next call to SDL_PenGCSweep. */
extern SDL_Pen * SDL_PenRegister(SDL_PenID id, SDL_PenGUID guid, char *name, Uint32 capabilities);

/* Free memory associated with all remaining stale pens and marks them inactive.
   Calls "free_deviceinfo" on non-NULL deviceinfo that should be deallocated.
   "context" is optional and passed on to free_deviceinfo, unaltered.
*/
extern void SDL_PenGCSweep(void *context, void (*free_deviceinfo)(Uint32 penid_id, void* deviceinfo, void *context));


/* Send a pen motion event. Can be reported either relative to window or absolute to screen. */
extern int SDL_SendPenMotion(SDL_Window * window, SDL_PenID, SDL_bool window_relative, float x, float y, const float axes[SDL_PEN_NUM_AXES]);

/* Send a pen motion event. (x,y) = (axes[0],axes[1]), axes[2:] are the SDL_PEN_NUMAXIS axes.  is_relative indicates whether window-relative or screen-absolute.. */
extern int SDL_SendPenButton(SDL_Window * window, SDL_PenID penID, Uint8 state, Uint8 button, SDL_bool window_relative, float x, float y, const float axes[SDL_PEN_NUM_AXES]);


#include "begin_code.h"
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/* Global definitions */

/*
 * Note: Maximum size of SDLTest log message is less than SDL's limit
 * to ensure we can fit additional information such as the timestamp.
 */
#define SDLTEST_MAX_LOGMESSAGE_LENGTH   3584

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include "close_code.h"

#endif /* SDL_test_h_ */

/* vi: set ts=4 sw=4 expandtab: */
