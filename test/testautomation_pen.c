/**
 * Pen test suite
 */

#define SDL_internal_h_ /* Inhibit dynamic symbol redefinitions that clash with ours */

/* ================= System Under Test (SUT) ================== */
/* Renaming SUT operations to avoid link-time symbol clashes */
#define SDL_NumPens SDL_SUT_NumPens
#define SDL_PenIDForIndex SDL_SUT_PenIDForIndex
#define SDL_PenStatus SDL_SUT_PenStatus
#define SDL_PenIDForGUID SDL_SUT_PenIDForGUID
#define SDL_PenGUIDForPenID SDL_SUT_PenGUIDForPenID
#define SDL_PenGUIDCompare SDL_SUT_PenGUIDCompare
#define SDL_PenStringForGUID SDL_SUT_PenStringForGUID
#define SDL_PenGUIDForString SDL_SUT_PenGUIDForString
#define SDL_PenAttached SDL_SUT_PenAttached
#define SDL_PenName SDL_SUT_PenName
#define SDL_PenCapabilities SDL_SUT_PenCapabilities
#define SDL_PenType SDL_SUT_PenType

#define SDL_GetPen SDL_SUT_GetPen
#define SDL_PenModifyBegin SDL_SUT_PenModifyBegin
#define SDL_PenModifyAddCapabilities SDL_SUT_PenModifyAddCapabilities
#define SDL_PenModifyFromWacomID SDL_SUT_PenModifyFromWacomID
#define SDL_PenModifyEnd SDL_SUT_PenModifyEnd
#define SDL_PenGCMark SDL_SUT_PenGCMark
#define SDL_PenGCSweep SDL_SUT_PenGCSweep
#define SDL_SendPenMotion SDL_SUT_SendPenMotion
#define SDL_SendPenButton SDL_SUT_SendPenButton
#define SDL_PenInit SDL_SUT_PenInit

/* ================= Mock API ================== */

/* For SDL_Window, SDL_Mouse, SDL_MouseID: */
#include "../src/events/SDL_mouse_c.h"

/* Divert calls to mock mouse API: */
#define SDL_SendMouseMotion SDL_Mock_SendMouseMotion
#define SDL_SendMouseButton SDL_Mock_SendMouseButton
#define SDL_GetMouse SDL_Mock_GetMouse
#define SDL_IsMousePositionInWindow SDL_Mock_IsMousePositionInWindow

/* Mock mouse API */
static int SDL_SendMouseMotion(SDL_Window * window, SDL_MouseID mouseID, int relative, int x, int y);
static int SDL_SendMouseButton(SDL_Window * window, SDL_MouseID mouseID, Uint8 state, Uint8 button);
static SDL_Mouse * SDL_GetMouse(void);
static SDL_bool SDL_IsMousePositionInWindow(SDL_Window * window, SDL_MouseID mouseID, int x, int y);

/* Import SUT code with macro-renamed function names  */
#include "../src/events/SDL_pen_c.h"
#include "../src/events/SDL_pen.c"

#include "SDL.h"
#include "SDL_test.h"


/* ================= Internal SDL API Compatibility ================== */
/* Mock implementations of Pen -> Mouse calls */
/* Not thread-safe! */

static SDL_bool
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

static int
SDL_SendMouseButton(SDL_Window * window, SDL_MouseID mouseID, Uint8 state, Uint8 button)
{
    if (mouseID == SDL_PEN_MOUSEID) {
        _mouseemu_last_event = (state == SDL_PRESSED) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        _mouseemu_last_button = button;
        _mouseemu_last_mouseid = mouseID;
    }
    return 1;
}

static int
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


