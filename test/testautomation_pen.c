/**
 * Pen test suite
 */

#include <stdio.h>
#include <limits.h>

#include "SDL.h"
#include "SDL_test.h"

#define SDL_internal_h_ /* Inhibit dynamic symbol redefinitions */
#include "../src/events/SDL_pen_c.h"

/* ================= Test Case Implementation ================== */

#define PEN_NUM_TEST_IDS 8

/* Helper functions */

/* Iterate over all pens to find index for pen ID, otherwise -1 */
static int
_pen_iterationFindsPenIDAt(SDL_PenID needle)
{
    int i;
    for (i = 0; i < SDL_NumPens(); ++i) {
	if ((SDL_GetPenIDForIndex(i)).id == needle.id) {
	    return i;
	}
    }
    return -1;
}

/* ---------------------------------------- */
/* Test device deallocation */

typedef struct {  /* Collection of pen (de)allocation information  */
    unsigned int deallocated_id_flags; /* ith bits set to 1 if the ith test_id is deallocated */
    unsigned int deallocated_deviceinfo_flags; /* ith bits set to 1 if deviceinfo as *int with value i was deallocated */
    SDL_PenID ids[PEN_NUM_TEST_IDS];
    SDL_PenGUID guids[PEN_NUM_TEST_IDS];
    int num_ids;
} pen_testdata;

/* SDL_PenGCSweep(): callback for tracking pen deallocation */
static void
_pen_testdata_callback(Uint32 deviceid, void *deviceinfo, void *tracker_ref)
{
    pen_testdata *tracker = (pen_testdata *) tracker;
    int offset = -1;
    int i;

    for (i = 0; i < tracker->num_ids; ++i) {
	if (deviceid == tracker->ids[i].id) {
	    tracker->deallocated_id_flags |= (1 << i);
	}
    }

    SDLTest_AssertCheck(deviceinfo != NULL, "Device %d did not have deviceinfo", deviceid);
    offset = *((int*)deviceinfo);
    SDLTest_AssertCheck(offset >= 0 && offset <= 31, "Device %d had bad deviceinfo %d", deviceid, offset);
    tracker->deallocated_deviceinfo_flags |= 1 << offset;
    SDL_free(deviceinfo);
}

/* GC Sweep tracking: update "tracker->deallocated_id_flags" and "tracker->deallocated_deviceinfo_flags" to record deallocations */
static void
_pen_trackGCSweep(pen_testdata *tracker)
{
    tracker->deallocated_id_flags = 0;
    tracker->deallocated_deviceinfo_flags = 0;
    SDL_PenGCSweep(tracker, _pen_testdata_callback);
}

/* Finds a number of unused pen IDs (does not allocate them).  Also initialises GUIDs. */
static void
_pen_unusedIDs(pen_testdata *tracker, int count)
{
    Uint32 synthetic_penid = 1000u;
    int index = 0;

    tracker->num_ids = count;
    SDLTest_AssertCheck(count < PEN_NUM_TEST_IDS, "Test setup bug: Invalid number of test IDs requested: %d", (int) count);

    while (count--) {
	int k;

	while (SDL_GetPen(synthetic_penid)) {
	    ++synthetic_penid;
	}
	tracker->ids[index].id = synthetic_penid;
	for (k = 0; k < 16; ++k) {
	    tracker->guids[index].data[k] = (16 * k) + index;
	}

	++synthetic_penid;
	++index;
    }
}

/* Allocate deviceinfo for pen */
static void
_pen_setDeviceinfo(SDL_Pen *pen, int deviceinfo)
{
    int *data = (int *) SDL_malloc(sizeof(int));
    *data = deviceinfo;
    SDLTest_AssertCheck(pen->deviceinfo == NULL, "pen->deviceinfo was not NULL for %p (%d) when requesting deviceinfo %d",
			pen, pen->id.id, deviceinfo);
    pen->deviceinfo = data;
}

/* ---------------------------------------- */
/* Back up and restore device information */

typedef struct deviceinfo_backup {
    Uint32 deviceid;
    void *deviceinfo;
    struct deviceinfo_backup *next;
} deviceinfo_backup;

