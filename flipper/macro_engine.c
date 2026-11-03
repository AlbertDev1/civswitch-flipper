#include "macro_engine.h"
#include "usb_composite.h"
#include "ascii_hid.h"
#include <furi.h>
#include <stdio.h>
#include <string.h>

#define HEADER_SIZE 24u
#define RECORD_SIZE 8u
#define MAX_RECORDS 1000000u

static uint32_t u32(const uint8_t* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

static uint32_t crc_step(uint32_t crc, const uint8_t* p, size_t n) {
    while(n--) {
        crc ^= *p++;
        for(unsigned k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return crc;
}

static void fail(MacroEngine* m, const char* message) {
    m->state = MacroError;
    snprintf(m->error, sizeof(m->error), "%s", message);
    civ_usb_neutral();
}

static bool
    valid_record(const uint8_t* r, uint32_t expected_line, uint32_t total_lines, bool last) {
    uint32_t ms = u32(r + 4);
    if(r[3] || r[0] < OpPadPress || r[0] > OpTypeAscii) return false;
    if(r[0] == OpLine)
        return !last && r[1] == 0 && r[2] == 0 && ms == expected_line && ms <= total_lines;
    if(r[0] == OpEnd) return last && r[1] == 0 && r[2] == 0 && ms == 0;
    if(last) return false;
    if(r[0] == OpKeyPress)
        return (r[1] == 0 || r[1] == 2) && r[2] >= 4 && r[2] <= 0x65 && ms == 32;
    if(r[0] == OpTypeAscii) {
        uint8_t mod, key;
        return r[1] == 0 && ms == 32 && civ_ascii_to_hid((char)r[2], &mod, &key);
    }
    if(r[1] || r[2]) return false;
    if(r[0] == OpPadPress) return ms == 50;
    if(r[0] == OpKeyRelease) return ms == 0 || ms == 32 || ms == 200 || ms == 750;
    if(r[0] == OpPadRelease) return ms == 550;
    return r[0] == OpWait && ms > 0;
}

bool macro_open(MacroEngine* m, Storage* storage, const char* path) {
    macro_close(m);
    m->storage = storage;
    m->file = storage_file_alloc(storage);
    if(!storage_file_open(m->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        fail(m, "Cannot open payload");
        return false;
    }
    uint8_t h[HEADER_SIZE];
    if(storage_file_read(m->file, h, sizeof(h)) != sizeof(h) || memcmp(h, "FMAC", 4) ||
       h[4] != 1 || h[5] || h[6] != RECORD_SIZE || h[7]) {
        fail(m, "Invalid macro header");
        return false;
    }
    uint32_t count = u32(h + 8), lines = u32(h + 12), duration = u32(h + 16),
             expected_crc = u32(h + 20);
    if(count < 2 || count > MAX_RECORDS || lines == 0 ||
       storage_file_size(m->file) != (uint64_t)HEADER_SIZE + (uint64_t)count * RECORD_SIZE) {
        fail(m, "Invalid macro size");
        return false;
    }
    uint8_t rec[RECORD_SIZE];
    uint32_t crc = 0xFFFFFFFFu, measured_ms = 0, current_line = 0;
    for(uint32_t i = 0; i < count; ++i) {
        if(storage_file_read(m->file, rec, sizeof(rec)) != sizeof(rec)) {
            fail(m, "Truncated macro");
            return false;
        }
        if(rec[0] == OpLine) current_line++;
        if(!valid_record(rec, current_line, lines, i + 1 == count)) {
            fail(m, "Invalid operation");
            return false;
        }
        crc = crc_step(crc, rec, sizeof(rec));
        if(rec[0] != OpLine) {
            uint32_t ms = u32(rec + 4);
            if(UINT32_MAX - measured_ms < ms) {
                fail(m, "Duration overflow");
                return false;
            }
            measured_ms += ms;
        }
    }
    if((crc ^ 0xFFFFFFFFu) != expected_crc || measured_ms != duration || current_line != lines) {
        fail(m, "Checksum or count mismatch");
        return false;
    }
    m->records = count;
    m->total_lines = lines;
    m->total_ms = duration;
    macro_reset(m);
    return true;
}

void macro_close(MacroEngine* m) {
    if(m->state == MacroRunning || m->state == MacroPaused || m->state == MacroDisconnected)
        macro_abort(m);
    if(m->file) {
        storage_file_close(m->file);
        storage_file_free(m->file);
        m->file = NULL;
    }
    m->state = MacroEmpty;
}

void macro_reset(MacroEngine* m) {
    if(!m->file || !storage_file_seek(m->file, HEADER_SIZE, true)) {
        fail(m, "Cannot seek macro");
        return;
    }
    m->position = m->line = m->completed_ms = m->paused_ticks = 0;
    m->due_tick = 0;
    m->finish_tick = 0;
    m->pause_requested = false;
    m->state = MacroReady;
    m->error[0] = 0;
    civ_usb_neutral();
}

bool macro_start(MacroEngine* m) {
    if(m->state != MacroReady || !civ_usb_connected()) return false;
    m->state = MacroRunning;
    m->start_tick = furi_get_tick();
    m->due_tick = m->start_tick;
    return true;
}

void macro_pause(MacroEngine* m) {
    if(m->state != MacroRunning) return;
    m->pause_requested = true;
}

bool macro_resume(MacroEngine* m) {
    if(m->state == MacroRunning && m->pause_requested) {
        m->pause_requested = false;
        return true;
    }
    if((m->state != MacroPaused && m->state != MacroDisconnected) || !civ_usb_connected())
        return false;
    uint32_t now = furi_get_tick();
    m->paused_ticks += now - m->pause_tick;
    m->due_tick = now;
    m->pause_requested = false;
    m->state = MacroRunning;
    return true;
}

void macro_abort(MacroEngine* m) {
    civ_usb_neutral();
    macro_reset(m);
}

void macro_tick(MacroEngine* m) {
    if(m->state != MacroRunning) return;
    /* Process zero-duration metadata without adding an artificial millisecond.
       The small fixed budget keeps a malformed file from monopolizing the UI. */
    for(uint8_t budget = 0; budget < 3; ++budget) {
        if(!civ_usb_connected()) {
            m->state = MacroDisconnected;
            m->pause_tick = furi_get_tick();
            civ_usb_neutral();
            return;
        }
        uint32_t now = furi_get_tick();
        if((int32_t)(now - m->due_tick) < 0) return;
        if(m->pause_requested) {
            civ_usb_neutral();
            m->pause_requested = false;
            m->state = MacroPaused;
            m->pause_tick = furi_get_tick();
            return;
        }
        if(m->position >= m->records) {
            fail(m, "Macro overrun");
            return;
        }
        uint8_t rec[RECORD_SIZE];
        if(storage_file_read(m->file, rec, sizeof(rec)) != sizeof(rec)) {
            fail(m, "Read failed");
            return;
        }
        ++m->position;
        uint32_t expected_line = m->line + (rec[0] == OpLine ? 1u : 0u);
        if(!valid_record(rec, expected_line, m->total_lines, m->position == m->records)) {
            fail(m, "Changed macro record");
            return;
        }
        uint32_t ms = u32(rec + 4);
        uint32_t duration = rec[0] == OpLine ? 0 : ms;
        bool sent = true;
        switch(rec[0]) {
        case OpPadPress:
            sent = civ_usb_send_pad(true);
            break;
        case OpPadRelease:
            sent = civ_usb_send_pad(false);
            break;
        case OpKeyPress:
            sent = civ_usb_send_keyboard(rec[1], rec[2]);
            break;
        case OpTypeAscii: {
            uint8_t mod, key;
            if(!civ_ascii_to_hid((char)rec[2], &mod, &key)) {
                fail(m, "Invalid ASCII");
                return;
            }
            sent = civ_usb_send_keyboard(mod, key);
            break;
        }
        case OpKeyRelease:
            sent = civ_usb_send_keyboard(0, 0);
            break;
        case OpLine:
            m->line = ms;
            break;
        case OpWait:
            break;
        case OpEnd:
            civ_usb_neutral();
            m->finish_tick = furi_get_tick();
            m->state = MacroDone;
            break;
        default:
            fail(m, "Invalid runtime operation");
            return;
        }
        if(!sent) {
            --m->position;
            if(!storage_file_seek(m->file, HEADER_SIZE + m->position * RECORD_SIZE, true)) {
                fail(m, "Cannot retry operation");
                return;
            }
            m->state = MacroDisconnected;
            m->pause_tick = furi_get_tick();
            civ_usb_neutral();
            return;
        }
        if(rec[0] != OpLine && rec[0] != OpEnd) m->completed_ms += duration;
        m->due_tick = furi_get_tick() + furi_ms_to_ticks(duration);
        if(duration != 0 || m->state != MacroRunning) return;
    }
}

uint32_t macro_elapsed_ms(const MacroEngine* m) {
    if(m->state == MacroReady || m->state == MacroEmpty) return 0;
    uint32_t now = m->state == MacroDone                                      ? m->finish_tick :
                   (m->state == MacroPaused || m->state == MacroDisconnected) ? m->pause_tick :
                                                                                furi_get_tick();
    return (uint32_t)(((uint64_t)(now - m->start_tick - m->paused_ticks) * 1000) /
                      furi_kernel_get_tick_frequency());
}

uint32_t macro_remaining_ms(const MacroEngine* m) {
    uint32_t left = m->completed_ms <= m->total_ms ? m->total_ms - m->completed_ms : 0;
    if(m->state == MacroRunning && (int32_t)(m->due_tick - furi_get_tick()) > 0) {
        uint32_t ticks = m->due_tick - furi_get_tick();
        left += (uint32_t)(((uint64_t)ticks * 1000) / furi_kernel_get_tick_frequency());
    }
    return left;
}