static SDL_Mouse *
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

    SDLTest_AssertCheck(deviceinfo != NULL, "Device %u has deviceinfo", deviceid);
    offset = *((int*)deviceinfo);
    SDLTest_AssertCheck(offset >= 0 && offset <= 31, "Device %u has well-formed deviceinfo %d", deviceid, offset);
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
        SDLTest_AssertCheck(pen->deviceinfo != NULL, "pen->deviceinfo was already set for %p (%u), as expected",
                            pen, pen->header.id.id);
    } else {
        int *data = (int *) SDL_malloc(sizeof(int));
        *data = deviceinfo;

        SDLTest_AssertCheck(pen->deviceinfo == NULL, "pen->deviceinfo was NULL for %p (%u) when requesting deviceinfo %d",
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
    SDLTest_AssertCheck(0, "Deallocation for deviceid %u during enableAndRestore: not expected", deviceid);
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
        deviceinfo_backup *next = backup->next;

        SDL_PenModifyEnd(SDL_PenModifyBegin(disabledpen->header.id.id),
                         SDL_TRUE);
        disabledpen->deviceinfo = backup->deviceinfo;

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
                           "Registered PenID(%u) since index %d == -1", ptest->ids[i].id, index);
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
#define SIMPEN_ACTION_MOTION_EVENT   4 /* epxlicit motion event */
#define SIMPEN_ACTION_MOTION_EVENT_S 5 /* send motion event but expect it to be suppressed */
#define SIMPEN_ACTION_PRESS          6 /* implicit update event */
#define SIMPEN_ACTION_RELEASE        7 /* implicit update event */
#define SIMPEN_ACTION_ERASER_MODE    8

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
        SDL_Log("Error: SIMPEN_EVENT_BUTTON must have button > 0  (LMB is button 1!), in line %d!", line_nr);
        exit(1);
    }
    return action;
}

/* STEP is passed in later (C macros use dynamic scoping) */

#define SIMPEN_DONE()                                   \
    STEP _simpen_event(SIMPEN_ACTION_DONE, 0, 0, 0.0f, __LINE__)
#define SIMPEN_MOVE(pen_index, x, y)                                    \
    STEP _simpen_event(SIMPEN_ACTION_MOVE_X, (pen_index), 0, (x), __LINE__); \
    STEP _simpen_event(SIMPEN_ACTION_MOVE_Y, (pen_index), 0, (y), __LINE__)

#define SIMPEN_AXIS(pen_index, axis, y)                         \
    STEP _simpen_event(SIMPEN_ACTION_AXIS, (pen_index), (axis), (y), __LINE__)

#define SIMPEN_EVENT_MOTION(pen_index)                                  \
    STEP _simpen_event(SIMPEN_ACTION_MOTION_EVENT, (pen_index), 0, 0.0f, __LINE__)

#define SIMPEN_EVENT_MOTION_SUPPRESSED(pen_index)                                  \
    STEP _simpen_event(SIMPEN_ACTION_MOTION_EVENT_S, (pen_index), 0, 0.0f, __LINE__)

#define SIMPEN_EVENT_BUTTON(pen_index, push, button)                    \
    STEP _simpen_event((push) ? SIMPEN_ACTION_PRESS : SIMPEN_ACTION_RELEASE, (pen_index), (button), 0.0f, __LINE__)

#define SIMPEN_SET_ERASER(pen_index, eraser_mode)                       \
    STEP _simpen_event(SIMPEN_ACTION_ERASER_MODE, (pen_index), eraser_mode, 0.0f, __LINE__)


