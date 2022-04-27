/**
 * Pen test suite
 */

#include <stdio.h>
#include <limits.h>

#include "SDL.h"
#include "SDL_test.h"

#define SDL_internal_h_ /* Inhibit dynamic symbol redefinitions */
#include "../src/events/SDL_pen_c.h"
#include "../src/events/SDL_pen.c"

/* ================= Internal SDL API Compatibility ================== */
/* Mock implementations of Pen -> Mouse calls */
/* Not thread-safe! */

/* "standard" pen registration process */
SDL_Pen *
_pen_register(SDL_PenID penid, SDL_PenGUID guid, char *name, Uint32 flags)
{
    SDL_Pen *pen = SDL_PenModifyBegin(penid.id);
    pen->guid = guid;
    SDL_strlcpy(pen->name, name, SDL_PEN_MAX_NAME);
    SDL_PenModifyAddCapabilities(pen, flags);
    return pen;
}

SDL_bool
SDL_IsMousePositionInWindow(SDL_Window * window, SDL_MouseID mouseID, int x, int y)
{
    return SDL_TRUE;
}

static int _mouseemu_last_event = 0;
static int _mouseemu_last_x = 0;
static int _mouseemu_last_y = 0;
static int _mouseemu_last_mouseid = 0;
static int _mouseemu_last_button = 0;
static int _mouseemu_last_relative = 0;

int
SDL_SendMouseButton(SDL_Window * window, SDL_MouseID mouseID, Uint8 state, Uint8 button)
{
    if (mouseID == SDL_PEN_MOUSEID) {
        _mouseemu_last_event = (state == SDL_PRESSED) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        _mouseemu_last_button = button;
        _mouseemu_last_mouseid = mouseID;
    }
    return 1;
}

int
SDL_SendMouseMotion(SDL_Window * window, SDL_MouseID mouseID, int relative, int x, int y)
{
    if (mouseID == SDL_PEN_MOUSEID) {
        _mouseemu_last_event = SDL_MOUSEMOTION;
        _mouseemu_last_x = x;
        _mouseemu_last_y = y;
        _mouseemu_last_mouseid = mouseID;
        _mouseemu_last_relative = relative;
    }
    return 1;
}


SDL_Mouse *
SDL_GetMouse(void)
{
    static SDL_Mouse dummy_mouse;

    dummy_mouse.focus = NULL;
    dummy_mouse.mouseID = 0;

    return &dummy_mouse;
}


/* ================= Test Case Support ================== */

#define PEN_NUM_TEST_IDS 8

/* Helper functions */

/* Iterate over all pens to find index for pen ID, otherwise -1 */
static int
_pen_iterationFindsPenIDAt(SDL_PenID needle)
{
    int i;
    for (i = 0; i < SDL_NumPens(); ++i) {
        SDL_PenID pen_id = SDL_PenIDForIndex(i);
        if (pen_id.id == needle.id) {
            return i;
        }
    }
    return -1;
}

/* Assert number of pens is as expected */
static void
_AssertCheck_num_pens(int expected, char *location)
{
    int num_pens = SDL_NumPens();
    SDLTest_AssertCheck(expected == num_pens,
                        "Expected SDL_NumPens() == %d, actual = %d: %s", expected, num_pens, location);

}

/* ---------------------------------------- */
/* Test device deallocation */

typedef struct {  /* Collection of pen (de)allocation information  */
    unsigned int deallocated_id_flags; /* ith bits set to 1 if the ith test_id is deallocated */
    unsigned int deallocated_deviceinfo_flags; /* ith bits set to 1 if deviceinfo as *int with value i was deallocated */
    SDL_PenID ids[PEN_NUM_TEST_IDS];
    SDL_PenGUID guids[PEN_NUM_TEST_IDS];
    int num_ids;
    int initial_pen_count;
} pen_testdata;

