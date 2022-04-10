/*
  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

#include "SDL.h"
#include <stdlib.h> /* for exit() */

#include "SDL_test.h"
#include "SDL_test_common.h"

#define WIDTH 1600
#define HEIGHT 1200

#define VERBOSE 0

static SDLTest_CommonState *state;
static int quitting = 0;

static float last_x, last_y;
static float last_xtilt, last_ytilt, last_pressure;
static int last_button;
static int last_was_eraser;

static void
DrawScreen(SDL_Renderer *renderer)
{
    float xdelta, ydelta, endx, endy;

    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0x40, 0x40, 0x40, 0xff);
    SDL_RenderClear(renderer);

    /* Mark screen position for pen */
    if (last_was_eraser) {
        SDL_Rect rect = {
            .x = last_x - 10,
            .y = last_y - 10,
            .w = 21,
            .h = 21 };

        SDL_SetRenderDrawColor(renderer, 0x00, 0xff, 0xff, 0xff);
        SDL_RenderFillRect(renderer, &rect);
    } else {
        SDL_SetRenderDrawColor(renderer, 0xff, 0, 0, 0xff);
        SDL_RenderDrawLineF(renderer, last_x - 10, last_y, last_x + 10, last_y);
        SDL_RenderDrawLineF(renderer, last_x, last_y - 10, last_x, last_y + 10);
    }

    /* Draw a cone based on the direction the pen is leaning as if it were shining a light. */
    /* Colour derived from pens, intensity based on pressure: */
    SDL_SetRenderDrawColor(renderer,
                           (last_button & 0x01)? 0xff : 0,
                           (last_button & 0x02)? 0xff : 0,
                           (last_button & 0x04)? 0xff : 0,
                           (int) (0xff));

    xdelta = -last_xtilt * 100;
    ydelta = -last_ytilt * 100;
    endx = last_x + xdelta;
    endy = last_y + ydelta;
    SDL_RenderDrawLineF(renderer, last_x, last_y, endx, endy);

    SDL_SetRenderDrawColor(renderer,
                           (last_button & 0x01)? 0xff : 0,
                           (last_button & 0x02)? 0xff : 0,
                           (last_button & 0x04)? 0xff : 0,
                           (int) (0xff * last_pressure));
    /* Cone base width based on pressure: */
    SDL_RenderDrawLineF(renderer, last_x, last_y, endx + (ydelta * last_pressure / 3.0), endy - (xdelta * last_pressure / 3.0));
    SDL_RenderDrawLineF(renderer, last_x, last_y, endx - (ydelta * last_pressure / 3.0), endy + (xdelta * last_pressure / 3.0));

    SDL_RenderPresent(renderer);
}

static void
dump_state()
{
    int i;
    SDL_Log("Found %d pens\n", SDL_NumPens());
    for (i = 0; i < SDL_NumPens(); ++i) {
        SDL_PenID penid = SDL_PenIDForIndex(i);
        SDL_PenGUID guid = SDL_PenGUIDForPenID(penid);
        SDL_PenGUID guid2;
        char guid_str[33];
        float axes[SDL_PEN_NUM_AXES];
        float x, y;
        int k;
        Uint32 status = SDL_PenStatus(penid, &x, &y, axes, SDL_PEN_NUM_AXES);

        SDL_PenStringForGUID(guid, guid_str, 33);

        SDL_Log("Pen %d: [%s] connected=%d [cap= %08x:%08x =status] '%s'\n",
                penid.id, guid_str,
                SDL_PenConnected(penid),
                SDL_PenCapabilities(penid),
                status,
                SDL_PenName(penid)
            );
        SDL_Log("   pos=(%.2f, %.2f)", x, y);
        for (k = 0; k < SDL_PEN_NUM_AXES; ++k) {
            SDL_Log("   axis %d:  %.3f", k, axes[k]);
        }
        if (SDL_PenGUIDCompare(guid, guid) != 0) {
            SDL_Log("   ERROR: PenGUIDCompare\n");
        }
        guid2 = SDL_PenGUIDForPenID(penid);
        if (SDL_PenGUIDCompare(guid, guid2) != 0) {
            SDL_Log("   ERROR: PenGUIDForPenID() consistency\n");
        }
        if (SDL_PenGUIDCompare(guid, SDL_PenGUIDForString(guid_str)) != 0) {
            SDL_Log("   ERROR: PenGUIDCompare or PenGUIDForString\n");
        }
        if (SDL_PenIDForGUID(guid).id != penid.id) {
            SDL_Log("   ERROR: PenIDForGUID\n");
        }


        guid2.data[15] = 0;
        guid.data[15] = 1;
        if (SDL_PenGUIDCompare(guid2, guid) >= 0) {
            SDL_Log("   ERROR: PenGUIDCompare(smaller, bigger) = %d \n",
                    SDL_PenGUIDCompare(guid2, guid));
        }
        if (SDL_PenGUIDCompare(guid, guid2) <= 0) {
            SDL_Log("   ERROR: PenGUIDCompare(bigger, smaller) = %d \n",
                    SDL_PenGUIDCompare(guid, guid2));
        }
    }
}