/* SDL_PenGCSweep(): Helper callback for collecting all deviceinfo records */
static void
_pen_accumulate_gc_sweep(Uint32 deviceid, void* deviceinfo, void *backup_ref)
{
    deviceinfo_backup **db_ref = (deviceinfo_backup **) backup_ref;
    deviceinfo_backup *next = *db_ref;

    *db_ref = SDL_calloc(sizeof(deviceinfo_backup), 1);
    (*db_ref)->deviceid = deviceid;
    (*db_ref)->deviceinfo = deviceinfo;
    (*db_ref)->next = next;
}

/* SDL_PenGCSweep(): Helper callback that must never be called  */
static void
_pen_assert_impossible(Uint32 deviceid, void* deviceinfo, void *backup_ref)
{
    SDLTest_AssertCheck(0, "Deallocation during enableAndRestore: not expected");
}

/* Disable all pens and store their status */
static deviceinfo_backup *
_pen_disableAndBackup(void)
{
    deviceinfo_backup *backup = NULL;

    SDL_PenGCMark();
    SDL_PenGCSweep(&backup, _pen_accumulate_gc_sweep);
    return backup;
}

/* Restore all pens to their previous status */
static void
_pen_enableAndRestore(deviceinfo_backup *backup)
{
    SDL_PenGCMark();
    while (backup) {
	SDL_Pen *disabledpen = SDL_GetPen(backup->deviceid);

	SDL_PenRegister(disabledpen->id, disabledpen->guid, disabledpen->name, disabledpen->flags);
	disabledpen->deviceinfo = backup->deviceinfo;

	deviceinfo_backup *next = backup->next;
	SDL_free(backup);
	backup = next;
    }

    SDL_PenGCSweep(NULL, _pen_assert_impossible);
}

/* ---------------------------------------- */
/* Test case functions */

/**
 * @brief Check basic pen device introduction and iteration
 *
 * @sa SDL_NumPens, SDL_PenIDForIndex, SDL_PenName, SDL_PenCapabilities
 */