/* SDL_PenGCSweep(): callback for tracking pen deallocation */
static void
_pen_testdata_callback(Uint32 deviceid, void *deviceinfo, void *tracker_ref)
{
    pen_testdata *tracker = (pen_testdata *) tracker_ref;
    int offset = -1;
    int i;

    for (i = 0; i < tracker->num_ids; ++i) {
        if (deviceid == tracker->ids[i].id) {
            tracker->deallocated_id_flags |= (1 << i);
        }
    }

    SDLTest_AssertCheck(deviceinfo != NULL, "Device %d has deviceinfo", deviceid);
    offset = *((int*)deviceinfo);
    SDLTest_AssertCheck(offset >= 0 && offset <= 31, "Device %d has well-formed deviceinfo %d", deviceid, offset);
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
    SDLTest_AssertCheck(count < PEN_NUM_TEST_IDS, "Test setup: Valid number of test IDs requested: %d", (int) count);

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

#define DEVICEINFO_UNCHANGED -17

/* Allocate deviceinfo for pen */
static void
_pen_setDeviceinfo(SDL_Pen *pen, int deviceinfo)
{
    if (deviceinfo == DEVICEINFO_UNCHANGED) {
        SDLTest_AssertCheck(pen->deviceinfo != NULL, "pen->deviceinfo was already set for %p (%d), as expected",
                            pen, pen->header.id.id);
    } else {
        int *data = (int *) SDL_malloc(sizeof(int));
        *data = deviceinfo;

        SDLTest_AssertCheck(pen->deviceinfo == NULL, "pen->deviceinfo was NULL for %p (%d) when requesting deviceinfo %d",
                            pen, pen->header.id.id, deviceinfo);

        pen->deviceinfo = data;
    }
    SDL_PenModifyEnd(pen, SDL_TRUE);
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
    SDLTest_AssertCheck(0, "Deallocation for deviceid %d during enableAndRestore: not expected", deviceid);
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
_pen_enableAndRestore(deviceinfo_backup *backup, int test_marksweep)
{
    if (test_marksweep) {
        SDL_PenGCMark();
    }
    while (backup) {
        SDL_Pen *disabledpen = SDL_GetPen(backup->deviceid);

        SDL_PenModifyEnd(SDL_PenModifyBegin(disabledpen->header.id.id),
                         SDL_TRUE);
        disabledpen->deviceinfo = backup->deviceinfo;

        deviceinfo_backup *next = backup->next;
        SDL_free(backup);
        backup = next;
    }
    if (test_marksweep){
        SDL_PenGCSweep(NULL, _pen_assert_impossible);
    }
}


/* ---------------------------------------- */
/* Default set-up and tear down routines    */

/* Back up existing pens, allocate fresh ones but don't assign them yet */
static deviceinfo_backup*
_setup_test(pen_testdata *ptest, int pens_for_testing)
{
    int i;
    deviceinfo_backup *backup;

    ptest->initial_pen_count = SDL_NumPens();

    /* Grab unused pen IDs for testing */
    _pen_unusedIDs(ptest, pens_for_testing);
    for (i = 0; i < pens_for_testing; ++i) {
        int index = _pen_iterationFindsPenIDAt(ptest->ids[i]);
        SDLTest_AssertCheck(-1 == index,
                           "Registered PenID(%d) since index %d == -1", ptest->ids[i].id, index);
    }

    /* Remove existing pens, but back up */
    backup = _pen_disableAndBackup();

    _AssertCheck_num_pens(0, "after disabling and backing up all current pens");
    SDLTest_AssertPass("Removed existing pens");

    return backup;
}

static void
_teardown_test_general(pen_testdata *ptest, deviceinfo_backup *backup, int with_gc_test)
{
    /* Restore previously existing pens */
    _pen_enableAndRestore(backup, with_gc_test);

    /* validate */
    SDLTest_AssertPass("Restored pens to pre-test state");
    _AssertCheck_num_pens(ptest->initial_pen_count, "after restoring all initial pens");
}

static void
_teardown_test(pen_testdata *ptest, deviceinfo_backup *backup)
{
    _teardown_test_general(ptest, backup, 0);
}

static void
_teardown_test_with_gc(pen_testdata *ptest, deviceinfo_backup *backup)
{
    _teardown_test_general(ptest, backup, 1);
}


/* ---------------------------------------- */
/* Pen simulation                           */

#define SIMPEN_ACTION_DONE           0
#define SIMPEN_ACTION_MOVE_X         1
#define SIMPEN_ACTION_MOVE_Y         2
#define SIMPEN_ACTION_AXIS           3
#define SIMPEN_ACTION_MOTION_EVENT   4
#define SIMPEN_ACTION_PRESS          5 /* implicit update event */
#define SIMPEN_ACTION_RELEASE        6 /* implicit update event */
#define SIMPEN_ACTION_ERASER_MODE    7

/* Individual action in pen simulation script */
typedef struct simulated_pen_action {
    int type;
    int pen_index;   /* index into the list of simulated pens */
    int index;       /* button or axis number, if needed */
    float update;    /* x,y; for AXIS, update[0] is the updated axis */
} simulated_pen_action;

static simulated_pen_action
_simpen_event(int type, int pen_index, int index, float v, int line_nr) {
    simulated_pen_action action;
    action.type = type;
    action.pen_index = pen_index;
    action.index = index;
    action.update = v;

    /* Sanity check-- turned out to be necessary */
    if ((type == SIMPEN_ACTION_PRESS || type == SIMPEN_ACTION_RELEASE)
        && index == 0) {
        fprintf(stderr, "Error: SIMPEN_EVENT_BUTTON must have button > 0  (LMB is button 1!), in line %d!\n", line_nr);
        exit(1);
    }
    return action;
}

#define SIMPEN_DONE()                                   \
    _simpen_event(SIMPEN_ACTION_DONE, 0, 0, 0.0f, __LINE__)
#define SIMPEN_MOVE(pen_index, x, y)                                    \
    _simpen_event(SIMPEN_ACTION_MOVE_X, (pen_index), 0, (x), __LINE__), \
        _simpen_event(SIMPEN_ACTION_MOVE_Y, (pen_index), 0, (y), __LINE__)

#define SIMPEN_AXIS(pen_index, axis, y)                         \
    _simpen_event(SIMPEN_ACTION_AXIS, (pen_index), (axis), (y), __LINE__)

#define SIMPEN_EVENT_MOTION(pen_index)                                  \
    _simpen_event(SIMPEN_ACTION_MOTION_EVENT, (pen_index), 0, 0.0f, __LINE__)

#define SIMPEN_EVENT_BUTTON(pen_index, push, button)                    \
    _simpen_event((push) ? SIMPEN_ACTION_PRESS : SIMPEN_ACTION_RELEASE, (pen_index), (button), 0.0f, __LINE__)

#define SIMPEN_SET_ERASER(pen_index, eraser_mode)                       \
    _simpen_event(SIMPEN_ACTION_ERASER_MODE, (pen_index), eraser_mode, 0.0f, __LINE__)


static void
_pen_dump(SDL_Pen *pen)
{
    int i;

    if (!pen) {
        fprintf(stderr, "(NULL pen)\n");
        return;
    }

    fprintf(stderr, "pen %d (%s): status=%04x, flags=%x, x,y=(%f, %f) axes = ",
            pen->header.id.id, pen->name, pen->last.buttons,
            pen->header.flags, pen->last.x, pen->last.y);
    for (i = 0; i < SDL_PEN_NUM_AXES; ++i) {
        fprintf(stderr, "\t%f", pen->last.axes[i]);
    }
    fprintf(stderr, "\n");
}

/* Runs until the next event has been issued or we are done and returns pointer to it.
   Returns NULL once we hit SIMPEN_ACTION_DONE.
   Updates simulated_pens accordingly.  There must be as many simulated_pens as the highest pen_index used in
   any of the "steps".
   Also validates the internal state with expectations (via SDL_PenStatus()) and updates the, but does not poll SDL events. */
static simulated_pen_action *
_pen_simulate(simulated_pen_action *steps, int *step_counter, SDL_Pen *simulated_pens, int num_pens)
{
    SDL_bool done = SDL_FALSE;
    SDL_bool dump_pens = SDL_FALSE;
    unsigned int mask;
    int pen_nr;

    do {
        simulated_pen_action step = steps[*step_counter];
        SDL_Pen *simpen = &simulated_pens[step.pen_index];

        if (step.pen_index >= num_pens) {
            SDLTest_AssertCheck(0,
                                "Unexpected pen index %d at step %d, action %d", step.pen_index, *step_counter, step.type);
            return SDL_FALSE;
        }

        switch (step.type) {
        case SIMPEN_ACTION_DONE:
            SDLTest_AssertPass("SIMPEN_ACTION_DONE");
            return SDL_FALSE;

        case SIMPEN_ACTION_MOVE_X:
            SDLTest_AssertPass("SIMPEN_ACTION_MOVE_X [pen %d] : y <- %f", step.pen_index, step.update);
            simpen->last.x = step.update;
            break;

        case SIMPEN_ACTION_MOVE_Y:
            SDLTest_AssertPass("SIMPEN_ACTION_MOVE_Y [pen %d] : x <- %f", step.pen_index, step.update);
            simpen->last.y = step.update;
            break;

        case SIMPEN_ACTION_AXIS:
            SDLTest_AssertPass("SIMPEN_ACTION_AXIS [pen %d] : axis[%d] <- %f", step.pen_index, step.index, step.update);
            simpen->last.axes[step.index] = step.update;
            break;

        case SIMPEN_ACTION_MOTION_EVENT:
            SDLTest_AssertCheck(SDL_SendPenMotion(NULL, simpen->header.id, SDL_TRUE,
                                                  &simpen->last),
                                "SIMPEN_ACTION_MOTION_EVENT [pen %d]", step.pen_index);
            done = true;
            break;

        case SIMPEN_ACTION_PRESS:
            mask = (1 << (step.index - 1));
            simpen->last.buttons |= mask;
            SDLTest_AssertCheck(SDL_SendPenButton(NULL, simpen->header.id, SDL_PRESSED, step.index),
                                "SIMPEN_ACTION_PRESS [pen %d]: button %d (mask %x)", step.pen_index, step.index, mask);
            done = true;
            break;

        case SIMPEN_ACTION_RELEASE:
            mask = ~(1 << (step.index - 1));
            simpen->last.buttons &= mask;
            SDLTest_AssertCheck(SDL_SendPenButton(NULL, simpen->header.id, SDL_RELEASED, step.index),
                                "SIMPEN_ACTION_RELEASE [pen %d]: button %d (mask %x)", step.pen_index, step.index, mask);
            done = true;
            break;

        default:
            SDLTest_AssertCheck(0,
                                "Unexpected pen simulation action %d", step.type);
            return SDL_FALSE;
        }
        ++(*step_counter);
    } while (!done);

    for (pen_nr = 0; pen_nr < num_pens; ++pen_nr) {
        SDL_Pen *simpen = &simulated_pens[pen_nr];
        float x, y;
        float axes[SDL_PEN_NUM_AXES];
        Uint32 actual_flags = SDL_PenStatus(simpen->header.id, &x, &y, axes, SDL_PEN_NUM_AXES);
        int i;

        if (simpen->last.x != x || simpen->last.y != y) {
            SDLTest_AssertCheck(0, "Coordinate mismatch in pen %d", pen_nr);
            dump_pens = SDL_TRUE;
        }
        if ((actual_flags & ~(SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK)) != (simpen->last.buttons & ~(SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK))) {
            SDLTest_AssertCheck(0, "Status mismatch in pen %d (reported: %08x)", pen_nr, actual_flags);
            dump_pens = SDL_TRUE;
        }
        if ((actual_flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK)) != (simpen->header.flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK))) {
            SDLTest_AssertCheck(0, "Flags mismatch in pen %d (reported: %08x)", pen_nr, actual_flags);
            dump_pens = SDL_TRUE;
        }
        for (i = 0; i < SDL_PEN_NUM_AXES; ++i) {
            if (axes[i] != simpen->last.axes[i]) {
                SDLTest_AssertCheck(0, "Axis %d mismatch in pen %d", pen_nr, i);
                dump_pens = SDL_TRUE;
            }
        }
    }

    if (dump_pens) {
        int i;
        for (i = 0; i < num_pens; ++i) {
            fprintf(stderr, "==== pen #%d\n", i);
            fprintf(stderr, "expect:\t");
            _pen_dump(simulated_pens + i);
            fprintf(stderr, "actual:\t");
            _pen_dump(SDL_GetPen(simulated_pens[i].header.id.id));
        }
    }

    return &steps[(*step_counter) - 1];
}

