#include "terminal_view.h"
#include "furi.h"
#include <gui/canvas.h>
#include <gui/elements.h>

struct TerminalView {
    View* view;
};

typedef struct {
    uint8_t buffer[4096];
    uint8_t* tail;
    size_t size;
    size_t scroll_offset;
    FuriString* tmp_str;
    TerminalDisplayMode display_mode;
} TerminalViewModel;

#define TERMINAL_VIEW_CONTEXT_TO_MODEL(context) \
    furi_check(context);                        \
    TerminalViewModel* model = context;

typedef struct {
    size_t width;
    size_t height;
    size_t glyph_width;
    size_t glyph_height;
    size_t frame_width;
    size_t frame_height;
    size_t frame_padding;
    size_t frame_body_width;
    size_t frame_body_height;
    size_t rows;
    size_t columns;
} TerminalViewDrawInfo;

static inline uint8_t* wrap_pointer(TerminalViewModel* model, uint8_t* ptr) {
    int pos = ptr - model->buffer;

    int offset_from_start_of_buffer = pos % sizeof(model->buffer);

    return model->buffer + offset_from_start_of_buffer;
}

static inline uint8_t* byte_form_start(TerminalViewModel* model, uint8_t* start, size_t offset) {
    return wrap_pointer(model, start + offset);
}

static inline uint8_t
    byte_val_from_start(TerminalViewModel* model, uint8_t* start, size_t offset) {
    uint8_t* ptr = byte_form_start(model, start, offset);
    return *ptr;
}

static uint8_t* terminal_view_get_start(TerminalViewModel* model) {
    uint8_t* start;
    if(model->size != sizeof(model->buffer)) {
        start = model->buffer;
    } else {
        start = model->tail + 1;
    }

    start = start + model->scroll_offset;

    return wrap_pointer(model, start);
}

static inline void terminal_view_draw_binary_draw_row(
    Canvas* canvas,
    const TerminalViewDrawInfo* info,
    size_t row,
    FuriString* str) {
    const size_t x = info->frame_padding;

    const size_t y = info->frame_padding + // padding from top
                     info->glyph_height + // strings start to drawing from the bottom
                     (info->glyph_height * row); // offset for row

    canvas_draw_str(canvas, x, y, furi_string_get_cstr(str));
}

static void terminal_view_draw_binary(
    Canvas* canvas,
    TerminalViewModel* model,
    const TerminalViewDrawInfo* info) {
    uint8_t* start = terminal_view_get_start(model);

    const size_t bytes_per_row = (info->columns / 9); // +1 => Space between bytes
    const size_t bytes_on_screen = bytes_per_row * info->rows; // max number of bytes on screen

    furi_string_reset(model->tmp_str);
    size_t to_print = MIN(model->size, bytes_on_screen); // how many chars need to be printed
    size_t current_row = 0; // offset of current row
    size_t in_row = 0; // printed number of bytes in current row
    size_t offset = 0; // offset of current byte
    while(to_print > 0) {
        uint8_t b = byte_val_from_start(model, start, offset);

        for(int i = 7; i >= 0; i--) {
            if(b & (1 << i)) {
                furi_string_push_back(model->tmp_str, '1');
            } else {
                furi_string_push_back(model->tmp_str, '0');
            }
        }

        furi_string_push_back(model->tmp_str, ' ');

        offset++;
        to_print--;

        in_row++;
        if(in_row >= bytes_per_row) { // end of row reached
            terminal_view_draw_binary_draw_row(canvas, info, current_row, model->tmp_str);
            furi_string_reset(model->tmp_str);

            in_row = 0;
            current_row++;
        } else {
            furi_string_push_back(model->tmp_str, ' '); // separator between bytes/chars
        }
    }

    if(in_row > 0) {
        terminal_view_draw_binary_draw_row(canvas, info, current_row, model->tmp_str);
    }
}