static int
pen_iteration(void *arg)
{
    pen_testdata ptest;
    int initial_pen_count;
    int i;
    deviceinfo_backup *backup;
    char long_pen_name[SDL_PEN_MAX_NAME + 10];
    const char *name;

    /* Check initial pens */
    SDL_PumpEvents();
    initial_pen_count = SDL_NumPens();
    SDLTest_AssertPass("SDL_NumPens() = %d", initial_pen_count);

    /* Grab unused pen IDs for testing */
    _pen_unusedIDs(&ptest, 3);
    for (i = 0; i < 3; ++i) {
	int index = _pen_iterationFindsPenIDAt(ptest.ids[i]);
	SDLTest_AssertCheck(-1 == index,
			   "Unexpectedly registered PenID(%d) found at index %d", ptest.ids[i].id, index);
    }

    /* Remove existing pens, but back up */
    backup = _pen_disableAndBackup();
    SDLTest_AssertCheck(0 == SDL_NumPens(),
			"Expected SDL_NumPens() == 0 after disabling and backing up all");
    SDLTest_AssertPass("Removed existing pens");

    /* Re-run GC, track deallocations */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    SDLTest_AssertCheck(0 == SDL_NumPens(),
			"Expected SDL_NumPens() == 0 after second GC pass");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0, "Unexpected device deallocation");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0, "Unexpected deviceinfo deallocation");
    SDLTest_AssertPass("Validated that GC on empty pen set is idempotent");

    /* Add three pens, validate */
    SDL_PenGCMark();

    SDL_memset(long_pen_name, 'x', sizeof(long_pen_name));     /* Include pen name that is too long */
    long_pen_name[sizeof(long_pen_name) - 1] = 0;

    _pen_setDeviceinfo(SDL_PenRegister(ptest.ids[0], ptest.guids[0], "pen 0",
				       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
		       16);
    _pen_setDeviceinfo(SDL_PenRegister(ptest.ids[2], ptest.guids[2], long_pen_name,
				       SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK),
		       20);
    _pen_setDeviceinfo(SDL_PenRegister(ptest.ids[1], ptest.guids[1], "pen 1",
				       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT_MASK),
		       24);
    _pen_trackGCSweep(&ptest);

    SDLTest_AssertCheck(SDL_NumPens() == 0,
			"Expected SDL_NumPens() == 0 after second GC pass");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0, "Unexpected device deallocation");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0, "Unexpected deviceinfo deallocation");

    SDLTest_AssertCheck(3 == SDL_NumPens(),
			"Expected SDL_NumPens() == 3 after allocation");

    for (i = 0; i < 3; ++i) {
	/* Check that all pens are accounted for */
	int index = _pen_iterationFindsPenIDAt(ptest.ids[i]);
	SDLTest_AssertCheck(-1 != index,  "Could not find PenID(%d)", ptest.ids[i].id);
    }
    SDLTest_AssertPass("Validated that all three pens are indexable");

    /* Check pen properties */
    SDLTest_AssertCheck(0 == SDL_strcmp("pen 0", SDL_PenName(ptest.ids[0])),
			"Pen #0 name");
    SDLTest_AssertCheck((SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK) == SDL_PenCapabilities(ptest.ids[0]),
			"Pen #0 capabilities");

    SDLTest_AssertCheck(0 == SDL_strcmp("pen 1", SDL_PenName(ptest.ids[1])),
			"Pen #1 name");
    SDLTest_AssertCheck((SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT_MASK) == SDL_PenCapabilities(ptest.ids[1]),
			"Pen #1 capabilities");

    name = SDL_PenName(ptest.ids[2]);
    SDLTest_AssertCheck(SDL_PEN_MAX_NAME - 1 == SDL_strlen(name),
			"Pen #2 name length");
    SDLTest_AssertCheck(0 == SDL_memcmp(name, long_pen_name, SDL_PEN_MAX_NAME - 1),
			"Pen #2 name contents");
    SDLTest_AssertCheck((SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK) == SDL_PenCapabilities(ptest.ids[2]),
			"Pen #2 capabilities");
    SDLTest_AssertPass("Pen registration and basic queries");

    /* Re-run GC, track deallocations */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    SDLTest_AssertCheck(0 == SDL_NumPens(),
			"Expected SDL_NumPens() == 0 after third GC pass");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x07,
			"Unexpected device deallocation : %08x", ptest.deallocated_id_flags);
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x01110000,
			"Unexpected deviceinfo deallocation : %08x ", ptest.deallocated_deviceinfo_flags);
    SDLTest_AssertPass("Validated that GC on empty pen set is idempotent");

    /* Restore previously existing pens */
    _pen_enableAndRestore(backup);
    SDLTest_AssertPass("Restored pens to pre-test state");

    /* Check that we restored the right number of pens */
    SDLTest_AssertCheck(SDL_NumPens() == initial_pen_count,
			"SDL_NumPens() == %d (initail_pen_count)", initial_pen_count);

    return TEST_COMPLETED;
}

/**
 * @brief Check basic pen device queries
 *
 * @sa SDL_PenName, SDL_PenCapabilities
 */