static void
_pen_dump(const char *prefix, SDL_Pen *pen)
{
    int i;
    char *axes_str;

    if (!pen) {
        SDL_Log("(NULL pen)");
        return;
    }

    axes_str = SDL_strdup("");
    for (i = 0; i < SDL_PEN_NUM_AXES; ++i) {
        char *old_axes_str = axes_str;
        SDL_asprintf(&axes_str, "%s\t%f", old_axes_str, pen->last.axes[i]);
        SDL_free(old_axes_str);
    }
    SDL_Log("%s: pen %u (%s): status=%04x, flags=%x, x,y=(%f, %f) axes = %s",
            prefix,
            pen->header.id.id, pen->name, pen->last.buttons,
            pen->header.flags, pen->last.x, pen->last.y,
            axes_str);
    SDL_free(axes_str);
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
            return NULL;
        }

        switch (step.type) {
        case SIMPEN_ACTION_DONE:
            SDLTest_AssertPass("SIMPEN_ACTION_DONE");
            return NULL;

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
            done = SDL_TRUE;
            SDLTest_AssertCheck(SDL_SendPenMotion(NULL, simpen->header.id, SDL_TRUE,
                                                  &simpen->last),
                                "SIMPEN_ACTION_MOTION_EVENT [pen %d]", step.pen_index);
            break;

        case SIMPEN_ACTION_MOTION_EVENT_S:
            SDLTest_AssertCheck(!SDL_SendPenMotion(NULL, simpen->header.id, SDL_TRUE,
                                                   &simpen->last),
                                "SIMPEN_ACTION_MOTION_EVENT_SUPPRESSED [pen %d]", step.pen_index);
            break;

        case SIMPEN_ACTION_PRESS:
            mask = (1 << (step.index - 1));
            simpen->last.buttons |= mask;
            SDLTest_AssertCheck(SDL_SendPenButton(NULL, simpen->header.id, SDL_PRESSED, step.index),
                                "SIMPEN_ACTION_PRESS [pen %d]: button %d (mask %x)", step.pen_index, step.index, mask);
            done = SDL_TRUE;
            break;

        case SIMPEN_ACTION_RELEASE:
            mask = ~(1 << (step.index - 1));
            simpen->last.buttons &= mask;
            SDLTest_AssertCheck(SDL_SendPenButton(NULL, simpen->header.id, SDL_RELEASED, step.index),
                                "SIMPEN_ACTION_RELEASE [pen %d]: button %d (mask %x)", step.pen_index, step.index, mask);
            done = SDL_TRUE;
            break;

        default:
            SDLTest_AssertCheck(0,
                                "Unexpected pen simulation action %d", step.type);
            return NULL;
        }
        ++(*step_counter);
    } while (!done);

    for (pen_nr = 0; pen_nr < num_pens; ++pen_nr) {
        SDL_Pen *simpen = &simulated_pens[pen_nr];
        float x = -1.0f, y = -1.0f;
        float axes[SDL_PEN_NUM_AXES];
        Uint32 actual_flags = SDL_PenStatus(simpen->header.id, &x, &y, axes, SDL_PEN_NUM_AXES);
        int i;

        if (simpen->last.x != x || simpen->last.y != y) {
            SDLTest_AssertCheck(0, "Coordinate mismatch in pen %d", pen_nr);
            dump_pens = SDL_TRUE;
        }
        if ((actual_flags & ~(SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK)) != (simpen->last.buttons & ~(SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK))) {
            SDLTest_AssertCheck(0, "Status mismatch in pen %d (reported: %08x)", pen_nr, (unsigned int) actual_flags);
            dump_pens = SDL_TRUE;
        }
        if ((actual_flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK)) != (simpen->header.flags & (SDL_PEN_INK_MASK | SDL_PEN_ERASER_MASK))) {
            SDLTest_AssertCheck(0, "Flags mismatch in pen %d (reported: %08x)", pen_nr, (unsigned int) actual_flags);
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
            SDL_Log("==== pen #%d", i);
            _pen_dump("expect", simulated_pens + i);
            _pen_dump("actual", SDL_GetPen(simulated_pens[i].header.id.id));
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

/* ---------------------------------------- */
/* Other helper functions                   */

/* "standard" pen registration process */
static SDL_Pen *
_pen_register(SDL_PenID penid, SDL_PenGUID guid, char *name, Uint32 flags)
{
    SDL_Pen *pen = SDL_PenModifyBegin(penid.id);
    pen->guid = guid;
    SDL_strlcpy(pen->name, name, SDL_PEN_MAX_NAME);
    SDL_PenModifyAddCapabilities(pen, flags);
    return pen;
}

/* Test whether EXPECTED and ACTUAL of type TY agree.  Their C format string must be FMT.
   MESSAGE is a string with one format string, passed as ARG0. */
#define SDLTest_AssertEq1(TY, FMT, EXPECTED, ACTUAL, MESSAGE, ARG0) { \
        TY _t_expect = (EXPECTED);                                      \
        TY _t_actual = (ACTUAL);                                        \
        SDLTest_AssertCheck(_t_expect == _t_actual, "L%d: " MESSAGE ": expected " #EXPECTED " = "FMT", actual = "FMT, __LINE__, (ARG0), _t_expect, _t_actual); \
    }                                                                   \

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
        SDLTest_AssertCheck(-1 != index,  "Found PenID(%u)", ptest.ids[i].id);
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
    SDLTest_AssertCheck(-1 != _pen_iterationFindsPenIDAt(penid),  "Found PenID(%u)", penid.id);
    SDLTest_AssertCheck(SDL_PenAttached(penid), "Pen %u was attached, as expected", penid.id);
}