static void terminal_view_draw_callback(Canvas* canvas, void* context) {
    furi_check(canvas);
    TERMINAL_VIEW_CONTEXT_TO_MODEL(context);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontKeyboard);

    TerminalViewDrawInfo info = {0};
    info.width = canvas_width(canvas);
    info.height = canvas_height(canvas);
    info.glyph_width = canvas_current_font_width(canvas);
    info.glyph_height = canvas_current_font_height(canvas);
    info.frame_width = info.width - 4; // 4 => scrollbar has a width of 3 + 1 space
    info.frame_height = info.height; // mainly a alias
    info.frame_padding = 2 + 1; // +1 => line with of frame
    info.frame_body_width = info.frame_width - (info.frame_padding * 2);
    info.frame_body_height = info.frame_height - (info.frame_padding * 2);
    info.rows = info.frame_body_height / info.glyph_height;
    info.columns = info.frame_body_width / info.glyph_width;

    elements_scrollbar(canvas, model->scroll_offset, model->size);
    elements_slightly_rounded_frame(canvas, 0, 0, info.frame_width, info.frame_height);

    switch(model->display_mode) {
    case TerminalDisplayModeAuto:
    case TerminalDisplayModeHex:
    case TerminalDisplayModeBinary:
        terminal_view_draw_binary(canvas, model, &info);
        break;
    default:
        furi_crash("Bad display mode!");
        break;
    }
}

static bool terminal_view_input_callback(InputEvent* event, void* context) {
    furi_check(event);
    furi_check(context);

    return false;
}

static bool terminal_view_custom_callback(uint32_t event, void* context) {
    UNUSED(event);
    UNUSED(context);

    return false;
}

TerminalView* terminal_view_alloc() {
    TerminalView* terminal = malloc(sizeof(TerminalView));

    terminal->view = view_alloc();
    view_set_context(terminal->view, terminal);
    view_allocate_model(terminal->view, ViewModelTypeLockFree, sizeof(TerminalViewModel));
    view_set_draw_callback(terminal->view, terminal_view_draw_callback);
    view_set_input_callback(terminal->view, terminal_view_input_callback);
    view_set_custom_callback(terminal->view, terminal_view_custom_callback);

    with_view_model(
        terminal->view,
        TerminalViewModel * model,
        {
            model->display_mode = TerminalDisplayModeAuto;
            model->tail = model->buffer;
            model->size = 0;
            model->scroll_offset = 0;

            const char testData[] = "ab";
            memcpy(model->buffer, testData, sizeof(testData));
            model->size = sizeof(testData);

            model->tmp_str = furi_string_alloc();
            furi_string_reserve(model->tmp_str, 64);
        },
        true);

    return terminal;
}

View* terminal_view_get_view(TerminalView* terminal) {
    furi_check(terminal);
    furi_check(terminal->view);
    return terminal->view;
}

void terminal_view_free(TerminalView* terminal) {
    furi_check(terminal);

    with_view_model(
        terminal->view, TerminalViewModel * model, { furi_string_free(model->tmp_str); }, true);

    furi_check(terminal->view);
    view_free(terminal->view);

    free(terminal);
}

void terminal_view_reset(TerminalView* terminal) {
    furi_check(terminal);

    with_view_model(
        terminal->view,
        TerminalViewModel * model,
        {
            model->tail = model->buffer;
            model->size = 0;
            model->scroll_offset = 0;
        },
        true);
}

void terminal_view_set_display_mode(TerminalView* terminal, TerminalDisplayMode mode) {
    furi_check(terminal);
    furi_check(mode < TerminalDisplayModeMax);

    with_view_model(
        terminal->view, TerminalViewModel * model, { model->display_mode = mode; }, true);
}

void terminal_view_debug_print_buffer(TerminalView* terminal) {
    with_view_model(
        terminal->view,
        TerminalViewModel * model,
        {
            for(size_t i = 0; i < sizeof(model->buffer); i++) {
                uint8_t c = model->buffer[i];
                printf("\n%3zu: %02X", i, c);

                if((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    printf(" (%c)", c);
                }
            }
        },
        false);
}

static bool handle_recv(TerminalViewModel* model, FuriStreamBuffer* stream) {
    const uint8_t* end_of_buffer = model->buffer + sizeof(model->buffer);
    size_t free = end_of_buffer - model->tail;
    if(free == 0) {
        free = sizeof(model->buffer);
        model->tail = model->buffer;
    }

    size_t read = furi_stream_buffer_receive(stream, model->tail, free, 0);

    if(read == 0) {
        return false;
    }

    model->size = model->size + read;
    if(model->size > sizeof(model->buffer)) {
        model->size = sizeof(model->buffer);
    }

    model->tail = wrap_pointer(model, model->tail + read);

    return true;
}

void terminal_view_append_data_from_stream(TerminalView* terminal, FuriStreamBuffer* stream) {
    furi_check(terminal);
    furi_check(stream);

    bool update;
    with_view_model(
        terminal->view, TerminalViewModel * model, { update = handle_recv(model, stream); }, update)
}