/* Init simulated_pens with suitable initial state */
static void
_pen_simulate_init(pen_testdata *ptest, SDL_Pen *simulated_pens, int num_pens)
{
    int i;
    for (i = 0; i < num_pens; ++i) {
        simulated_pens[i] = *SDL_GetPen(ptest->ids[i].id);
    }
}

/* ================= Test Case Implementation ================== */


/**
 * @brief Check basic pen device introduction and iteration, as well as basic queries
 *
 * @sa SDL_NumPens, SDL_PenIDForIndex, SDL_PenName, SDL_PenCapabilities
 */
static int
pen_iteration(void *arg)
{
    pen_testdata ptest;
    int i;
    char long_pen_name[SDL_PEN_MAX_NAME + 10];
    const char *name;
    deviceinfo_backup *backup;

    /* Check initial pens */
    SDL_PumpEvents();
    SDLTest_AssertPass("SDL_NumPens() = %d", SDL_NumPens());

    /* Grab unused pen IDs for testing */
    backup = _setup_test(&ptest, 3); /* validates that we have zero pens */

    /* Re-run GC, track deallocations */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _AssertCheck_num_pens(0, "after second GC pass");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0, "No unexpected device deallocations");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0, "No unexpected deviceinfo deallocations");
    SDLTest_AssertPass("Validated that GC on empty pen set is idempotent");

    /* Add three pens, validate */
    SDL_PenGCMark();

    SDL_memset(long_pen_name, 'x', sizeof(long_pen_name));     /* Include pen name that is too long */
    long_pen_name[sizeof(long_pen_name) - 1] = 0;

    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "pen 0",
                                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       16);
    _pen_setDeviceinfo(_pen_register(ptest.ids[2], ptest.guids[2], long_pen_name,
                                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK),
                       20);
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "pen 1",
                                       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT_MASK),
                       24);
    _pen_trackGCSweep(&ptest);

    _AssertCheck_num_pens(3, "after allocating three pens");

    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0, "No unexpected device deallocations");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0, "No unexpected deviceinfo deallocations");

    for (i = 0; i < 3; ++i) {
        /* Check that all pens are accounted for */
        int index = _pen_iterationFindsPenIDAt(ptest.ids[i]);
        SDLTest_AssertCheck(-1 != index,  "Found PenID(%d)", ptest.ids[i].id);
    }
    SDLTest_AssertPass("Validated that all three pens are indexable");

    /* Check pen properties */
    SDLTest_AssertCheck(0 == SDL_strcmp("pen 0", SDL_PenName(ptest.ids[0])),
                        "Pen #0 name");
    SDLTest_AssertCheck((SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK) == SDL_PenCapabilities(ptest.ids[0], NULL),
                        "Pen #0 capabilities");

    SDLTest_AssertCheck(0 == SDL_strcmp("pen 1", SDL_PenName(ptest.ids[1])),
                        "Pen #1 name");
    SDLTest_AssertCheck((SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT_MASK) == SDL_PenCapabilities(ptest.ids[1], NULL),
                        "Pen #1 capabilities");

    name = SDL_PenName(ptest.ids[2]);
    SDLTest_AssertCheck(SDL_PEN_MAX_NAME - 1 == SDL_strlen(name),
                        "Pen #2 name length");
    SDLTest_AssertCheck(0 == SDL_memcmp(name, long_pen_name, SDL_PEN_MAX_NAME - 1),
                        "Pen #2 name contents");
    SDLTest_AssertCheck((SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK) == SDL_PenCapabilities(ptest.ids[2], NULL),
                        "Pen #2 capabilities");
    SDLTest_AssertPass("Pen registration and basic queries");

    /* Re-run GC, track deallocations */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _AssertCheck_num_pens(0, "after third GC pass");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x07,
                        "No unexpected device deallocation : %08x", ptest.deallocated_id_flags);
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x01110000,
                        "No unexpected deviceinfo deallocation : %08x ", ptest.deallocated_deviceinfo_flags);
    SDLTest_AssertPass("Validated that GC on empty pen set is idempotent");

    /* tear down and finish */
    _teardown_test(&ptest, backup);
    return TEST_COMPLETED;
}

