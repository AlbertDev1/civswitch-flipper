#include "macro_engine.h"
#include "usb_composite.h"
#include "ascii_hid.h"
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/view.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define DATA_DIR "/ext/apps_data/civswitch"

/* Diagnostic safety timeout. 0 in production builds; a test build may define a
   nonzero value so the app always exits on its own even if something goes
   wrong. Back always exits too, so this is only a backstop. */
#ifndef CIVSWITCH_DIAGNOSTIC_TIMEOUT_MS
#define CIVSWITCH_DIAGNOSTIC_TIMEOUT_MS 0
#endif

/* Set to 1 to take over USB with the composite gamepad+keyboard interface.
   Left at 0 for the input-architecture test build so the device stays in CDC
   serial mode (recoverable over the CLI). */
#ifndef CIVSWITCH_ENABLE_USB
#define CIVSWITCH_ENABLE_USB 1
#endif

/* Macro engine tick granularity (ms). Small enough to honor the 32 ms key
   holds the Pico sequence relies on. */
#define CIVSWITCH_TICK_MS 2u
#define CIVSWITCH_REDRAW_MS 100u

typedef enum {
    ViewIdMenu = 0,
    ViewIdWork = 1,
} ViewId;

typedef enum {
    PageRun,
    PageConfirmStart,
    PageConfirmAbort,
    PageConnection,
    PagePadTest,
    PageKeyTest,
    PageSettings,
    PageAbout
} Page;

typedef enum {
    MenuLoad = 0,
    MenuStart,
    MenuConnection,
    MenuPadTest,
    MenuKeyTest,
    MenuSettings,
    MenuAbout
} MenuIndex;

typedef struct {
    Gui* gui;
    ViewDispatcher* vd;
    Submenu* menu;
    View* work;
    DialogsApp* dialogs;
    Storage* storage;
    MacroEngine macro;
    FuriHalUsbInterface* previous_usb;
    bool usb_configured;
    Page page;
    uint8_t test_key;
    bool abort_was_running;
    uint32_t started_at;
    uint32_t redraw_accum_ms;
    bool redraw_pending;
    char selected_file[52];
} CivApp;

/* The custom View's model only needs to reach the app instance; all state
   lives in CivApp and is touched exclusively from the view dispatcher thread. */
typedef struct {
    CivApp* app;
} WorkModel;

static const char* state_name(MacroState state) {
    switch(state) {
    case MacroReady:
        return "Ready";
    case MacroRunning:
        return "Running";
    case MacroPaused:
        return "Paused";
    case MacroDisconnected:
        return "USB lost";
    case MacroDone:
        return "Complete";
    case MacroError:
        return "Error";
    default:
        return "No payload";
    }
}

static void work_request_redraw(CivApp* a) {
    /* Commit the model so the dispatcher repaints the work view. */
    with_view_model(a->work, WorkModel * m, { m->app = a; }, true);
}

/* ---- drawing ---- */