static int
pen_queries(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device hotplugging
 *
 * @sa SDL_NumPens, SDL_PenIDForIndex, SDL_PenName, SDL_PenCapabilities, SDL_PenConnected
 */
static int
pen_hotplugging(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device GUID handling
 *
 * @sa SDL_PenGUIDForPenID, SDL_PenGUIDCompare, SDL_PenStringForGUID, SDL_PenGUIDForString
 */
static int
pen_GUIDs(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device button reporting
 *
 */
static int
pen_buttonReporting(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device movement reporting
 *
 */
static int
pen_movement(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device axis updates
 *
 */
static int
pen_axes(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device mouse emulation and event suppression
 *
 */
static int
pen_mouseEmulation(void *arg)
{
    return TEST_COMPLETED;
}

/**
 * @brief Check pen device status tracking
 *
 * @sa SDL_PenStatus
 */
static int
pen_status(void *arg)
{
    return TEST_COMPLETED;
}

#if 0
void f()
{
   int x;
   int y;
   Uint32 state;

   /* Pump some events to update mouse state */
   SDL_PumpEvents();
   SDLTest_AssertPass("Call to SDL_PumpEvents()");

   /* Case where x, y pointer is NULL */
   state = SDL_GetMouseState(NULL, NULL);
   SDLTest_AssertPass("Call to SDL_GetMouseState(NULL, NULL)");
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   /* Case where x pointer is not NULL */
   x = INT_MIN;
   state = SDL_GetMouseState(&x, NULL);
   SDLTest_AssertPass("Call to SDL_GetMouseState(&x, NULL)");
   SDLTest_AssertCheck(x > INT_MIN, "Validate that value of x is > INT_MIN, got: %i", x);
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   /* Case where y pointer is not NULL */
   y = INT_MIN;
   state = SDL_GetMouseState(NULL, &y);
   SDLTest_AssertPass("Call to SDL_GetMouseState(NULL, &y)");
   SDLTest_AssertCheck(y > INT_MIN, "Validate that value of y is > INT_MIN, got: %i", y);
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   /* Case where x and y pointer is not NULL */
   x = INT_MIN;
   y = INT_MIN;
   state = SDL_GetMouseState(&x, &y);
   SDLTest_AssertPass("Call to SDL_GetMouseState(&x, &y)");
   SDLTest_AssertCheck(x > INT_MIN, "Validate that value of x is > INT_MIN, got: %i", x);
   SDLTest_AssertCheck(y > INT_MIN, "Validate that value of y is > INT_MIN, got: %i", y);
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   return TEST_COMPLETED;
}

/**
 * @brief Check call to SDL_GetRelativeMouseState
 *
 */
static int
pen_getRelativeMouseState(void *arg)
{
   int x;
   int y;
   Uint32 state;

   /* Pump some events to update mouse state */
   SDL_PumpEvents();
   SDLTest_AssertPass("Call to SDL_PumpEvents()");

   /* Case where x, y pointer is NULL */
   state = SDL_GetRelativeMouseState(NULL, NULL);
   SDLTest_AssertPass("Call to SDL_GetRelativeMouseState(NULL, NULL)");
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   /* Case where x pointer is not NULL */
   x = INT_MIN;
   state = SDL_GetRelativeMouseState(&x, NULL);
   SDLTest_AssertPass("Call to SDL_GetRelativeMouseState(&x, NULL)");
   SDLTest_AssertCheck(x > INT_MIN, "Validate that value of x is > INT_MIN, got: %i", x);
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   /* Case where y pointer is not NULL */
   y = INT_MIN;
   state = SDL_GetRelativeMouseState(NULL, &y);
   SDLTest_AssertPass("Call to SDL_GetRelativeMouseState(NULL, &y)");
   SDLTest_AssertCheck(y > INT_MIN, "Validate that value of y is > INT_MIN, got: %i", y);
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   /* Case where x and y pointer is not NULL */
   x = INT_MIN;
   y = INT_MIN;
   state = SDL_GetRelativeMouseState(&x, &y);
   SDLTest_AssertPass("Call to SDL_GetRelativeMouseState(&x, &y)");
   SDLTest_AssertCheck(x > INT_MIN, "Validate that value of x is > INT_MIN, got: %i", x);
   SDLTest_AssertCheck(y > INT_MIN, "Validate that value of y is > INT_MIN, got: %i", y);
   SDLTest_AssertCheck(_mouseStateCheck(state), "Validate state returned from function, got: %i", state);

   return TEST_COMPLETED;
}


/* XPM definition of mouse Cursor */
static const char *_mouseArrowData[] = {
  /* pixels */
  "X                               ",
  "XX                              ",
  "X.X                             ",
  "X..X                            ",
  "X...X                           ",
  "X....X                          ",
  "X.....X                         ",
  "X......X                        ",
  "X.......X                       ",
  "X........X                      ",
  "X.....XXXXX                     ",
  "X..X..X                         ",
  "X.X X..X                        ",
  "XX  X..X                        ",
  "X    X..X                       ",
  "     X..X                       ",
  "      X..X                      ",
  "      X..X                      ",
  "       XX                       ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                "
};

/* Helper that creates a new mouse cursor from an XPM */
static SDL_Cursor *_initArrowCursor(const char *image[])
{
  SDL_Cursor *cursor;
  int i, row, col;
  Uint8 data[4*32];
  Uint8 mask[4*32];

  i = -1;
  for ( row=0; row<32; ++row ) {
    for ( col=0; col<32; ++col ) {
      if ( col % 8 ) {
        data[i] <<= 1;
        mask[i] <<= 1;
      } else {
        ++i;
        data[i] = mask[i] = 0;
      }
      switch (image[row][col]) {
        case 'X':
          data[i] |= 0x01;
          mask[i] |= 0x01;
          break;
        case '.':
          mask[i] |= 0x01;
          break;
        case ' ':
          break;
      }
    }
  }

  cursor = SDL_CreateCursor(data, mask, 32, 32, 0, 0);
  return cursor;
}

/**
 * @brief Check call to SDL_CreateCursor and SDL_FreeCursor
 *
 * @sa http://wiki.libsdl.org/SDL_CreateCursor
 * @sa http://wiki.libsdl.org/SDL_FreeCursor
 */
static int
pen_createFreeCursor(void *arg)
{
    SDL_Cursor *cursor;

    /* Create a cursor */
    cursor = _initArrowCursor(_mouseArrowData);
        SDLTest_AssertPass("Call to SDL_CreateCursor()");
        SDLTest_AssertCheck(cursor != NULL, "Validate result from SDL_CreateCursor() is not NULL");
    if (cursor == NULL) {
        return TEST_ABORTED;
    }

    /* Free cursor again */
    SDL_FreeCursor(cursor);
    SDLTest_AssertPass("Call to SDL_FreeCursor()");

    return TEST_COMPLETED;
}

/**
 * @brief Check call to SDL_CreateColorCursor and SDL_FreeCursor
 *
 * @sa http://wiki.libsdl.org/SDL_CreateColorCursor
 * @sa http://wiki.libsdl.org/SDL_FreeCursor
 */
static int
pen_createFreeColorCursor(void *arg)
{
    SDL_Surface *face;
    SDL_Cursor *cursor;

    /* Get sample surface */
    face = SDLTest_ImageFace();
    SDLTest_AssertCheck(face != NULL, "Validate sample input image is not NULL");
    if (face == NULL) return TEST_ABORTED;

    /* Create a color cursor from surface */
    cursor = SDL_CreateColorCursor(face, 0, 0);
        SDLTest_AssertPass("Call to SDL_CreateColorCursor()");
        SDLTest_AssertCheck(cursor != NULL, "Validate result from SDL_CreateColorCursor() is not NULL");
    if (cursor == NULL) {
        SDL_FreeSurface(face);
        return TEST_ABORTED;
    }

    /* Free cursor again */
    SDL_FreeCursor(cursor);
    SDLTest_AssertPass("Call to SDL_FreeCursor()");

    /* Clean up */
    SDL_FreeSurface(face);

    return TEST_COMPLETED;
}

/* Helper that changes cursor visibility */
static void
_changeCursorVisibility(int state)
{
    int oldState;
    int newState;
    int result;

        oldState = SDL_ShowCursor(SDL_QUERY);
    SDLTest_AssertPass("Call to SDL_ShowCursor(SDL_QUERY)");

        result = SDL_ShowCursor(state);
    SDLTest_AssertPass("Call to SDL_ShowCursor(%s)", (state == SDL_ENABLE) ? "SDL_ENABLE" : "SDL_DISABLE");
    SDLTest_AssertCheck(result == oldState, "Validate result from SDL_ShowCursor(%s), expected: %i, got: %i",
        (state == SDL_ENABLE) ? "SDL_ENABLE" : "SDL_DISABLE", oldState, result);

    newState = SDL_ShowCursor(SDL_QUERY);
    SDLTest_AssertPass("Call to SDL_ShowCursor(SDL_QUERY)");
    SDLTest_AssertCheck(state == newState, "Validate new state, expected: %i, got: %i",
        state, newState);
}

/**
 * @brief Check call to SDL_ShowCursor
 *
 * @sa http://wiki.libsdl.org/SDL_ShowCursor
 */
static int
pen_showCursor(void *arg)
{
    int currentState;

    /* Get current state */
    currentState = SDL_ShowCursor(SDL_QUERY);
    SDLTest_AssertPass("Call to SDL_ShowCursor(SDL_QUERY)");
    SDLTest_AssertCheck(currentState == SDL_DISABLE || currentState == SDL_ENABLE,
        "Validate result is %i or %i, got: %i", SDL_DISABLE, SDL_ENABLE, currentState);
    if (currentState == SDL_DISABLE) {
        /* Show the cursor, then hide it again */
        _changeCursorVisibility(SDL_ENABLE);
        _changeCursorVisibility(SDL_DISABLE);
    } else if (currentState == SDL_ENABLE) {
        /* Hide the cursor, then show it again */
        _changeCursorVisibility(SDL_DISABLE);
        _changeCursorVisibility(SDL_ENABLE);
    } else {
        return TEST_ABORTED;
    }

    return TEST_COMPLETED;
}

/**
 * @brief Check call to SDL_SetCursor
 *
 * @sa http://wiki.libsdl.org/SDL_SetCursor
 */
static int
pen_setCursor(void *arg)
{
    SDL_Cursor *cursor;

    /* Create a cursor */
    cursor = _initArrowCursor(_mouseArrowData);
        SDLTest_AssertPass("Call to SDL_CreateCursor()");
        SDLTest_AssertCheck(cursor != NULL, "Validate result from SDL_CreateCursor() is not NULL");
    if (cursor == NULL) {
        return TEST_ABORTED;
    }

    /* Set the arrow cursor */
    SDL_SetCursor(cursor);
    SDLTest_AssertPass("Call to SDL_SetCursor(cursor)");

    /* Force redraw */
    SDL_SetCursor(NULL);
    SDLTest_AssertPass("Call to SDL_SetCursor(NULL)");

    /* Free cursor again */
    SDL_FreeCursor(cursor);
    SDLTest_AssertPass("Call to SDL_FreeCursor()");

    return TEST_COMPLETED;
}

/**
 * @brief Check call to SDL_GetCursor
 *
 * @sa http://wiki.libsdl.org/SDL_GetCursor
 */
static int
pen_getCursor(void *arg)
{
    SDL_Cursor *cursor;

    /* Get current cursor */
    cursor = SDL_GetCursor();
        SDLTest_AssertPass("Call to SDL_GetCursor()");
        SDLTest_AssertCheck(cursor != NULL, "Validate result from SDL_GetCursor() is not NULL");

    return TEST_COMPLETED;
}

/**
 * @brief Check call to SDL_GetRelativeMouseMode and SDL_SetRelativeMouseMode
 *
 * @sa http://wiki.libsdl.org/SDL_GetRelativeMouseMode
 * @sa http://wiki.libsdl.org/SDL_SetRelativeMouseMode
 */
static int
pen_getSetRelativeMouseMode(void *arg)
{
    int result;
        int i;
    SDL_bool initialState;
    SDL_bool currentState;

    /* Capture original state so we can revert back to it later */
    initialState = SDL_GetRelativeMouseMode();
        SDLTest_AssertPass("Call to SDL_GetRelativeMouseMode()");

        /* Repeat twice to check D->D transition */
        for (i=0; i<2; i++) {
      /* Disable - should always be supported */
          result = SDL_SetRelativeMouseMode(SDL_FALSE);
          SDLTest_AssertPass("Call to SDL_SetRelativeMouseMode(FALSE)");
          SDLTest_AssertCheck(result == 0, "Validate result value from SDL_SetRelativeMouseMode, expected: 0, got: %i", result);
      currentState = SDL_GetRelativeMouseMode();
          SDLTest_AssertPass("Call to SDL_GetRelativeMouseMode()");
          SDLTest_AssertCheck(currentState == SDL_FALSE, "Validate current state is FALSE, got: %i", currentState);
        }

        /* Repeat twice to check D->E->E transition */
        for (i=0; i<2; i++) {
      /* Enable - may not be supported */
          result = SDL_SetRelativeMouseMode(SDL_TRUE);
          SDLTest_AssertPass("Call to SDL_SetRelativeMouseMode(TRUE)");
          if (result != -1) {
            SDLTest_AssertCheck(result == 0, "Validate result value from SDL_SetRelativeMouseMode, expected: 0, got: %i", result);
        currentState = SDL_GetRelativeMouseMode();
            SDLTest_AssertPass("Call to SDL_GetRelativeMouseMode()");
            SDLTest_AssertCheck(currentState == SDL_TRUE, "Validate current state is TRUE, got: %i", currentState);
          }
        }

    /* Disable to check E->D transition */
        result = SDL_SetRelativeMouseMode(SDL_FALSE);
        SDLTest_AssertPass("Call to SDL_SetRelativeMouseMode(FALSE)");
        SDLTest_AssertCheck(result == 0, "Validate result value from SDL_SetRelativeMouseMode, expected: 0, got: %i", result);
    currentState = SDL_GetRelativeMouseMode();
        SDLTest_AssertPass("Call to SDL_GetRelativeMouseMode()");
        SDLTest_AssertCheck(currentState == SDL_FALSE, "Validate current state is FALSE, got: %i", currentState);

        /* Revert to original state - ignore result */
        result = SDL_SetRelativeMouseMode(initialState);

    return TEST_COMPLETED;
}

#define PEN_TESTWINDOW_WIDTH  320
#define PEN_TESTWINDOW_HEIGHT 200

/**
 * Creates a test window
 */
static SDL_Window
*_createMouseSuiteTestWindow()
{
  int posX = 100, posY = 100, width = PEN_TESTWINDOW_WIDTH, height = PEN_TESTWINDOW_HEIGHT;
  SDL_Window *window;
  window = SDL_CreateWindow("pen_createMouseSuiteTestWindow", posX, posY, width, height, 0);
  SDLTest_AssertPass("SDL_CreateWindow()");
  SDLTest_AssertCheck(window != NULL, "Check SDL_CreateWindow result");
  return window;
}

/*
 * Destroy test window
 */
static void
_destroyMouseSuiteTestWindow(SDL_Window *window)
{
  if (window != NULL) {
     SDL_DestroyWindow(window);
     window = NULL;
     SDLTest_AssertPass("SDL_DestroyWindow()");
  }
}

/**
 * @brief Check call to SDL_WarpMouseInWindow
 *
 * @sa http://wiki.libsdl.org/SDL_WarpMouseInWindow
 */
static int
pen_warpMouseInWindow(void *arg)
{
    const int w = PEN_TESTWINDOW_WIDTH, h = PEN_TESTWINDOW_HEIGHT;
    int numPositions = 6;
    int xPositions[6];
    int yPositions[6];
    int x, y, i, j;
    SDL_Window *window;

    xPositions[0] = -1;
    xPositions[1] = 0;
    xPositions[2] = 1;
    xPositions[3] = w-1;
    xPositions[4] = w;
    xPositions[5] = w+1;
    yPositions[0] = -1;
    yPositions[1] = 0;
    yPositions[2] = 1;
    yPositions[3] = h-1;
    yPositions[4] = h;
    yPositions[5] = h+1;
    /* Create test window */
    window = _createMouseSuiteTestWindow();
    if (window == NULL) return TEST_ABORTED;

    /* Mouse to random position inside window */
    x = SDLTest_RandomIntegerInRange(1, w-1);
    y = SDLTest_RandomIntegerInRange(1, h-1);
    SDL_WarpMouseInWindow(window, x, y);
    SDLTest_AssertPass("SDL_WarpMouseInWindow(...,%i,%i)", x, y);

        /* Same position again */
    SDL_WarpMouseInWindow(window, x, y);
    SDLTest_AssertPass("SDL_WarpMouseInWindow(...,%i,%i)", x, y);

    /* Mouse to various boundary positions */
    for (i=0; i<numPositions; i++) {
      for (j=0; j<numPositions; j++) {
        x = xPositions[i];
        y = yPositions[j];
        SDL_WarpMouseInWindow(window, x, y);
        SDLTest_AssertPass("SDL_WarpMouseInWindow(...,%i,%i)", x, y);

        /* TODO: add tracking of events and check that each call generates a mouse motion event */
        SDL_PumpEvents();
        SDLTest_AssertPass("SDL_PumpEvents()");
      }
    }


        /* Clean up test window */
    _destroyMouseSuiteTestWindow(window);

    return TEST_COMPLETED;
}

/**
 * @brief Check call to SDL_GetMouseFocus
 *
 * @sa http://wiki.libsdl.org/SDL_GetMouseFocus
 */
static int
pen_getMouseFocus(void *arg)
{
    const int w = PEN_TESTWINDOW_WIDTH, h = PEN_TESTWINDOW_HEIGHT;
    int x, y;
    SDL_Window *window;
    SDL_Window *focusWindow;

    /* Get focus - focus non-deterministic */
    focusWindow = SDL_GetMouseFocus();
    SDLTest_AssertPass("SDL_GetMouseFocus()");

        /* Create test window */
    window = _createMouseSuiteTestWindow();
    if (window == NULL) return TEST_ABORTED;

    /* Mouse to random position inside window */
    x = SDLTest_RandomIntegerInRange(1, w-1);
    y = SDLTest_RandomIntegerInRange(1, h-1);
    SDL_WarpMouseInWindow(window, x, y);
    SDLTest_AssertPass("SDL_WarpMouseInWindow(...,%i,%i)", x, y);

    /* Pump events to update focus state */
    SDL_Delay(100);
    SDL_PumpEvents();
    SDLTest_AssertPass("SDL_PumpEvents()");

        /* Get focus with explicit window setup - focus deterministic */
    focusWindow = SDL_GetMouseFocus();
    SDLTest_AssertPass("SDL_GetMouseFocus()");
    SDLTest_AssertCheck (focusWindow != NULL, "Check returned window value is not NULL");
    SDLTest_AssertCheck (focusWindow == window, "Check returned window value is test window");

    /* Mouse to random position outside window */
    x = SDLTest_RandomIntegerInRange(-9, -1);
    y = SDLTest_RandomIntegerInRange(-9, -1);
    SDL_WarpMouseInWindow(window, x, y);
    SDLTest_AssertPass("SDL_WarpMouseInWindow(...,%i,%i)", x, y);

        /* Clean up test window */
    _destroyMouseSuiteTestWindow(window);

    /* Pump events to update focus state */
    SDL_PumpEvents();
    SDLTest_AssertPass("SDL_PumpEvents()");

        /* Get focus for non-existing window */
    focusWindow = SDL_GetMouseFocus();
    SDLTest_AssertPass("SDL_GetMouseFocus()");
    SDLTest_AssertCheck (focusWindow == NULL, "Check returned window value is NULL");


    return TEST_COMPLETED;
}
#endif

/* ================= Test References ================== */

/* Mouse test cases */
static const SDLTest_TestCaseReference penTest1 =
        { (SDLTest_TestCaseFp)pen_iteration, "pen_iteration", "Iterate over all pens with SDL_PenIDForIndex", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest2 =
        { (SDLTest_TestCaseFp)pen_queries, "pen_queries", "Query pens with SDL_PenName and SDL_PenCapabilities", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest3 =
        { (SDLTest_TestCaseFp)pen_hotplugging, "pen_hotplugging", "Hotplug pens and validate their status, including SDL_PenConnected", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest4 =
        { (SDLTest_TestCaseFp)pen_GUIDs, "pen_GUIDs", "Check SDL_PenGUID operations", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest5 =
        { (SDLTest_TestCaseFp)pen_buttonReporting, "pen_buttonReporting", "Check pen button presses", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest6 =
        { (SDLTest_TestCaseFp)pen_movement, "pen_movement", "Check pen movement reporting", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest7 =
        { (SDLTest_TestCaseFp)pen_axes, "pen_axes", "Check pen axis updates", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest8 =
        { (SDLTest_TestCaseFp)pen_mouseEmulation, "pen_mouseEmulation", "Check pen-as-mouse event forwarding", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest9 =
        { (SDLTest_TestCaseFp)pen_status, "pen_status", "Check pen status tracking and updating via SDL_PenStatus", TEST_ENABLED };

/* Sequence of Mouse test cases */
static const SDLTest_TestCaseReference *penTests[] =  {
    &penTest1, &penTest2, &penTest3, &penTest4, &penTest5, &penTest6,
    &penTest7, &penTest8, &penTest9, NULL
};

/* Mouse test suite (global) */
SDLTest_TestSuiteReference penTestSuite = {
    "Pen",
    NULL,
    penTests,
    NULL
};