static void
_expect_pen_attached(SDL_PenID penid)
{
    SDLTest_AssertCheck(-1 != _pen_iterationFindsPenIDAt(penid),  "Found PenID(%d)", penid.id);
    SDLTest_AssertCheck(SDL_PenAttached(penid), "Pen %d was attached, as expected", penid.id);
}

static void
_expect_pen_detached(SDL_PenID penid)
{
    SDLTest_AssertCheck(-1 == _pen_iterationFindsPenIDAt(penid),  "Did not find PenID(%d), as expected", penid.id);
    SDLTest_AssertCheck(!SDL_PenAttached(penid), "Pen %d was detached, as expected", penid.id);
}

/**
 * @brief Check pen device hotplugging
 *
 * @sa SDL_NumPens, SDL_PenIDForIndex, SDL_PenName, SDL_PenCapabilities, SDL_PenAttached
 */
static int
pen_hotplugging(void *arg)
{
    pen_testdata ptest;
    deviceinfo_backup *backup = _setup_test(&ptest, 3);

    /* Add two pens */
    SDL_PenGCMark();

    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "pen 0", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       16);
    _pen_setDeviceinfo(_pen_register(ptest.ids[2], ptest.guids[2], "pen 2", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       24);
    _pen_trackGCSweep(&ptest);

    _AssertCheck_num_pens(2, "after allocating two pens (pass 1)");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0, "No unexpected device deallocation (pass 1)");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0, "No unexpected deviceinfo deallocation (pass 1)");

    _expect_pen_attached(ptest.ids[0]);
    _expect_pen_detached(ptest.ids[1]);
    _expect_pen_attached(ptest.ids[2]);
    SDLTest_AssertPass("Validated hotplugging (pass 1): attachmend of two pens");

    /* Introduce pen #1, remove pen #2 */
    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "pen 0", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       DEVICEINFO_UNCHANGED);
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "pen 1", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       20);
    _pen_trackGCSweep(&ptest);

    _AssertCheck_num_pens(2, "after allocating two pens (pass 2)");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x04, "No unexpected device deallocation (pass 2): %x", ptest.deallocated_id_flags);
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x01000000, "No unexpected deviceinfo deallocation (pass 2): %x", ptest.deallocated_deviceinfo_flags);

    _expect_pen_attached(ptest.ids[0]);
    _expect_pen_attached(ptest.ids[1]);
    _expect_pen_detached(ptest.ids[2]);
    SDLTest_AssertPass("Validated hotplugging (pass 2): unplug one, attach another");

    /* Return to previous state (#2 attached) */
    SDL_PenGCMark();

    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "pen 0", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT),
                       DEVICEINFO_UNCHANGED);
    _pen_setDeviceinfo(_pen_register(ptest.ids[2], ptest.guids[2], "pen 2", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       24);
    _pen_trackGCSweep(&ptest);

    _AssertCheck_num_pens(2, "after allocating two pens (pass 3)");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x02, "No unexpected device deallocation (pass 3)");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x00100000, "No unexpected deviceinfo deallocation (pass 3)");

    _expect_pen_attached(ptest.ids[0]);
    _expect_pen_detached(ptest.ids[1]);
    _expect_pen_attached(ptest.ids[2]);
    SDLTest_AssertPass("Validated hotplugging (pass 3): return to state of pass 1");

    /* Introduce pen #1, remove pen #0 */
    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "pen 1", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       20);
    _pen_setDeviceinfo(_pen_register(ptest.ids[2], ptest.guids[2], "pen 2", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       DEVICEINFO_UNCHANGED);
    _pen_trackGCSweep(&ptest);

    _AssertCheck_num_pens(2, "after allocating two pens (pass 4)");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x01, "No unexpected device deallocation (pass 4): %x", ptest.deallocated_id_flags);
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x00010000, "No unexpected deviceinfo deallocation (pass 4): %x", ptest.deallocated_deviceinfo_flags);

    _expect_pen_detached(ptest.ids[0]);
    _expect_pen_attached(ptest.ids[1]);
    _expect_pen_attached(ptest.ids[2]);
    SDLTest_AssertPass("Validated hotplugging (pass 5)");

    /* Check detached pen */
    SDLTest_AssertCheck(0 == SDL_strcmp("pen 0", SDL_PenName(ptest.ids[0])),
                        "Pen #0 name");
    SDLTest_AssertCheck(0 == SDL_memcmp(ptest.guids[0].data, SDL_PenGUIDForPenID(ptest.ids[0]).data, sizeof(ptest.guids[0].data)),
                        "Pen #0 guid");
    SDLTest_AssertCheck((SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT) == SDL_PenCapabilities(ptest.ids[0], NULL),
                        "Pen #0 capabilities");

    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _AssertCheck_num_pens(0, "after allocating two pens (cleanup)");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x06, "No unexpected device deallocation (cleanup): %x", ptest.deallocated_id_flags);
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x01100000, "No unexpected deviceinfo deallocation (pass 4): %x", ptest.deallocated_deviceinfo_flags);

    _teardown_test_with_gc(&ptest, backup);

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
    int i;
    char* names[4] = { "pen 0", "pen 1", "pen 2", "pen 3" };
    pen_testdata ptest;
    deviceinfo_backup *backup;

    char* guid_strings[3] = {
        "00112233445566770011223344556680",
        "a0112233445566770011223344556680",
        "a0112233445566770011223344556681",
    };
    char guidbuf[33];

    /* Test by-GUID lookup */
    backup = _setup_test(&ptest, 4);

    /* From string */
    for (i = 0; i < 3; ++i) {
        ptest.guids[i] = SDL_PenGUIDForString(guid_strings[i]);
    }

    /* Back to strings */
    for (i = 0; i < 3; ++i) {
        SDL_PenStringForGUID(ptest.guids[i], guidbuf, 33);
        SDLTest_AssertCheck(0 == SDL_strcmp(guidbuf, guid_strings[i]),
                            "GUID string deserialisation:\n\tExpected:\t'%s'\n\tActual:\t\t'%s'",
                            guidbuf, guid_strings[i]);
    }
    SDLTest_AssertPass("Pen GUID from and back to string");

    /* comparison */
    for (i = 0; i < 3; ++i) {
        int k;
        for (k = 0; k < 3; ++k) {
            SDL_PenGUID lhs = ptest.guids[i];
            SDL_PenGUID rhs = ptest.guids[k];

            if (i == k) {
                SDLTest_AssertCheck(0 == SDL_PenGUIDCompare(lhs, rhs),
                                    "GUID #%d must be equal to GUID %d", i, k);
            } else if (i < k) {
                SDLTest_AssertCheck(0 > SDL_PenGUIDCompare(lhs, rhs),
                                    "GUID #%d must be less than GUID %d", i, k);
            } else {
                SDLTest_AssertCheck(0 < SDL_PenGUIDCompare(lhs, rhs),
                                    "GUID #%d must be bigger than GUID %d", i, k);
            }
        }
    }
    SDLTest_AssertPass("Pen GUID comparison");

    /* Define four pens */
    SDL_PenGCMark();
    for (i = 0; i < 4; ++i) {
        _pen_setDeviceinfo(_pen_register(ptest.ids[i], ptest.guids[i], names[i], SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                           20);
    }
    _pen_trackGCSweep(&ptest);

    /* Detach pens 0 and 2 */
    SDL_PenGCMark();
    for (i = 1; i < 4; i += 2) {
        _pen_setDeviceinfo(_pen_register(ptest.ids[i], ptest.guids[i], names[i], SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                           DEVICEINFO_UNCHANGED);
    }
    _pen_trackGCSweep(&ptest);

    for (i = 0; i < 4; ++i) {
        SDLTest_AssertCheck(ptest.ids[i].id == SDL_PenIDForGUID(ptest.guids[i]).id,
                            "GUID search succeeded for %d", i);
    }

    /* detach all */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);

    _teardown_test(&ptest, backup);
    SDLTest_AssertPass("Pen ID lookup by GUID");


    return TEST_COMPLETED;
}

