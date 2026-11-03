#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "macro_engine.h"

static uint32_t tick;
static bool connected = true;
static unsigned pad_down, pad_up, key_down, key_up;

uint32_t furi_get_tick(void) {
    return tick;
}
uint32_t furi_ms_to_ticks(uint32_t ms) {
    return ms;
}
uint32_t furi_kernel_get_tick_frequency(void) {
    return 1000;
}
File* storage_file_alloc(Storage* s) {
    (void)s;
    return calloc(1, sizeof(File));
}
void storage_file_free(File* f) {
    free(f);
}
bool storage_file_open(File* f, const char* p, int a, int m) {
    (void)a;
    (void)m;
    f->fp = fopen(p, "rb");
    return f->fp != NULL;
}
void storage_file_close(File* f) {
    if(f->fp) fclose(f->fp);
    f->fp = NULL;
}
size_t storage_file_read(File* f, void* p, size_t n) {
    return fread(p, 1, n, f->fp);
}
bool storage_file_seek(File* f, uint32_t p, bool absolute) {
    return fseek(f->fp, p, absolute ? SEEK_SET : SEEK_CUR) == 0;
}
uint64_t storage_file_size(File* f) {
    long p = ftell(f->fp);
    fseek(f->fp, 0, SEEK_END);
    long n = ftell(f->fp);
    fseek(f->fp, p, SEEK_SET);
    return n;
}
bool civ_usb_connected(void) {
    return connected;
}
bool civ_usb_send_pad(bool pressed) {
    if(!connected) return false;
    if(pressed)
        pad_down++;
    else
        pad_up++;
    return true;
}
bool civ_usb_send_keyboard(uint8_t mod, uint8_t key) {
    (void)mod;
    if(!connected) return false;
    if(key)
        key_down++;
    else
        key_up++;
    return true;
}
void civ_usb_neutral(void) {
    civ_usb_send_pad(false);
    civ_usb_send_keyboard(0, 0);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    Storage storage = {0};
    MacroEngine macro = {0};
    bool opened = macro_open(&macro, &storage, argv[2]);
    if(strcmp(argv[1], "reject") == 0) {
        assert(!opened);
        assert(macro.state == MacroError);
        macro_close(&macro);
        return 0;
    }
    assert(opened && macro.state == MacroReady);
    assert(macro_start(&macro));
    if(strcmp(argv[1], "timing") == 0) {
        for(tick = 0; tick < macro.total_ms + 2000; ++tick) {
            uint32_t previous_position = macro.position;
            uint32_t previous_due = macro.due_tick;
            macro_tick(&macro);
            if(macro.position != previous_position) assert(tick >= previous_due);
            if(macro.state == MacroDone) break;
        }
        if(macro.state != MacroDone)
            fprintf(
                stderr,
                "timing stopped: state=%d pos=%u/%u line=%u error=%s tick=%u due=%u\n",
                macro.state,
                macro.position,
                macro.records,
                macro.line,
                macro.error,
                tick,
                macro.due_tick);
        assert(macro.state == MacroDone);
        assert(macro.completed_ms == macro.total_ms);
        assert(tick == macro.total_ms);
        macro_close(&macro);
        return 0;
    }
    bool paused = false, disconnected = false;
    for(tick = 0; tick < macro.total_ms + 10000; tick++) {
        if(tick == 12) macro_pause(&macro);
        macro_tick(&macro);
        if(macro.state == MacroPaused && !paused) {
            paused = true;
            uint32_t pos = macro.position;
            tick += 1000;
            macro_tick(&macro);
            assert(macro.position == pos);
            assert(macro_resume(&macro));
        }
        if(paused && !disconnected && tick > 1600 && macro.state == MacroRunning) {
            connected = false;
            macro_tick(&macro);
            assert(macro.state == MacroDisconnected);
            uint32_t pos = macro.position;
            tick += 1000;
            macro_tick(&macro);
            assert(macro.position == pos);
            connected = true;
            assert(macro_resume(&macro));
            disconnected = true;
        }
        if(macro.state == MacroDone) break;
    }
    assert(paused && disconnected && macro.state == MacroDone);
    assert(macro.position == macro.records);
    assert(macro.completed_ms == macro.total_ms);
    assert(pad_down >= 6 && pad_up >= pad_down && key_down > 0 && key_up > 0);
    macro_close(&macro);
    return 0;
}