static void
_expect_pen_detached(SDL_PenID penid)
{
    SDLTest_AssertCheck(-1 == _pen_iterationFindsPenIDAt(penid),  "Did not find PenID(%u), as expected", penid.id);
    SDLTest_AssertCheck(!SDL_PenAttached(penid), "Pen %u was detached, as expected", penid.id);
}

#define ATTACHED(i) (1 << (i))

static void
_expect_pens_attached_or_detached(SDL_PenID *pen_ids, int ids, Uint32 mask)
{
    int i;
    int attached_count = 0;
    for (i = 0; i < ids; ++i) {
        if (mask & (1 << i)) {
            ++attached_count;
            _expect_pen_attached(pen_ids[i]);
        } else {
            _expect_pen_detached(pen_ids[i]);
        }
    }
    _AssertCheck_num_pens(attached_count, "While checking attached/detached status");
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
    SDL_PenGUID checkguid;

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

    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(0) | ATTACHED(2));
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

    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(0) | ATTACHED(1));
    SDLTest_AssertPass("Validated hotplugging (pass 2): unplug one, attach another");

    /* Return to previous state (#0 and #2 attached) */
    SDL_PenGCMark();

    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "pen 0", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT),
                       DEVICEINFO_UNCHANGED);
    _pen_setDeviceinfo(_pen_register(ptest.ids[2], ptest.guids[2], "pen 2", SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK),
                       24);
    _pen_trackGCSweep(&ptest);

    _AssertCheck_num_pens(2, "after allocating two pens (pass 3)");
    SDLTest_AssertCheck(ptest.deallocated_id_flags == 0x02, "No unexpected device deallocation (pass 3)");
    SDLTest_AssertCheck(ptest.deallocated_deviceinfo_flags == 0x00100000, "No unexpected deviceinfo deallocation (pass 3)");

    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(0) | ATTACHED(2));
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

    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(1) | ATTACHED(2));
    SDLTest_AssertPass("Validated hotplugging (pass 5)");

    /* Check detached pen */
    SDLTest_AssertCheck(0 == SDL_strcmp("pen 0", SDL_PenName(ptest.ids[0])),
                        "Pen #0 name");
    checkguid = SDL_PenGUIDForPenID(ptest.ids[0]);
    SDLTest_AssertCheck(0 == SDL_memcmp(ptest.guids[0].data, checkguid.data, sizeof(ptest.guids[0].data)),
                        "Pen #0 guid");
    SDLTest_AssertCheck((SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_YTILT) == SDL_PenCapabilities(ptest.ids[0], NULL),
                        "Pen #0 capabilities");
    SDLTest_AssertPass("Validated that detached pens retained name, GUID, axis info after pass 5");

    /* Individually detach #1 dn #2 */
    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(1) | ATTACHED(2));
    SDL_PenModifyEnd(SDL_PenModifyBegin(ptest.ids[1].id), SDL_FALSE);
    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(2));

    SDL_PenModifyEnd(SDL_PenModifyBegin(ptest.ids[2].id), SDL_FALSE);
    _expect_pens_attached_or_detached(ptest.ids, 3, 0);

    SDLTest_AssertPass("Validated individual hotplugging (pass 6)");

    /* Individually attach all */
    SDL_PenModifyEnd(SDL_PenModifyBegin(ptest.ids[2].id), SDL_TRUE);
    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(2));

    SDL_PenModifyEnd(SDL_PenModifyBegin(ptest.ids[0].id), SDL_TRUE);
    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(0) | ATTACHED(2));

    SDL_PenModifyEnd(SDL_PenModifyBegin(ptest.ids[1].id), SDL_TRUE);
    _expect_pens_attached_or_detached(ptest.ids, 3, ATTACHED(0) | ATTACHED(1) | ATTACHED(2));
    SDLTest_AssertPass("Validated individual hotplugging (pass 7)");

    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _AssertCheck_num_pens(0, "after hotplugging test (cleanup)");
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
    const Uint32 all_axes = SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_ROTATION_MASK | SDL_PEN_AXIS_THROTTLE_MASK; 

    /* Register pen */
    deviceinfo_backup *backup = _setup_test(&ptest, 2);
    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "test pen",
                                     SDL_PEN_INK_MASK | all_axes),
                       20);
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "test eraser",
                                     SDL_PEN_ERASER_MASK | all_axes),
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
                    if (0 != SDL_memcmp(expected_axes, event.pbutton.axes, sizeof(float) * SDL_PEN_NUM_AXES)) {
                        int ax;
                        for (ax = 0; ax < SDL_PEN_NUM_AXES; ++ax) {
                            SDL_Log("\tax %d\t%.5f\t%.5f expected (equal=%d)",
                                    ax,
                                    event.pbutton.axes[ax], expected_axes[ax],
                                    event.pbutton.axes[ax] == expected_axes[ax]
                                );
                        }
                    }
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
#define MAX_STEPS 80
    /* Pen simulation */
    simulated_pen_action steps[MAX_STEPS];
    size_t num_steps = 0;

    SDL_Pen simulated_pens[2];
    int sim_pc = 0;
    simulated_pen_action *last_action;

    /* Register pen */
    deviceinfo_backup *backup = _setup_test(&ptest, 2);

    /* Pen simulation program */
