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

#include "SDL_windowspen.h"
#include "../../core/windows/SDL_hid.h"

#define WIN_PEN_MESSAGE_WNDCLASS L"SDL_PenMessageClass"

static HWND penChangeWindow;

void
WIN_FreePenDeviceInfo(Uint32 penid_id, void *deviceinfo, void *context)
{
    SDL_free(deviceinfo);
}

void
WIN_PenCalcRectData(SDL_Pen* pen, HANDLE device)
{
    RECT displayRect, deviceRect;
    INT32 displayWidth, displayHeight, deviceWidth, deviceHeight;
    float scaleX, scaleY, offsetX, offsetY;

    WIN_PenDriverInfo *data = pen->deviceinfo;
    if (data == NULL) {
        data = SDL_malloc(sizeof(WIN_PenDriverInfo));
        pen->deviceinfo = data;
    }

    GetPointerDeviceRects(device, &deviceRect, &displayRect);

    /*
    SDL_Log("device: L: %ld, T: %ld, R: %ld, B: %ld", deviceRect.left, deviceRect.top, deviceRect.right, deviceRect.bottom);
    SDL_Log("display: L: %ld, T: %ld, R: %ld, B: %ld", displayRect.left, displayRect.top, displayRect.right, displayRect.bottom);
    */

    displayWidth = displayRect.right - displayRect.left;
    displayHeight = displayRect.bottom - displayRect.top;

    deviceWidth = deviceRect.right - deviceRect.left;
    deviceHeight = deviceRect.bottom - deviceRect.top;

    scaleX = displayWidth / (float) deviceWidth;
    scaleY = displayHeight / (float) deviceHeight;

    offsetX = displayRect.left - deviceRect.left / scaleX;
    offsetY = displayRect.top - deviceRect.top / scaleY;

    data->RectData.ScaleX = scaleX;
    data->RectData.ScaleY = scaleY;
    data->RectData.OffsetX = offsetX;
    data->RectData.OffsetY = offsetY;
}