static void work_draw(Canvas* canvas, void* model) {
    WorkModel* m = model;
    CivApp* a = m->app;
    if(!a) return;
    char buf[64];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "CivSwitch");
    canvas_set_font(canvas, FontSecondary);

    if(a->page == PageRun) {
        snprintf(
            buf,
            sizeof(buf),
            "USB:%s  %s",
            civ_usb_connected() ? "on" : "off",
            state_name(a->macro.state));
        canvas_draw_str(canvas, 0, 21, buf);
        snprintf(
            buf,
            sizeof(buf),
            "Line %lu/%lu  %lu%%",
            (unsigned long)a->macro.line,
            (unsigned long)a->macro.total_lines,
            a->macro.total_ms ?
                (unsigned long)((uint64_t)(a->macro.total_ms - macro_remaining_ms(&a->macro)) *
                                100 / a->macro.total_ms) :
                0UL);
        canvas_draw_str(canvas, 0, 32, buf);
        snprintf(
            buf,
            sizeof(buf),
            "Elapsed %lus  ETA %lus",
            (unsigned long)(macro_elapsed_ms(&a->macro) / 1000),
            (unsigned long)(macro_remaining_ms(&a->macro) / 1000));
        canvas_draw_str(canvas, 0, 43, buf);
        if(a->macro.state == MacroError)
            canvas_draw_str(canvas, 0, 54, a->macro.error);
        else if(a->macro.state == MacroDisconnected)
            canvas_draw_str(canvas, 0, 54, "OK: resume manually");
        else
            canvas_draw_str(canvas, 0, 54, "OK: pause/resume");
        canvas_draw_str(canvas, 0, 63, "Back: abort");
    } else if(a->page == PageConfirmStart) {
        canvas_draw_str(canvas, 0, 25, "Start payload injection?");
        canvas_draw_str(canvas, 0, 45, "OK: start  Back: cancel");
    } else if(a->page == PageConfirmAbort) {
        canvas_draw_str(canvas, 0, 25, "Abort and release keys?");
        canvas_draw_str(canvas, 0, 45, "OK: abort  Back: cancel");
    } else if(a->page == PageConnection) {
        canvas_draw_str(
            canvas,
            0,
            25,
            civ_usb_connected() ? "Composite USB configured" : "Waiting for USB host");
        canvas_draw_str(canvas, 0, 50, "Back: menu");
    } else if(a->page == PagePadTest) {
        canvas_draw_str(canvas, 0, 25, "OK: send one A press");
        canvas_draw_str(canvas, 0, 40, "50 ms, then release");
        canvas_draw_str(canvas, 0, 60, "Back: menu");
    } else if(a->page == PageKeyTest) {
        snprintf(buf, sizeof(buf), "OK: type %c once", a->test_key ? '1' : 'a');
        canvas_draw_str(canvas, 0, 25, buf);
        canvas_draw_str(canvas, 0, 40, "32 ms, then release");
        canvas_draw_str(canvas, 0, 60, "Back: menu");
    } else if(a->page == PageSettings) {
        snprintf(buf, sizeof(buf), "Test key: %c", a->test_key ? '1' : 'a');
        canvas_draw_str(canvas, 0, 25, buf);
        canvas_draw_str(canvas, 0, 40, "Up/Down: change");
        canvas_draw_str(canvas, 0, 60, "Back: menu");
    } else if(a->page == PageAbout) {
        canvas_draw_str(canvas, 0, 25, "CivSwitch USB macro");
        canvas_draw_str(canvas, 0, 40, "Host test before Switch.");
        canvas_draw_str(canvas, 0, 60, "Back: menu");
    }
}

/* ---- work-view input ---- */

static void go_menu(CivApp* a) {
    view_dispatcher_switch_to_view(a->vd, ViewIdMenu);
}