/**
 * @brief Check pen device button reporting
 *
 */
static int
pen_buttonReporting(void *arg)
{
    int i;
    int button_nr, pen_nr;
    pen_testdata ptest;
    SDL_Event event;
    SDL_PenStatusInfo update;
    float axes[SDL_PEN_NUM_AXES + 1];
    const float expected_x[2] = { 10.0f, 20.0f };
    const float expected_y[2] = { 11.0f, 21.0f };

    /* Register pen */
    deviceinfo_backup *backup = _setup_test(&ptest, 2);
    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "test pen",
                                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT | SDL_PEN_AXIS_YTILT),
                       20);
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "test eraser",
                                       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT | SDL_PEN_AXIS_YTILT),
                       24);
    _pen_trackGCSweep(&ptest);

    /* Position mouse suitably before we start */
    for (i = 0; i <= SDL_PEN_NUM_AXES; ++i) {
        axes[i] = 0.0625f * i;  /* initialise with numbers that can be represented precisely in IEEE 754 and
                                   are > 0.0f and <= 1.0f */
    }
    update.x = expected_x[0];
    update.y = expected_y[0];
    SDL_memcpy(update.axes, axes, sizeof(float) * SDL_PEN_NUM_AXES);
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);
    update.x = expected_x[1];
    update.y = expected_y[1];
    SDL_memcpy(update.axes, axes + 1, sizeof(float) * SDL_PEN_NUM_AXES);
    SDL_SendPenMotion(NULL, ptest.ids[1], SDL_TRUE, &update);

    while (SDL_PollEvent(&event)); /* Flush event queue */
    SDLTest_AssertPass("Pen and eraser set up for button testing");

    /* Actual tests start: pen, then eraser */
    for (pen_nr = 0; pen_nr < 2; ++pen_nr) {
        Uint16 pen_state = 0x0000;
        float *expected_axes = axes + pen_nr;

        if (pen_nr == 1) {
            pen_state |= SDL_PEN_ERASER_MASK;
        }
        for (button_nr = 1; button_nr <= 8; ++button_nr) {
            SDL_bool found_event = SDL_FALSE;
            pen_state |= (1 << (button_nr - 1));

            SDL_SendPenButton(NULL, ptest.ids[pen_nr], SDL_PRESSED, button_nr);
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_PENBUTTONDOWN) {
                    SDLTest_AssertCheck(event.pbutton.which.id == ptest.ids[pen_nr].id,
                                        "Received SDL_PENBUTTONDOWN from correct pen");
                    SDLTest_AssertCheck(event.pbutton.button == button_nr,
                                        "Received SDL_PENBUTTONDOWN from correct button");
                    SDLTest_AssertCheck(event.pbutton.state == SDL_PRESSED,
                                        "Received SDL_PENBUTTONDOWN but and marked SDL_PRESSED");
                    SDLTest_AssertCheck(event.pbutton.pen_state == pen_state,
                                        "Received SDL_PENBUTTONDOWN, and state %04x == %04x (expected)",
                                        event.pbutton.pen_state, pen_state);
                    SDLTest_AssertCheck((event.pbutton.x == expected_x[pen_nr]) && (event.pbutton.y == expected_y[pen_nr]),
                                        "Received SDL_PENBUTTONDOWN event at correct coordinates: (%f, %f) vs (%f, %f) (expected)",
                                        event.pbutton.x, event.pbutton.y, expected_x[pen_nr], expected_y[pen_nr]);
                    SDLTest_AssertCheck(0 == SDL_memcmp(expected_axes, event.pbutton.axes, sizeof(float) * SDL_PEN_NUM_AXES),
                                        "Received SDL_PENBUTTONDOWN event with correct axis values");
                    found_event = SDL_TRUE;
                }
            }
            SDLTest_AssertCheck(found_event,
                                "Received the expected SDL_PENBUTTONDOWN event");
        }
    }
    SDLTest_AssertPass("Pressed all buttons");

    /* Release every other button */
    for (pen_nr = 0; pen_nr < 2; ++pen_nr) {
        Uint16 pen_state = 0x00ff; /* 8 buttons pressed */
        float *expected_axes = axes + pen_nr;

        if (pen_nr == 1) {
            pen_state |= SDL_PEN_ERASER_MASK;
        }
        for (button_nr = pen_nr + 1; button_nr <= 8; button_nr += 2) {
            SDL_bool found_event = SDL_FALSE;
            pen_state &= ~(1 << (button_nr - 1));

            SDL_SendPenButton(NULL, ptest.ids[pen_nr], SDL_RELEASED, button_nr);
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_PENBUTTONUP) {
                    SDLTest_AssertCheck(event.pbutton.which.id == ptest.ids[pen_nr].id,
                                        "Received SDL_PENBUTTONUP from correct pen");
                    SDLTest_AssertCheck(event.pbutton.button == button_nr,
                                        "Received SDL_PENBUTTONUP from correct button");
                    SDLTest_AssertCheck(event.pbutton.state == SDL_RELEASED,
                                        "Received SDL_PENBUTTONUP and is marked SDL_RELEASED");
                    SDLTest_AssertCheck(event.pbutton.pen_state == pen_state,
                                        "Received SDL_PENBUTTONUP, and state %04x == %04x (expected)",
                                        event.pbutton.pen_state, pen_state);
                    SDLTest_AssertCheck((event.pbutton.x == expected_x[pen_nr]) && (event.pbutton.y == expected_y[pen_nr]),
                                        "Received SDL_PENBUTTONUP event at correct coordinates");
                    SDLTest_AssertCheck(0 == SDL_memcmp(expected_axes, event.pbutton.axes, sizeof(float) * SDL_PEN_NUM_AXES),
                                        "Received SDL_PENBUTTONUP event with correct axis values");
                    found_event = SDL_TRUE;
                }
            }
            SDLTest_AssertCheck(found_event,
                                "Received the expected SDL_PENBUTTONUP event");
        }
    }
    SDLTest_AssertPass("Released every other button");

    /* Cleanup */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _teardown_test(&ptest, backup);

    return TEST_COMPLETED;
}