void 
WIN_ReloadPens()
{
    UINT32 pointerDeviceCount = 0;
    POINTER_DEVICE_INFO *pointerDevices = NULL;
    UINT32 i;

    SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "WIN_ReloadPens\n");

    if (!GetPointerDevices(&pointerDeviceCount, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "GetPointerDevices failed");
        return;
    }

    SDL_PenGCMark();

    SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "WIN_ReloadPens count pointers: %u", pointerDeviceCount);

    pointerDevices = SDL_malloc(pointerDeviceCount * sizeof(POINTER_DEVICE_INFO));

    GetPointerDevices(&pointerDeviceCount, pointerDevices);

    for (i = 0; i < pointerDeviceCount; i++) {
        UINT32 c;
        POINTER_DEVICE_INFO *deviceInfo;
        POINTER_DEVICE_CURSOR_INFO *cursorInfos = NULL;
        POINTER_DEVICE_PROPERTY *deviceProperties = NULL;
        UINT32 cursorCount = 0;
        UINT32 propertiesCount = 0;
        char *name;

        deviceInfo = pointerDevices + i;

        if (deviceInfo->pointerDeviceType != POINTER_DEVICE_TYPE_INTEGRATED_PEN && deviceInfo->pointerDeviceType != POINTER_DEVICE_TYPE_EXTERNAL_PEN) {
            continue;
        }

        name = WIN_StringToUTF8W(deviceInfo->productString);

        SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "Pointer device: %s, type: %d, start ID: %lu", name, deviceInfo->pointerDeviceType, deviceInfo->startingCursorId);

        GetPointerDeviceCursors(deviceInfo->device, &cursorCount, NULL);

        SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "Cursor count: %u", cursorCount);

        cursorInfos = SDL_malloc(cursorCount * sizeof(POINTER_DEVICE_CURSOR_INFO));
        GetPointerDeviceCursors(deviceInfo->device, &cursorCount, cursorInfos);

        for (c = 0; c < cursorCount; c++) {
            SDL_GUID guid = { 0 };
            POINTER_DEVICE_CURSOR_INFO *cursor;
            SDL_PenID penId;
            SDL_Pen *pen;

            cursor = cursorInfos + c;
            SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "Cursor: %u, %d", cursor->cursorId, cursor->cursor);

            /* TODO: Can we get sane GUIDs here? Just mapping this as the HANDLE right now. */ 
            *((UINT32 *) &guid) = cursor->cursorId;

            penId = cursor->cursorId;
            pen = SDL_PenModifyBegin(penId);
            pen->guid = guid;
            pen->type = cursor->cursor == POINTER_DEVICE_CURSOR_TYPE_ERASER ? SDL_PEN_TYPE_ERASER : SDL_PEN_TYPE_PEN;
            pen->info.num_buttons = 2;
            pen->info.max_tilt = 1;
            SDL_strlcpy(pen->name, name, SDL_PEN_MAX_NAME);
            WIN_PenCalcRectData(pen, deviceInfo->device);

            /* 
            Windows Ink doesn't tell us ahead of time whether any of these are actually supported ahead of time, so we just have to report all I guess.
            Distance and slider do not appear to be reported by the Windows Ink API at all.
            */
            SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_ROTATION_MASK);
            SDL_PenModifyAddCapabilities(pen, cursor->cursor == POINTER_DEVICE_CURSOR_TYPE_ERASER ? SDL_PEN_ERASER_MASK : SDL_PEN_INK_MASK);

            SDL_PenModifyEnd(pen, SDL_TRUE);
        }

        GetPointerDeviceProperties(deviceInfo->device, &propertiesCount, NULL);

        SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "Properties count: %u", propertiesCount);

        deviceProperties = SDL_malloc(propertiesCount * sizeof(POINTER_DEVICE_PROPERTY));
        GetPointerDeviceProperties(deviceInfo->device, &propertiesCount, deviceProperties);

        for (c = 0; c < propertiesCount; c++) {
            POINTER_DEVICE_PROPERTY *prop = deviceProperties + c;

            SDL_LogVerbose(SDL_LOG_CATEGORY_VIDEO, "Property %hx/%hx: %u %u", prop->usagePageId, prop->usageId, prop->unit, prop->unitExponent);
        }

        SDL_free(name);
        SDL_free(cursorInfos);
    }

    SDL_PenGCSweep(NULL, &WIN_FreePenDeviceInfo);

    if (pointerDevices != NULL)
        SDL_free(pointerDevices);
}

LRESULT CALLBACK
WIN_PenMessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_POINTERDEVICECHANGE:
        {
            SDL_Log("WND: WM_POINTERDEVICECHANGE");
            /* 
            We could in theory do more granular updating based on the wParam/lParam, 
            But the message docs aren't the most clear thing in the world so I'll pass.
            */
            WIN_ReloadPens();
            return 0;
        }
        case WM_POINTERDEVICEINRANGE:
        {
            SDL_Log("WND: WM_POINTERDEVICEINRANGE");
            return 0;
        }
        case WM_POINTERDEVICEOUTOFRANGE:
        {
            SDL_Log("WND: WM_POINTERDEVICEOUTOFRANGE");
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void
WIN_InitPen(_THIS)
{
    if (!WIN_IsWindows8OrGreater()) {
        return;
    }

    /* Make a message-only window */
    WNDCLASSW windowClass;
    SDL_zero(windowClass);
    windowClass.lpfnWndProc = &WIN_PenMessageWindowProc;
    windowClass.lpszClassName = WIN_PEN_MESSAGE_WNDCLASS;
    windowClass.hInstance = GetModuleHandleW(NULL);

    RegisterClassW(&windowClass);

    penChangeWindow = CreateWindowW(windowClass.lpszClassName, L"SDL_PenMessageWindow", WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, 0, NULL, 0);

    RegisterPointerDeviceNotifications(penChangeWindow, FALSE);

    WIN_ReloadPens();
}

void
WIN_QuitPen(_THIS)
{
    if (!WIN_IsWindows8OrGreater()) {
        return;
    }

    if (penChangeWindow != NULL) {
        DestroyWindow(penChangeWindow);
        penChangeWindow = NULL;
    }

    UnregisterClassW(WIN_PEN_MESSAGE_WNDCLASS, GetModuleHandleW(NULL));
}