static void
process_event(SDL_Event event)
{
    SDLTest_CommonEvent(state, &event, &quitting);

    switch (event.type) {
    case SDL_KEYDOWN: {
        dump_state();
        break;
    }
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
#if VERBOSE
	int x, y;

	SDL_GetMouseState(&x, &y);

	if (event.type == SDL_MOUSEMOTION) {
	    SDL_Log("mouse motion: mouse ID %d is at %d,%d  (state: %d,%d) delta (%d, %d)\n",
		    event.motion.which,
		    event.motion.x, event.motion.y,
		    event.motion.xrel, event.motion.yrel,
		    x, y);
	} else {
	    SDL_Log("mouse button: mouse ID %d is at %d,%d  (state: %d,%d)\n",
		    event.button.which,
		    event.button.x, event.button.y,
		    x, y);
	}

#endif
        if (event.motion.which != SDL_PEN_MOUSEID) {
            SDL_ShowCursor(SDL_ENABLE);
        }
        break;

    case SDL_PENMOTION: {
        SDL_PenMotionEvent *ev = &event.pmotion;

        SDL_ShowCursor(SDL_DISABLE);
        last_x = ev->x;
        last_y = ev->y;
        last_xtilt = ev->axes[SDL_PEN_AXIS_XTILT];
        last_ytilt = ev->axes[SDL_PEN_AXIS_YTILT];
        last_pressure = ev->axes[SDL_PEN_AXIS_PRESSURE];
        last_was_eraser = ev->pen_state & SDL_PEN_ERASER_MASK;
#if VERBOSE
        SDL_Log("pen motion: %s %u at %f,%f; pressure=%.3f, tilt=%.3f/%.3f [buttons=%02x]\n",
                last_was_eraser ? "eraser" : "pen",
                (unsigned int) ev->which.id, ev->x, ev->y, last_pressure, last_xtilt, last_ytilt,
                ev->pen_state);
#endif
        break;
    }
    case SDL_PENBUTTONUP:
    case SDL_PENBUTTONDOWN: {
        SDL_PenButtonEvent *ev = &event.pbutton;

        SDL_ShowCursor(SDL_DISABLE);
        last_x = ev->x;
        last_y = ev->y;
        last_xtilt = ev->axes[SDL_PEN_AXIS_XTILT];
        last_ytilt = ev->axes[SDL_PEN_AXIS_YTILT];
        last_pressure = ev->axes[SDL_PEN_AXIS_PRESSURE];
        last_was_eraser = ev->pen_state & SDL_PEN_ERASER_MASK;
        last_button = ev->state == SDL_PRESSED ? ev->button : 0;
#if VERBOSE
        SDL_Log("pen button: %s %u at %f,%f; BUTTON %d reported %s with event %s [pressure=%.3f, tilt=%.3f/%.3f]\n",
                last_was_eraser ? "eraser" : "pen",
                (unsigned int) ev->which.id, ev->x, ev->y,
                ev->button,
                (ev->state == SDL_PRESSED) ? "PRESSED"
                : ((ev->state == SDL_RELEASED) ? "RELEASED" : "--invalid--"),
                event.type == SDL_PENBUTTONUP ? "PENBUTTONUP" : "PENBUTTONDOWN",
                last_pressure, last_xtilt, last_ytilt);
#endif
        break;
    }
    default:
        break;
    }
}

static void
loop(void)
{
    SDL_Event event;
    int i;

    for (i = 0; i < state->num_windows; ++i) {
        if (state->renderers[i]) {
            DrawScreen(state->renderers[i]);
        }
    }

    if (SDL_WaitEventTimeout(&event, 10)) {
        process_event(event);
    }
    while (SDL_PollEvent(&event)) {
        process_event(event);
    }
}

int main(int argc, char* argv[])
{
    state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
    if (!state) {
        return 1;
    }

    state->window_title = "Pressure-Sensitive Pen Test";
    state->window_w = WIDTH;
    state->window_h = HEIGHT;
    state->skip_renderer = SDL_FALSE;

    if (!SDLTest_CommonDefaultArgs(state, argc, argv) || !SDLTest_CommonInit(state)) {
        SDLTest_CommonQuit(state);
        return 1;
    }

    while (!quitting) {
        loop();
    }

    SDLTest_CommonQuit(state);
    return 0;
}