#define STEP steps[num_steps++] =

    /* #1: Check basic reporting */
    /* Hover eraser, tilt axes */
    SIMPEN_MOVE(0, 30.0f, 31.0f);
    SIMPEN_AXIS(0, 0, 0.0f);
    SIMPEN_AXIS(0, 1, 0.125f);
    SIMPEN_AXIS(0, 2, 0.5f);
    SIMPEN_EVENT_MOTION(0);

    /* #2: Check that motion events without motion aren't reported */
    SIMPEN_EVENT_MOTION_SUPPRESSED(0);
    SIMPEN_EVENT_MOTION_SUPPRESSED(0);

    /* #3: Check multiple pens being reported */
    /* Move pen and touch surface, don't tilt */
    SIMPEN_MOVE(1, 40.0f, 41.0f);
    SIMPEN_AXIS(1, 0, 0.25f);
    SIMPEN_EVENT_MOTION(1);

    /* $4: Multi-buttons */
    /* Press eraser buttons */
    SIMPEN_EVENT_BUTTON(0, "push", 1);
    SIMPEN_EVENT_BUTTON(0, "push", 3);
    SIMPEN_EVENT_BUTTON(0, "push", 2);
    SIMPEN_EVENT_BUTTON(0, 0, 2); /* release again */
    SIMPEN_EVENT_BUTTON(0, "push", 4);

    /* #5: Check move + button actions connecting */
    /* Move and tilt pen, press some pen buttons */
    SIMPEN_MOVE(1, 3.0f, 8.0f);
    SIMPEN_AXIS(1, 0, 0.5f);
    SIMPEN_AXIS(1, 1, -0.125f);
    SIMPEN_AXIS(1, 2, -0.25f);
    SIMPEN_EVENT_MOTION(1);
    SIMPEN_EVENT_BUTTON(1, "push", 3);
    SIMPEN_EVENT_BUTTON(1, "push", 1);

    /* #6: Check nonterference between pens */
    /* Eraser releases buttons */
    SIMPEN_EVENT_BUTTON(0, 0, 2);
    SIMPEN_EVENT_BUTTON(0, 0, 1);

    /* #7: Press-move-release action */
    /* Eraser press-move-release */
    SIMPEN_EVENT_BUTTON(0, "push", 2);
    SIMPEN_MOVE(0, 99.0f, 88.0f);
    SIMPEN_AXIS(0, 0, 0.625f);
    SIMPEN_EVENT_MOTION(0);
    SIMPEN_MOVE(0, 44.5f, 42.25f);
    SIMPEN_EVENT_MOTION(0);
    SIMPEN_EVENT_BUTTON(0, 0, 2);

    /* #8: Intertwining button release actions some more */
    /* Pen releases button */
    SIMPEN_EVENT_BUTTON(1, 0, 3);
    SIMPEN_EVENT_BUTTON(1, 0, 1);

    /* Push one more pen button, then release all ereaser buttons */
    SIMPEN_EVENT_BUTTON(1, "push", 1);
    SIMPEN_EVENT_BUTTON(0, 0, 3);
    SIMPEN_EVENT_BUTTON(0, 0, 4);

    /* #9: Suppress move on unsupported axis */
    SIMPEN_AXIS(1, 3, -0.25f);
    SIMPEN_EVENT_MOTION_SUPPRESSED(0);

    SIMPEN_DONE();
