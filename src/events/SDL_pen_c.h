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

#ifndef SDL_pen_c_h_
#define SDL_pen_c_h_

#include "SDL_mouse_c.h"

#  define SDL_PEN_AXIS_PRESSURE 0
#  define SDL_PEN_AXIS_XTILT    1
#  define SDL_PEN_AXIS_YTILT    2
#  define SDL_PEN_AXIS_LAST     SDL_PEN_AXIS_YTILT
#  define SDL_PEN_NUM_AXIS      (1 + SDL_PEN_AXIS_LAST)

/* Send a pen motion event. Can be reported either relative to window or absolute to screen. */
extern int SDL_SendPenMotion(SDL_Window * window, SDL_MouseID penID, SDL_bool eraser, SDL_bool is_relative, float x, float y, const float axes[SDL_PEN_NUM_AXIS]);

/* Send a pen motion event. (x,y) = (axes[0],axes[1]), axes[2:] are the SDL_PEN_NUMAXIS axes.  is_relative indicates whether window-relative or screen-absolute.. */
extern int SDL_SendPenButton(SDL_Window * window, SDL_MouseID penID, SDL_bool eraser, Uint8 state, Uint8 button, SDL_bool is_relative, float x, float y, const float axes[SDL_PEN_NUM_AXIS]);

#endif /* SDL_pen_c_h_ */

/* vi: set ts=4 sw=4 expandtab: */
