/*
  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/*  Usage:
 *  Spacebar to begin recording a gesture on all touches.
 *  s to save all touches into "./gestureSave"
 *  l to load all touches from "./gestureSave"
 */

#include "SDL.h"
#include <stdlib.h> /* for exit() */

#include "SDL_test.h"
#include "SDL_test_common.h"

#define WIDTH 1600
#define HEIGHT 1200

#define VERBOSE 1

static SDLTest_CommonState *state;
static int quitting = 0;

static float last_x, last_y;
static float last_xtilt, last_ytilt, last_pressure;
static int last_button;
static int last_is_eraser;

static void
DrawScreen(SDL_Renderer *renderer)
{
    float xdelta, ydelta, endx, endy;

    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0xa0, 0xa0, 0xa0, 0xff);
    SDL_RenderClear(renderer);

    /* Mark screen position for pen */
    if (last_is_eraser) {
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
                           (last_button & 0x01)? 0x255 : 0,
                           (last_button & 0x02)? 0x255 : 0,
                           (last_button & 0x04)? 0x255 : 0,
                           (int) (0xff * last_pressure));

    xdelta = -last_xtilt * 100;
    ydelta = -last_ytilt * 100;
    endx = last_x + xdelta;
    endy = last_y + ydelta;
    SDL_RenderDrawLineF(renderer, last_x, last_y, endx, endy);

    SDL_SetRenderDrawColor(renderer,
                           (last_button & 0x01)? 0x255 : 0,
                           (last_button & 0x02)? 0x255 : 0,
                           (last_button & 0x04)? 0x255 : 0,
                           (int) (0xff * last_pressure * last_pressure));
    /* Cone base width based on pressure: */
    SDL_RenderDrawLineF(renderer, last_x, last_y, endx + (ydelta * last_pressure / 3.0), endy - (xdelta * last_pressure / 3.0));
    SDL_RenderDrawLineF(renderer, last_x, last_y, endx - (ydelta * last_pressure / 3.0), endy + (xdelta * last_pressure / 3.0));

    SDL_RenderPresent(renderer);
}

static void
loop(void)
{
    SDL_Event event;
    int i;

    while (SDL_PollEvent(&event)) {
        SDLTest_CommonEvent(state, &event, &quitting);

        switch (event.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.motion.which != SDL_PEN_MOUSEID) {
                SDL_ShowCursor(SDL_ENABLE);
            }
            break;

        case SDL_PENMOTION: {
            SDL_PenMotionEvent *ev = &event.pmotion;

            SDL_ShowCursor(SDL_DISABLE);
            last_x = ev->x;
            last_y = ev->y;
            last_xtilt = ev->xtilt;
            last_ytilt = ev->ytilt;
            last_pressure = ev->pressure;
            last_is_eraser = ev->eraser;
#if VERBOSE
            SDL_Log("pen motion: %s %u at %f,%f; pressure=%.3f, tilt=%.3f/%.3f [buttons=%02x]\n",
                    ev->eraser ? "eraser" : "pen",
                    (unsigned int) ev->which, ev->x, ev->y, ev->pressure, ev->xtilt, ev->ytilt,
                    ev->state);
#endif
            break;
        }
        case SDL_PENBUTTONUP:
        case SDL_PENBUTTONDOWN: {
            SDL_PenButtonEvent *ev = &event.pbutton;

            SDL_ShowCursor(SDL_DISABLE);
            last_x = ev->x;
            last_y = ev->y;
            last_xtilt = ev->xtilt;
            last_ytilt = ev->ytilt;
            last_pressure = ev->pressure;
            last_button = ev->state == SDL_PRESSED ? ev->button : 0;
            last_is_eraser = ev->eraser;
#if VERBOSE
            SDL_Log("pen button: %s %u at %f,%f; BUTTON %d reported %s with event %s [pressure=%.3f, tilt=%.3f/%.3f]\n",
                    ev->eraser ? "eraser" : "pen",
                    (unsigned int) ev->which, ev->x, ev->y,
                    ev->button,
                    (ev->state == SDL_PRESSED) ? "PRESSED"
                    : ((ev->state == SDL_RELEASED) ? "RELEASED" : "--invalid--"),
                    event.type == SDL_PENBUTTONUP ? "PENBUTTONUP" : "PENBUTTONDOWN",
                    ev->pressure, ev->xtilt, ev->ytilt);
#endif
            break;
        }
        default:
            break;
        }
    }

    for (i = 0; i < state->num_windows; ++i) {
        if (state->renderers[i]) {
            DrawScreen(state->renderers[i]);
        }
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