#undef STEP
    /* End of pen simulation program */

    SDLTest_AssertCheck(num_steps < MAX_STEPS, "Pen simulation program does not exceed buffer size");
#undef MAX_STEPS

    SDL_PenGCMark();
    _pen_setDeviceinfo(_pen_register(ptest.ids[0], ptest.guids[0], "test eraser",
                                       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK),
                       20);
    _pen_setDeviceinfo(_pen_register(ptest.ids[1], ptest.guids[1], "test pen",
                                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK),
                       24);
    _pen_trackGCSweep(&ptest);
    while (SDL_PollEvent(&event)); /* Flush event queue */
    SDLTest_AssertPass("Pen and eraser set up for testing");


    _pen_simulate_init(&ptest, simulated_pens, 2);
    /* Simulate pen movements */
    while ((last_action = _pen_simulate(steps, &sim_pc, &simulated_pens[0], 2))) {
        int attempts = 0;
        SDL_Pen *simpen = &simulated_pens[last_action->pen_index];
        SDL_PenID reported_which;
        float reported_x, reported_y;
        float *reported_axes;
        Uint32 reported_pen_state;
        Uint32 expected_pen_state = simpen->header.flags & SDL_PEN_ERASER_MASK;
        SDL_bool dump_pens = SDL_FALSE;

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
            SDLTest_AssertCheck(0, "Expected report for pen %u but got report for pen %u",
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
            SDL_Log("----- Pen #%d:", last_action->pen_index);
            _pen_dump("expect", simpen);
            _pen_dump("actual", SDL_GetPen(simpen->header.id.id));
        }
    }
    SDLTest_AssertPass("Pen and eraser move and report events correctly and independently");

    /* Cleanup */
    SDL_PenGCMark();
    _pen_trackGCSweep(&ptest);
    _teardown_test(&ptest, backup);
    return TEST_COMPLETED;
}

static void
_expect_pen_config(SDL_PenID penid,
                   SDL_PenGUID expected_guid,
                   SDL_bool expected_attached,
                   char *expected_name,
                   int expected_type,
                   int expected_num_buttons,
                   float expected_max_tilt,
                   int expected_axes)
{
    SDL_PenCapabilityInfo actual_info;
    const char *actual_name = SDL_PenName(penid);

    SDLTest_AssertEq1(int, "%d", 0, SDL_PenGUIDCompare(expected_guid, SDL_PenGUIDForPenID(penid)), "Pen %u guid equality", penid.id);
    SDLTest_AssertCheck(0 == SDL_strcmp(expected_name, actual_name),
                        "Expected name='%s' vs actual='%s'", expected_name, actual_name);

    SDLTest_AssertEq1(int, "%d", expected_attached, SDL_PenAttached(penid), "Pen %u is attached", penid.id);
    SDLTest_AssertEq1(int, "%d", expected_type, SDL_PenType(penid), "Pen %u type", penid.id);
    SDLTest_AssertEq1(int, "%x", expected_axes, SDL_PenCapabilities(penid, &actual_info), "Pen %u axis flags", penid.id);
    SDLTest_AssertEq1(int, "%d", expected_num_buttons, actual_info.num_buttons, "Pen %u number of buttons", penid.id);
    SDLTest_AssertEq1(float, "%f", expected_max_tilt, actual_info.max_tilt, "Pen %u max tilt", penid.id);
}

/**
 * @brief Check backend pen iniitalisation and pen meta-information
 *
 * @sa SDL_PenCapabilities, SDL_PenAxisInfo
 */
