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


#ifndef SDL_windowspen_h_
#define SDL_windowspen_h_

#include "../../core/windows/SDL_windows.h"

#include "../SDL_sysvideo.h"

#include "../../events/SDL_pen_c.h"

/* Windows 8 types for pointer API */

/* Forward definition for SDL_x11video.h */
struct SDL_VideoData;

/* Information to map HIMETRIC units reported in pointer info to screen coordinates. */
typedef struct WIN_PenRectData
{
    /* HIMETRIC scale to screen pixels. */
    float ScaleX;
    float ScaleY;
    /* Offset to apply after scaling to get exact screen coords. */
    float OffsetX;
    float OffsetY;
} WIN_PenRectData;

/* Information stored in the driverinfo field of SDL_Pen */
typedef struct WIN_PenDriverInfo
{
    WIN_PenRectData RectData;
} WIN_PenDriverInfo;


extern void WIN_InitPen(_THIS);
extern void WIN_QuitPen(_THIS);

#endif /* SDL_windowspen_h_ */