/**
 * @brief Check pen device movement and axis update reporting
 *
 * Also tests SDL_PenStatus for agreement with the most recently reported events
 *
 * @sa SDL_PenStatus
 */
static int
pen_movementAndAxes(void *arg)
{
    pen_testdata ptest;
    SDL_Event event;

    /* Pen simulation */
    simulated_pen_action steps[80] = {
        /* #1: Check basic reporting */
        /* Hover eraser, tilt axes */
        SIMPEN_MOVE(0, 30.0f, 31.0f),
        SIMPEN_AXIS(0, 0, 0.0f),
        SIMPEN_AXIS(0, 1, 0.125f),
        SIMPEN_AXIS(0, 2, 0.5f),
        SIMPEN_EVENT_MOTION(0),

        /* #2: Check multiple pens being reported */
        /* Move pen and touch surface, don't tilt */
        SIMPEN_MOVE(1, 40.0f, 41.0f),
        SIMPEN_AXIS(1, 0, 0.25f),
        SIMPEN_EVENT_MOTION(1),

        /* $3: Multi-buttons */
        /* Press eraser buttons */
        SIMPEN_EVENT_BUTTON(0, "push", 1),
        SIMPEN_EVENT_BUTTON(0, "push", 3),
        SIMPEN_EVENT_BUTTON(0, "push", 2),
        SIMPEN_EVENT_BUTTON(0, 0, 2), /* release again */
        SIMPEN_EVENT_BUTTON(0, "push", 4),

        /* #4: Check move + button actions connecting */
        /* Move and tilt pen, press some pen buttons */
        SIMPEN_MOVE(1, 3.0f, 8.0f),
        SIMPEN_AXIS(1, 0, 0.5f),
        SIMPEN_AXIS(1, 1, -0.125f),
        SIMPEN_AXIS(1, 2, -0.25f),
        SIMPEN_EVENT_MOTION(1),
        SIMPEN_EVENT_BUTTON(1, "push", 3),
        SIMPEN_EVENT_BUTTON(1, "push", 1),

        /* #5: Check nonterference between pens */
        /* Eraser releases buttons */
        SIMPEN_EVENT_BUTTON(0, 0, 2),
        SIMPEN_EVENT_BUTTON(0, 0, 1),

        /* #6: Press-move-release action */
        /* Eraser press-move-release */
        SIMPEN_EVENT_BUTTON(0, "push", 2),
        SIMPEN_MOVE(0, 99.0f, 88.0f),
        SIMPEN_AXIS(0, 0, 0.625f),
        SIMPEN_EVENT_MOTION(0),
        SIMPEN_MOVE(0, 44.5f, 42.25f),
        SIMPEN_EVENT_MOTION(0),
        SIMPEN_EVENT_BUTTON(0, 0, 2),

        /* #7: Intertwining button release actions some more */
        /* Pen releases button */
        SIMPEN_EVENT_BUTTON(1, 0, 3),
        SIMPEN_EVENT_BUTTON(1, 0, 1),

        /* Push one more pen button, then release all ereaser buttons */
        SIMPEN_EVENT_BUTTON(1, "push", 1),
        SIMPEN_EVENT_BUTTON(0, 0, 3),
        SIMPEN_EVENT_BUTTON(0, 0, 4),

        SIMPEN_DONE()
    };
    SDL_Pen simulated_pens[2];
    int sim_pc = 0;
    simulated_pen_action *last_action;

    /* Register pen */
    deviceinfo_backup *backup = _setup_test(&ptest, 2);


    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "test eraser",
                                       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT | SDL_PEN_AXIS_YTILT),
                       20);
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "test pen",
                                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT | SDL_PEN_AXIS_YTILT),
                       24);
    _pen_trackGCSweep(&ptest);
    while (SDL_PollEvent(&event)); /* Flush event queue */
    SDLTest_AssertPass("Pen and eraser set up for testing");


    _pen_simulate_init(&ptest, simulated_pens, 2);
    /* Simulate pen movements */
    while ((last_action = _pen_simulate(steps, &sim_pc, &simulated_pens[0], 2))) {
        int attempts = 0;
        do {
            SDL_PumpEvents();
            SDL_PollEvent(&event);
            if (++attempts > 10000) {
                SDLTest_AssertCheck(0, "Never got the anticipated event");
                return TEST_ABORTED;
            }
        } while (event.type != SDL_PENMOTION
                 && event.type != SDL_PENBUTTONUP
                 && event.type != SDL_PENBUTTONDOWN); /* skip boring events */

        SDL_Pen *simpen = &simulated_pens[last_action->pen_index];
        SDL_PenID reported_which;
        float reported_x, reported_y;
        float *reported_axes;
        Uint32 reported_pen_state;
        Uint32 expected_pen_state = simpen->header.flags & SDL_PEN_ERASER_MASK;
        SDL_bool dump_pens = SDL_FALSE;

        expected_pen_state |= simpen->last.buttons;

        SDLTest_AssertCheck(0 != event.type,
                            "Received the anticipated event");

        switch (last_action->type) {
        case SIMPEN_ACTION_MOTION_EVENT:
            SDLTest_AssertCheck(event.type == SDL_PENMOTION, "Expected pen motion event (got 0x%x)", event.type);
            reported_which = event.pmotion.which;
            reported_x = event.pmotion.x;
            reported_y = event.pmotion.y;
            reported_pen_state = event.pmotion.pen_state;
            reported_axes = &event.pmotion.axes[0];
            break;

        case SIMPEN_ACTION_PRESS:
            SDLTest_AssertCheck(event.type == SDL_PENBUTTONDOWN, "Expected PENBUTTONDOWN event (got 0x%x)", event.type);
            SDLTest_AssertCheck(event.pbutton.state == SDL_PRESSED, "Expected PRESSED button");
            /* Fall through */
        case SIMPEN_ACTION_RELEASE:
            if (last_action->type == SIMPEN_ACTION_RELEASE) {
                SDLTest_AssertCheck(event.type == SDL_PENBUTTONUP, "Expected PENBUTTONUP event (got 0x%x)", event.type);
                SDLTest_AssertCheck(event.pbutton.state == SDL_RELEASED, "Expected RELEASED button");
            }
            SDLTest_AssertCheck(event.pbutton.button == last_action->index, "Expected button %d, got %d",
                                last_action->index, event.pbutton.button);
            reported_which = event.pbutton.which;
            reported_x = event.pbutton.x;
            reported_y = event.pbutton.y;
            reported_pen_state = event.pbutton.pen_state;
            reported_axes = &event.pbutton.axes[0];
            break;

        default:
            SDLTest_AssertCheck(0, "Error in pen simulator: unexpected action %d", last_action->type);
            return TEST_ABORTED;
        }

        if (reported_which.id != simpen->header.id.id) {
            dump_pens = SDL_TRUE;
            SDLTest_AssertCheck(0, "Expected report for pen %d but got report for pen %d",
                                simpen->header.id.id, reported_which.id);
        }
        if (reported_x != simpen->last.x
            || reported_y != simpen->last.y) {
            dump_pens = SDL_TRUE;
            SDLTest_AssertCheck(0, "Mismatch in pen coordinates");
        }
        if (reported_x != simpen->last.x
            || reported_y != simpen->last.y) {
            dump_pens = SDL_TRUE;
            SDLTest_AssertCheck(0, "Mismatch in pen coordinates");
        }
        if (reported_pen_state != expected_pen_state) {
            dump_pens = SDL_TRUE;
            SDLTest_AssertCheck(0, "Mismatch in pen state: %x vs %x (expected)",
                                reported_pen_state, expected_pen_state);
        }
        if (0 != SDL_memcmp(reported_axes, simpen->last.axes, sizeof(float) * SDL_PEN_NUM_AXES)) {
            dump_pens = SDL_TRUE;
            SDLTest_AssertCheck(0, "Mismatch in axes");
        }

        if (dump_pens) {
            fprintf(stderr, "----- Pen #%d:\n", last_action->pen_index);
            fprintf(stderr, "expect:\t");
            _pen_dump(simpen);
            fprintf(stderr, "actual:\t");
            _pen_dump(SDL_GetPen(simpen->header.id.id));
        }
    }
    SDLTest_AssertPass("Pen and eraser move and report events correctly and independently");

    /* Cleanup */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _teardown_test(&ptest, backup);
    return TEST_COMPLETED;
}

