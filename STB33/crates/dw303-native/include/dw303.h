#pragma once
/* DW-303 C ABI for the native format adapters (see crates/dw303-native).
   Parameter indices run 0..dw303_param_count(); values are physical units.
   prepare/process/reset/deactivate belong to the audio context; everything
   else to control threads. Editor calls belong to the host UI thread. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Dw303 Dw303;
typedef struct Dw303Editor Dw303Editor;

typedef struct { uint32_t frame; uint32_t size; uint8_t bytes[4]; } Dw303Midi;
typedef struct { uint32_t frame; uint32_t index; float value; } Dw303Param;

enum {
    DW303_TRANSPORT_PLAYING_VALID = 1,
    DW303_TRANSPORT_PLAYING = 2,
    DW303_TRANSPORT_TEMPO = 4,
    DW303_TRANSPORT_BEATS = 8,
    DW303_TRANSPORT_SAMPLE = 16,
};
typedef struct { uint32_t flags; double tempo; double beats; int64_t sample; } Dw303Transport;

typedef struct {
    uint32_t id;            /* stable host ID, shared with the CLAP plug-in */
    const char *name;       /* static UTF-8 */
    const char *unit;       /* static UTF-8 */
    float minimum, maximum, initial;
    uint32_t steps;         /* 0 = continuous, else number of intervals */
    bool automatable;
} Dw303ParamInfo;

/* Editor gestures, called on the main thread while the editor is open. */
typedef struct {
    void *context;
    void (*begin)(void *context, uint32_t index);
    void (*perform)(void *context, uint32_t index, float value);
    void (*end)(void *context, uint32_t index);
} Dw303EditorHost;

uint32_t dw303_param_count(void);
bool dw303_param_info(uint32_t index, Dw303ParamInfo *info);
float dw303_param_normalize(uint32_t index, float value);
float dw303_param_denormalize(uint32_t index, float normalized);
/* Writes a NUL-terminated string; returns false if it does not fit. */
bool dw303_param_text(uint32_t index, float value, char *text, size_t capacity);
bool dw303_param_parse(uint32_t index, const char *text, float *value);

Dw303 *dw303_create(void);
void dw303_destroy(Dw303 *);
bool dw303_prepare(Dw303 *, double sample_rate, uint32_t maximum_frames);
void dw303_reset(Dw303 *);
void dw303_deactivate(Dw303 *);
/* midi and params must each be ordered by frame. Output is cleared on failure. */
bool dw303_process(Dw303 *, float *left, float *right, uint32_t frames,
                   const Dw303Midi *midi, size_t midi_count,
                   const Dw303Param *params, size_t param_count,
                   const Dw303Transport *transport);
/* Latest value, including panel and sequencer changes made by the engine. */
float dw303_value(const Dw303 *, uint32_t index);
/* Queue an edit for the next block; false if invalid or the queue is full. */
bool dw303_set_value(Dw303 *, uint32_t index, float value);

uint8_t *dw303_state_save(const Dw303 *, size_t *size);
bool dw303_state_load(Dw303 *, const uint8_t *bytes, size_t size);
void dw303_bytes_free(uint8_t *bytes, size_t size);

/* Sizes are logical points on macOS and physical pixels on Windows and Linux,
   where the panel range scales with the host content scale (1-4). */
void dw303_editor_default_size(uint32_t *width, uint32_t *height);
/* Nearest permitted size with the panel's aspect ratio. */
void dw303_editor_constrain(uint32_t *width, uint32_t *height);
void dw303_editor_constrain_scaled(double scale, uint32_t *width, uint32_t *height);
Dw303Editor *dw303_editor_create(Dw303 *, Dw303EditorHost host);
/* parent: NSView (macOS), HWND (Windows) or X11 Window ID cast to void* (Linux). */
bool dw303_editor_attach(Dw303Editor *, void *parent);
bool dw303_editor_set_scale(Dw303Editor *, double scale);
bool dw303_editor_set_size(Dw303Editor *, uint32_t width, uint32_t height);
void dw303_editor_destroy(Dw303Editor *);
/* Linux: run pending GTK work; call every ~10 ms from the host UI loop while an
   editor is open. A no-op on macOS and Windows. */
void dw303_editor_idle(void);

#ifdef __cplusplus
}
#endif
