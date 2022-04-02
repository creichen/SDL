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

#include "SDL_pen_c.h"
#include "SDL_events_c.h"

static void
pen_relative_coordinates(SDL_Window *window, SDL_bool relative, float *x, float *y)
{
    int win_x, win_y;

    if (relative) {
        return;
    }
    SDL_GetWindowPosition(window, &win_x, &win_y);
    *x -= win_x;
    *y -= win_y;
}


int
SDL_SendPenMotion(SDL_Window *window, SDL_MouseID penID, SDL_bool eraser,
                  SDL_bool is_relative,
                  float x, float y, const float axes[SDL_PEN_NUM_AXIS])
{
    const SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Event event;
    SDL_bool posted;

    pen_relative_coordinates(window, is_relative, &x, &y);

    if (!(SDL_IsMousePositionInWindow(mouse->focus, mouse->mouseID, (int) x, (int) y))) {
        return SDL_FALSE;
    }

    event.pmotion.type = SDL_PENMOTION;
    event.pmotion.windowID = mouse->focus ? mouse->focus->id : 0;
    event.pmotion.which = penID;
    event.pmotion.eraser = eraser;
    event.pmotion.state = SDL_GetMouseState(NULL, NULL);
    event.pmotion.x = x;
    event.pmotion.y = y;
    event.pmotion.pressure = axes[SDL_PEN_AXIS_PRESSURE];
    event.pmotion.xtilt = axes[SDL_PEN_AXIS_XTILT];
    event.pmotion.ytilt = axes[SDL_PEN_AXIS_YTILT];

    posted = SDL_PushEvent(&event) > 0;

    if (!posted) {
        return SDL_FALSE;
    }

    return SDL_SendMouseMotion(window, SDL_PEN_MOUSEID, 0, x, y);
}

int
SDL_SendPenButton(SDL_Window *window, SDL_MouseID penID, SDL_bool eraser,
                  Uint8 state, Uint8 button,
                  SDL_bool is_relative,
                  float x, float y, const float axes[SDL_PEN_NUM_AXIS])
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Event event;
    SDL_bool posted;

    pen_relative_coordinates(window, is_relative, &x, &y);

    if (!(SDL_IsMousePositionInWindow(mouse->focus, mouse->mouseID, (int) x, (int) y))) {
        return SDL_FALSE;
    }

    event.pbutton.type = state == SDL_PRESSED ? SDL_PENBUTTONDOWN : SDL_PENBUTTONUP;
    event.pbutton.windowID = mouse->focus ? mouse->focus->id : 0;
    event.pbutton.which = penID;
    event.pbutton.eraser = eraser;
    event.pbutton.button = button;
    event.pbutton.state = state == SDL_PRESSED ? SDL_PRESSED : SDL_RELEASED;
    event.pbutton.x = x;
    event.pbutton.y = y;
    event.pbutton.pressure = axes[SDL_PEN_AXIS_PRESSURE];
    event.pbutton.xtilt = axes[SDL_PEN_AXIS_XTILT];
    event.pbutton.ytilt = axes[SDL_PEN_AXIS_YTILT];

    posted = SDL_PushEvent(&event) > 0;

    if (!posted) {
        return SDL_FALSE;
    }

    return SDL_SendMouseButton(window, SDL_PEN_MOUSEID, state, button);
}