#define SET_POS(update, xpos, ypos) (update).x = (xpos); (update).y = (ypos);

/**
 * @brief Check pen device mouse emulation and event suppression
 *
 * Since we include SDL_pen.c, we link it against our own mock implementations of SDL_PSendMouseButton
 * and SDL_SendMouseMotion; see tehere for details.
 */
static int
pen_mouseEmulation(void *arg)
{
    pen_testdata ptest;
    SDL_Event event;
    int i;
    SDL_PenStatusInfo update;

    /* Register pen */
    deviceinfo_backup *backup = _setup_test(&ptest, 1);
    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "testpen",
                                     SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT | SDL_PEN_AXIS_YTILT),
                       20);
    _pen_trackGCSweep(&ptest);


    /* Initialise pen location */
    SDL_memset(update.axes, 0, sizeof(update.axes));
    SET_POS(update, 100.0f, 100.0f);
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);
    while (SDL_PollEvent(&event)); /* Flush event queue */

    /* Test motion forwarding */
    _mouseemu_last_event = 0;
    SET_POS(update, 121.25f, 110.75f);
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);
    SDLTest_AssertCheck(SDL_MOUSEMOTION == _mouseemu_last_event,
                        "Mouse motion event: %d", _mouseemu_last_event);
    SDLTest_AssertCheck(121 == _mouseemu_last_x && 110 == _mouseemu_last_y,
                        "Motion to correct position: %d,%d", _mouseemu_last_x, _mouseemu_last_y);
    SDLTest_AssertCheck(SDL_PEN_MOUSEID == _mouseemu_last_mouseid,
                        "Observed the expected mouse ID: 0x%x", _mouseemu_last_mouseid);
    SDLTest_AssertCheck(0 == _mouseemu_last_relative,
                        "Absolute motion event");
    SDLTest_AssertPass("Motion emulation");

    /* Test redundant motion report suppression */
    _mouseemu_last_event = 0;

    SET_POS(update, 121.00f, 110.00f);
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);

    SET_POS(update, 121.95f, 110.95f);
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);

    update.axes[0] = 1.0f;
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);

    SET_POS(update, 121.12f, 110.70f);
    update.axes[0] = 0.0f;
    update.axes[1] = 0.75f;
    SDL_SendPenMotion(NULL, ptest.ids[0], SDL_TRUE, &update);

    SDLTest_AssertCheck(0 == _mouseemu_last_event,
                        "Redundant mouse motion suppressed: %d", _mouseemu_last_event);
    SDLTest_AssertPass("Redundant motion suppression");

    /* Test button press reporting */
    for (i = 1; i < 3; ++i) {
        SDL_SendPenButton(NULL, ptest.ids[0], SDL_PRESSED, i);
        SDLTest_AssertCheck(SDL_MOUSEBUTTONDOWN == _mouseemu_last_event,
                            "Mouse button press: %d", _mouseemu_last_event);
        SDLTest_AssertCheck(i == _mouseemu_last_button,
                            "Observed the expected simulated button: %d", _mouseemu_last_button);
        SDLTest_AssertCheck(SDL_PEN_MOUSEID == _mouseemu_last_mouseid,
                            "Observed the expected mouse ID: 0x%x", _mouseemu_last_mouseid);
    }
    SDLTest_AssertPass("Button press mouse emulation");

    /* Test button release reporting */
    for (i = 1; i < 3; ++i) {
        SDL_SendPenButton(NULL, ptest.ids[0], SDL_RELEASED, i);
        SDLTest_AssertCheck(SDL_MOUSEBUTTONUP == _mouseemu_last_event,
                            "Mouse button release: %d", _mouseemu_last_event);
        SDLTest_AssertCheck(i == _mouseemu_last_button,
                            "Observed the expected simulated button: %d", _mouseemu_last_button);
        SDLTest_AssertCheck(SDL_PEN_MOUSEID == _mouseemu_last_mouseid,
                            "Observed the expected mouse ID: 0x%x", _mouseemu_last_mouseid);
    }
    SDLTest_AssertPass("Button release mouse emulation");

    /* Cleanup */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _teardown_test(&ptest, backup);
    return TEST_COMPLETED;
}