static bool work_input(InputEvent* event, void* context) {
    CivApp* a = context;
    if(event->type != InputTypePress &&
       !(event->type == InputTypeRepeat &&
         (event->key == InputKeyUp || event->key == InputKeyDown)))
        return false;

    bool consumed = true;
    if(a->page == PageConfirmStart) {
        if(event->key == InputKeyBack)
            go_menu(a);
        else if(event->key == InputKeyOk)
            a->page = macro_start(&a->macro) ? PageRun : PageConnection;
        else
            consumed = false;
    } else if(a->page == PageConfirmAbort) {
        if(event->key == InputKeyBack) {
            if(a->abort_was_running) macro_resume(&a->macro);
            a->page = PageRun;
        } else if(event->key == InputKeyOk) {
            macro_abort(&a->macro);
            go_menu(a);
        } else
            consumed = false;
    } else if(a->page == PageRun) {
        if(event->key == InputKeyBack) {
            if(a->macro.state == MacroRunning || a->macro.state == MacroPaused ||
               a->macro.state == MacroDisconnected) {
                a->abort_was_running = a->macro.state == MacroRunning;
                if(a->abort_was_running) macro_pause(&a->macro);
                a->page = PageConfirmAbort;
            } else
                go_menu(a);
        } else if(event->key == InputKeyOk) {
            if(a->macro.state == MacroRunning)
                macro_pause(&a->macro);
            else if(a->macro.state == MacroPaused || a->macro.state == MacroDisconnected)
                macro_resume(&a->macro);
            else if(a->macro.state == MacroReady)
                a->page = PageConfirmStart;
            else if(a->macro.state == MacroDone)
                macro_reset(&a->macro);
        } else
            consumed = false;
    } else if(a->page == PagePadTest) {
        if(event->key == InputKeyBack)
            go_menu(a);
        else if(event->key == InputKeyOk && civ_usb_connected()) {
            civ_usb_send_pad(true);
            furi_delay_ms(50);
            civ_usb_send_pad(false);
        } else
            consumed = false;
    } else if(a->page == PageKeyTest) {
        if(event->key == InputKeyBack)
            go_menu(a);
        else if(event->key == InputKeyOk && civ_usb_connected()) {
            uint8_t mod, key;
            civ_ascii_to_hid(a->test_key ? '1' : 'a', &mod, &key);
            civ_usb_send_keyboard(mod, key);
            furi_delay_ms(32);
            civ_usb_send_keyboard(0, 0);
        } else
            consumed = false;
    } else if(a->page == PageSettings) {
        if(event->key == InputKeyUp || event->key == InputKeyDown)
            a->test_key ^= 1;
        else if(event->key == InputKeyBack)
            go_menu(a);
        else
            consumed = false;
    } else { /* PageAbout, PageConnection */
        if(event->key == InputKeyBack)
            go_menu(a);
        else
            consumed = false;
    }

    if(consumed) work_request_redraw(a);
    return consumed;
}

static uint32_t work_previous(void* context) {
    UNUSED(context);
    return ViewIdMenu;
}

static uint32_t menu_previous(void* context) {
    UNUSED(context);
    return VIEW_NONE; /* Back on the menu exits the app. */
}

/* ---- menu ---- */

static void load_payload(CivApp* a) {
    FuriString* path = furi_string_alloc_set(DATA_DIR);
    FuriString* result = furi_string_alloc();
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".fmacro", NULL);
    options.base_path = DATA_DIR;
    options.skip_assets = true;
    bool chosen = dialog_file_browser_show(a->dialogs, result, path, &options);
    if(chosen) {
        macro_open(&a->macro, a->storage, furi_string_get_cstr(result));
        snprintf(a->selected_file, sizeof(a->selected_file), "%s", furi_string_get_cstr(result));
        a->page = PageRun;
        work_request_redraw(a);
        view_dispatcher_switch_to_view(a->vd, ViewIdWork);
    } else {
        view_dispatcher_switch_to_view(a->vd, ViewIdMenu);
    }
    furi_string_free(path);
    furi_string_free(result);
}

static void menu_callback(void* context, uint32_t index) {
    CivApp* a = context;
    switch(index) {
    case MenuLoad:
        load_payload(a);
        return;
    case MenuStart:
        a->page = a->macro.state == MacroReady ? PageConfirmStart : PageRun;
        break;
    case MenuConnection:
        a->page = PageConnection;
        break;
    case MenuPadTest:
        a->page = PagePadTest;
        break;
    case MenuKeyTest:
        a->page = PageKeyTest;
        break;
    case MenuSettings:
        a->page = PageSettings;
        break;
    case MenuAbout:
        a->page = PageAbout;
        break;
    default:
        return;
    }
    work_request_redraw(a);
    view_dispatcher_switch_to_view(a->vd, ViewIdWork);
}

/* ---- tick: drive the macro and repaint ---- */

