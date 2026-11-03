#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <storage/storage.h>

typedef enum {
    MacroEmpty,
    MacroReady,
    MacroRunning,
    MacroPaused,
    MacroDisconnected,
    MacroDone,
    MacroError
} MacroState;
typedef enum {
    OpPadPress = 1,
    OpPadRelease,
    OpKeyPress,
    OpKeyRelease,
    OpWait,
    OpLine,
    OpEnd,
    OpTypeAscii
} MacroOp;

typedef struct {
    Storage* storage;
    File* file;
    MacroState state;
    uint32_t records, total_lines, total_ms, completed_ms, position, line;
    uint32_t due_tick, start_tick, paused_ticks, pause_tick, finish_tick;
    bool pause_requested;
    char error[48];
} MacroEngine;

bool macro_open(MacroEngine* m, Storage* storage, const char* path);
void macro_close(MacroEngine* m);
bool macro_start(MacroEngine* m);
void macro_tick(MacroEngine* m);
void macro_pause(MacroEngine* m);
bool macro_resume(MacroEngine* m);
void macro_abort(MacroEngine* m);
void macro_reset(MacroEngine* m);
uint32_t macro_elapsed_ms(const MacroEngine* m);
uint32_t macro_remaining_ms(const MacroEngine* m);