/* ================= Test References ================== */

/* Mouse test cases */
static const SDLTest_TestCaseReference penTest1 =
        { (SDLTest_TestCaseFp)pen_iteration, "pen_iteration", "Iterate over all pens with SDL_PenIDForIndex", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest2 =
        { (SDLTest_TestCaseFp)pen_hotplugging, "pen_hotplugging", "Hotplug pens and validate their status, including SDL_PenAttached", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest3 =
        { (SDLTest_TestCaseFp)pen_GUIDs, "pen_GUIDs", "Check SDL_PenGUID operations", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest4 =
        { (SDLTest_TestCaseFp)pen_buttonReporting, "pen_buttonReporting", "Check pen button presses", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest5 =
        { (SDLTest_TestCaseFp)pen_movementAndAxes, "pen_movementAndAxes", "Check pen movement and axis update reporting", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest6 =
        { (SDLTest_TestCaseFp)pen_mouseEmulation, "pen_mouseEmulation", "Check pen-as-mouse event forwarding", TEST_ENABLED };

/* Sequence of Mouse test cases */
static const SDLTest_TestCaseReference *penTests[] =  {
    &penTest1, &penTest2, &penTest3, &penTest4, &penTest5, &penTest6, NULL
};

/* Mouse test suite (global) */
SDLTest_TestSuiteReference penTestSuite = {
    "Pen",
    NULL,
    penTests,
    NULL
};