static void tick_callback(void* context) {
    CivApp* a = context;

    if(CIVSWITCH_DIAGNOSTIC_TIMEOUT_MS &&
       furi_get_tick() - a->started_at >= furi_ms_to_ticks(CIVSWITCH_DIAGNOSTIC_TIMEOUT_MS)) {
        view_dispatcher_stop(a->vd);
        return;
    }

    bool was_running = a->macro.state == MacroRunning;
    macro_tick(&a->macro);

    a->redraw_accum_ms += CIVSWITCH_TICK_MS;
    if(a->macro.state == MacroRunning || was_running ||
       a->redraw_accum_ms >= CIVSWITCH_REDRAW_MS) {
        a->redraw_accum_ms = 0;
        work_request_redraw(a);
    }
}

int32_t civswitch_app(void* context) {
    UNUSED(context);
    CivApp* a = calloc(1, sizeof(CivApp));
    if(!a) return -1;

    a->storage = furi_record_open(RECORD_STORAGE);
    a->dialogs = furi_record_open(RECORD_DIALOGS);
    a->gui = furi_record_open(RECORD_GUI);
    storage_simply_mkdir(a->storage, DATA_DIR);

    a->vd = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(a->vd, a);
    view_dispatcher_set_tick_event_callback(a->vd, tick_callback, CIVSWITCH_TICK_MS);

    a->menu = submenu_alloc();
    submenu_set_header(a->menu, "CivSwitch");
    submenu_add_item(a->menu, "Load Payload", MenuLoad, menu_callback, a);
    submenu_add_item(a->menu, "Start Automation", MenuStart, menu_callback, a);
    submenu_add_item(a->menu, "USB Connection Test", MenuConnection, menu_callback, a);
    submenu_add_item(a->menu, "Gamepad Test", MenuPadTest, menu_callback, a);
    submenu_add_item(a->menu, "Keyboard Test", MenuKeyTest, menu_callback, a);
    submenu_add_item(a->menu, "Settings", MenuSettings, menu_callback, a);
    submenu_add_item(a->menu, "About", MenuAbout, menu_callback, a);
    view_set_previous_callback(submenu_get_view(a->menu), menu_previous);
    view_dispatcher_add_view(a->vd, ViewIdMenu, submenu_get_view(a->menu));

    a->work = view_alloc();
    view_allocate_model(a->work, ViewModelTypeLocking, sizeof(WorkModel));
    with_view_model(a->work, WorkModel * m, { m->app = a; }, false);
    view_set_context(a->work, a);
    view_set_draw_callback(a->work, work_draw);
    view_set_input_callback(a->work, work_input);
    view_set_previous_callback(a->work, work_previous);
    view_dispatcher_add_view(a->vd, ViewIdWork, a->work);

    a->started_at = furi_get_tick();
    view_dispatcher_attach_to_gui(a->vd, a->gui, ViewDispatcherTypeFullscreen);

#if CIVSWITCH_ENABLE_USB
    a->previous_usb = furi_hal_usb_get_config();
    if(furi_hal_usb_is_locked() || !furi_hal_usb_set_config(&civ_usb_interface, NULL)) {
        a->macro.state = MacroError;
        snprintf(a->macro.error, sizeof(a->macro.error), "USB mode unavailable");
    } else {
        a->usb_configured = true;
    }
#endif

    view_dispatcher_switch_to_view(a->vd, ViewIdMenu);
    view_dispatcher_run(a->vd);

    /* teardown */
    macro_close(&a->macro);
    civ_usb_neutral();
    if(a->usb_configured && furi_hal_usb_get_config() == &civ_usb_interface)
        furi_hal_usb_set_config(a->previous_usb, NULL);

    view_dispatcher_remove_view(a->vd, ViewIdMenu);
    view_dispatcher_remove_view(a->vd, ViewIdWork);
    view_free(a->work);
    submenu_free(a->menu);
    view_dispatcher_free(a->vd);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(a);
    return 0;
}