static int
pen_initAndInfo(void *arg)
{
    pen_testdata ptest;
    SDL_Pen *pen;
    Uint32 mask;
    char strbuf[SDL_PEN_MAX_NAME];

    /* Init */
    deviceinfo_backup *backup = _setup_test(&ptest, 7);

    /* Register default pen */
    _expect_pens_attached_or_detached(ptest.ids, 7, 0);

    /* Register completely default pen */
    pen = SDL_PenModifyBegin(ptest.ids[0].id);
    SDL_memcpy(pen->guid.data, ptest.guids[0].data, sizeof(ptest.guids[0].data));
    SDL_PenModifyEnd(pen, SDL_TRUE);

    SDL_snprintf(strbuf, sizeof(strbuf),
                 "Pen %u", ptest.ids[0].id);
    _expect_pen_config(ptest.ids[0], ptest.guids[0], SDL_TRUE,
                       strbuf, SDL_PEN_TYPE_PEN, SDL_PEN_INFO_UNKNOWN, 0.0f,
                       SDL_PEN_INK_MASK);
    _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0));
    SDLTest_AssertPass("Pass #1: default pen");

    /* Register mostly-default pen with buttons and custom name */
    pen = SDL_PenModifyBegin(ptest.ids[1].id);
    SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_PRESSURE_MASK);
    SDL_memcpy(pen->guid.data, ptest.guids[1].data, sizeof(ptest.guids[1].data));
    SDL_strlcpy(strbuf, "My special test pen", SDL_PEN_MAX_NAME);
    SDL_strlcpy(pen->name, strbuf, SDL_PEN_MAX_NAME);
    pen->info.num_buttons = 7;
    SDL_PenModifyEnd(pen, SDL_TRUE);

    _expect_pen_config(ptest.ids[1], ptest.guids[1], SDL_TRUE,
                       strbuf, SDL_PEN_TYPE_PEN, 7, 0.0f,
                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK);
    _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0) | ATTACHED(1));
    SDLTest_AssertPass("Pass #2: default pen with button and name info");

    /* Register eraser with default name, but keep initially detached */
    pen = SDL_PenModifyBegin(ptest.ids[2].id);
    SDL_memcpy(pen->guid.data, ptest.guids[2].data, sizeof(ptest.guids[2].data));
    pen->type = SDL_PEN_TYPE_ERASER;
    SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK);
    SDL_PenModifyEnd(pen, SDL_FALSE);

    SDL_snprintf(strbuf, sizeof(strbuf),
                 "Eraser %u", ptest.ids[2].id);
    _expect_pen_config(ptest.ids[2], ptest.guids[2], SDL_FALSE,
                       strbuf, SDL_PEN_TYPE_ERASER, SDL_PEN_INFO_UNKNOWN, SDL_PEN_INFO_UNKNOWN,
                       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK);
    _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0) | ATTACHED(1));
    /* now make available */
    SDL_PenModifyEnd(SDL_PenModifyBegin(ptest.ids[2].id), SDL_TRUE);
    _expect_pen_config(ptest.ids[2], ptest.guids[2], SDL_TRUE,
                       strbuf, SDL_PEN_TYPE_ERASER, SDL_PEN_INFO_UNKNOWN, SDL_PEN_INFO_UNKNOWN,
                       SDL_PEN_ERASER_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK);
    _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0) | ATTACHED(1) | ATTACHED(2));
    SDLTest_AssertPass("Pass #3: eraser-type pen initially detached, then attached");

    /* Abort pen registration */
    pen = SDL_PenModifyBegin(ptest.ids[3].id);
    SDL_memcpy(pen->guid.data, ptest.guids[3].data, sizeof(ptest.guids[3].data));
    SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK);
    pen->type = SDL_PEN_TYPE_NONE;
    SDL_PenModifyEnd(pen, SDL_TRUE);
    _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0) | ATTACHED(1) | ATTACHED(2));
    SDLTest_AssertCheck(NULL == SDL_PenName(ptest.ids[3]), "Pen with aborted registration remains unknown");
    SDLTest_AssertPass("Pass #4: aborted pen registration");

    /* Brush with custom axes */
    pen = SDL_PenModifyBegin(ptest.ids[4].id);
    SDL_memcpy(pen->guid.data, ptest.guids[4].data, sizeof(ptest.guids[4].data));
    SDL_strlcpy(pen->name, "Testish Brush", SDL_PEN_MAX_NAME);
    pen->type = SDL_PEN_TYPE_BRUSH;
    pen->info.num_buttons = 1;
    SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_ROTATION_MASK);
    pen->info.max_tilt = 0.75f;
    SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_XTILT_MASK);
    SDL_PenModifyAddCapabilities(pen, SDL_PEN_AXIS_PRESSURE_MASK);
    SDL_PenModifyEnd(pen, SDL_TRUE);
    _expect_pen_config(ptest.ids[4], ptest.guids[4], SDL_TRUE,
                       "Testish Brush", SDL_PEN_TYPE_BRUSH, 1, 0.75f,
                       SDL_PEN_INK_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_ROTATION_MASK | SDL_PEN_AXIS_PRESSURE_MASK);
    _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0) | ATTACHED(1) | ATTACHED(2) | ATTACHED(4));
    SDLTest_AssertPass("Pass #5: brush-type pen with unusual axis layout");

    /* Wacom airbrush pen */
    {
        const Uint32 wacom_type_id   = 0x0912;
        const Uint32 wacom_serial_id = 0xa0b1c2d3;
        SDL_PenGUID guid = {
            { 0, 0, 0, 0,
              0, 0, 0, 0,
              'W', 'A', 'C', 'M',
              0, 0, 0, 0 } };
        guid.data[0] = (wacom_serial_id >>  0) & 0xff;
        guid.data[1] = (wacom_serial_id >>  8) & 0xff;
        guid.data[2] = (wacom_serial_id >> 16) & 0xff;
        guid.data[3] = (wacom_serial_id >> 24) & 0xff;
        guid.data[4] = (wacom_type_id >>  0) & 0xff;
        guid.data[5] = (wacom_type_id >>  8) & 0xff;
        guid.data[6] = (wacom_type_id >> 16) & 0xff;
        guid.data[7] = (wacom_type_id >> 24) & 0xff;

        pen = SDL_PenModifyBegin(ptest.ids[5].id);
        SDL_PenModifyFromWacomID(pen, wacom_type_id, wacom_serial_id, &mask);
        SDL_PenModifyAddCapabilities(pen, mask);
        SDL_PenModifyEnd(pen, SDL_TRUE);
        _expect_pen_config(ptest.ids[5], guid, SDL_TRUE,
                           "Wacom Airbrush Pen", SDL_PEN_TYPE_AIRBRUSH, 1, SDL_sin(64.0 /* degrees */ * (2.0 * M_PI / 360.0)),
                           SDL_PEN_INK_MASK | SDL_PEN_AXIS_PRESSURE_MASK | SDL_PEN_AXIS_XTILT_MASK | SDL_PEN_AXIS_YTILT_MASK | SDL_PEN_AXIS_DISTANCE_MASK | SDL_PEN_AXIS_THROTTLE_MASK);
        _expect_pens_attached_or_detached(ptest.ids, 7, ATTACHED(0) | ATTACHED(1) | ATTACHED(2) | ATTACHED(4) | ATTACHED(5));
    }
    SDLTest_AssertPass("Pass #6: wacom airbrush pen");

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
        { (SDLTest_TestCaseFp)pen_initAndInfo, "pen_info", "Check pen self-description and initialisation", TEST_ENABLED };

static const SDLTest_TestCaseReference penTest7 =
        { (SDLTest_TestCaseFp)pen_mouseEmulation, "pen_mouseEmulation", "Check pen-as-mouse event forwarding", TEST_ENABLED };

/* Sequence of Mouse test cases */
static const SDLTest_TestCaseReference *penTests[] =  {
    &penTest1, &penTest2, &penTest3, &penTest4, &penTest5, &penTest6, &penTest7, NULL
};

/* Mouse test suite (global) */
SDLTest_TestSuiteReference penTestSuite = {
    "Pen",
    NULL,
    penTests,
    NULL
};
