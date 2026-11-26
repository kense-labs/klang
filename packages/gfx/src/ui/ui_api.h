// Adapted for K.
/* The ui layer's C API: every function stdlib/ui.k reaches through an
   `extern "k_ui_..." fn`, listed once in K_UI_API. That list expands into
   the prototypes below and into the symbol table a JIT run registers
   (src/jit.h), and the test suite checks stdlib/ui.k against it
   (test/api_check.py).

   The layer sits over Nuklear (third_party/nuklear), an immediate-mode GUI
   library, and changes little of it: nk_button_label is k_ui_button_label,
   and ui::button_label in K. What it does change is what cannot cross
   an `extern fn` (spec 7.10):

   * Strings are slices, never NUL-terminated. Where Nuklear has a `_label`
     function taking a C string and a `_text` one taking a length, the layer
     has only the `_label` name, and passes the length on.
   * Pointers Nuklear keeps are kept by the layer instead: fonts' TTF data
     and glyph ranges, a layout row's ratios, cursors, scroll offsets a
     group writes back when it ends, the text a paste inserts. The program
     never lends memory beyond a call.
   * Nuklear's objects are handles: a context, a font atlas and its fonts, a
     text editor. Each is a slot index with a generation, so a destroyed one
     is an error to use, not a crash.
   * Callbacks are gone: text filters are picked from Nuklear's own set
     (FILTER_*), the clipboard is text the program hands over and takes
     back, and what Nuklear draws comes back as arrays -- its command list,
     or vertices, indices and draw calls to render with any GPU API.
   * Style is one K-shaped struct, read and written whole, in place of
     the pointers into nk_style that Nuklear's push and pop functions take.

   The layer has no GPU and no window: stdlib/ui.k renders what comes
   back through the gfx module, and feeds the context gfx's input. An image
   is any u32 a renderer understands, which for that renderer is a gfx
   texture handle.

   Errors: a function that can fail for reasons outside the program (a font
   file that is not there) returns false or a zero handle, with the reason
   in k_ui_error. A call the program should not have made (a widget outside
   a window, a group ended that was never begun, a key that does not exist,
   a stale handle, one of Nuklear's own asserts) is skipped, counted by
   k_ui_misuse_count, and turned into an abort by the K side at the
   next frame's input, render or check. Everything is main-thread only.

   The structs are packed and K-shaped: each is declared again in
   stdlib/ui.k with the same fields in the same order, `Rect` there
   being k_ui_rect here and a slice of `Color` k_ui_color_slice. */

#ifndef K_UI_API_H
#define K_UI_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* --- slices, vectors and handles ------------------------------------------ */

typedef struct { uint8_t *data; int64_t len; } k_ui_bytes;   /* u8[:], the generated sl_u8 */

typedef struct { int32_t x, y; } k_ui_int2;
typedef struct { float x, y; } k_ui_float2;
typedef struct { float x, y, z; } k_ui_float3;
typedef struct { float x, y, z, w; } k_ui_float4;

typedef struct { uint32_t id; } k_ui_context;
typedef struct { uint32_t id; } k_ui_font_atlas;
typedef struct { uint32_t id; } k_ui_font;
typedef struct { uint32_t id; } k_ui_text_edit;

typedef struct { float x, y, w, h; } k_ui_rect;
typedef struct { uint8_t r, g, b, a; } k_ui_color;
typedef struct { float r, g, b, a; } k_ui_colorf;
typedef struct { uint32_t x, y; } k_ui_scroll;

/* A texture, or a region of one: `texture` is whatever the renderer draws
   with (a gfx texture handle for stdlib/ui.k's), w and h its size and
   region x, y, w, h the part shown, for a subimage. */
typedef struct { uint32_t texture; uint16_t w, h; uint16_t region[4]; } k_ui_image;
/* An image drawn stretched in nine parts: l, t, r, b are the widths of the
   borders that are not stretched. */
typedef struct { k_ui_image img; uint16_t l, t, r, b; } k_ui_nine_slice;
/* A software cursor: an image with its size and hot spot. */
typedef struct { k_ui_image img; k_ui_float2 size, offset; } k_ui_cursor;

typedef struct { k_ui_float2 *data; int64_t len; } k_ui_float2_slice;
typedef struct { k_ui_int2 *data; int64_t len; } k_ui_int2_slice;
typedef struct { float *data; int64_t len; } k_ui_f32_slice;
typedef struct { uint32_t *data; int64_t len; } k_ui_u32_slice;
typedef struct { k_ui_color *data; int64_t len; } k_ui_color_slice;
typedef struct { k_ui_cursor *data; int64_t len; } k_ui_cursor_slice;
typedef struct { k_ui_font *data; int64_t len; } k_ui_font_slice;

/* --- fonts ----------------------------------------------------------------- */

/* How a font is baked into its atlas (nk_font_config, less its pointers). */
typedef struct {
    uint8_t oversample_h, oversample_v;     /* rasterized larger, for sub-pixel positions */
    uint8_t pixel_snap;                     /* glyphs on whole pixels */
    uint8_t merge_mode;                     /* glyphs added to the font added before */
    k_ui_float2 spacing;                   /* extra space between glyphs */
    int32_t ranges;                         /* a RANGE_* set of characters, unless given */
    uint32_t fallback_glyph;                /* drawn for a character the font lacks */
} k_ui_font_config;

typedef struct {
    float height, ascent, descent;
    int32_t glyph_count;
    uint32_t fallback_codepoint;
} k_ui_font_info;

typedef struct {
    uint32_t codepoint;
    float xadvance;
    float x0, y0, x1, y1, w, h;
    float u0, v0, u1, v1;
} k_ui_font_glyph;

/* --- input, output ---------------------------------------------------------- */

typedef struct {
    k_ui_float2 pos, prev, delta, scroll_delta;
    uint8_t grab, grabbed, ungrab;
} k_ui_mouse;

/* One command of what the widgets drew, flattened: the fields a kind has
   are the ones that mean something for it. */
typedef struct {
    int32_t kind;                   /* COMMAND_* */
    int32_t x, y, w, h;             /* scissor, rect, circle, image, text: the box; arc: its center and radius in x, y, w */
    int32_t line_thickness, rounding;
    k_ui_int2 points[4];           /* line: begin, end; curve: begin, two controls, end; triangle: its corners */
    float a_min, a_max;             /* arc: its angles */
    k_ui_color color;              /* stroke, fill or text color; image: its tint */
    k_ui_color background;         /* text: what it is drawn on */
    k_ui_color left, top, right, bottom;   /* rect_multi_color: each side's */
    int32_t first, count;           /* polygon, polyline: points in k_ui_command_points; text: bytes in k_ui_command_text */
    k_ui_image image;              /* image */
    k_ui_font font;                /* text */
    float height;                   /* text: the font's height */
} k_ui_command;
typedef struct { k_ui_command *data; int64_t len; } k_ui_command_slice;

/* How the commands become triangles (nk_convert_config, less its layout,
   which is always k_ui_vertex). */
typedef struct {
    float global_alpha;
    uint8_t line_aa, shape_aa;      /* anti-aliased edges */
    int32_t circle_segment_count, arc_segment_count, curve_segment_count;
} k_ui_convert_config;

typedef struct { k_ui_float2 pos, uv; k_ui_color color; } k_ui_vertex;
typedef struct { k_ui_vertex *data; int64_t len; } k_ui_vertex_slice;
/* elem_count indices from where the draw before it stopped, with this
   scissor and texture. */
typedef struct { uint32_t elem_count; k_ui_rect clip_rect; uint32_t texture; } k_ui_draw_command;
typedef struct { k_ui_draw_command *data; int64_t len; } k_ui_draw_command_slice;

typedef struct { int64_t begin, end, count; } k_ui_list_view;

typedef struct {
    int64_t cursor, select_start, select_end;   /* in characters */
    int64_t mode;                               /* TEXT_EDIT_MODE_* */
    int64_t length;                             /* in characters */
    uint8_t single_line, active;
} k_ui_text_edit_state;

/* --- style: nk_style and its parts, less callbacks, fonts and cursors ------- */

/* A background: a color, an image, a nine-slice (img with the borders l, t,
   r, b), or nothing, by `kind` (STYLE_ITEM_*). */
typedef struct {
    int32_t kind;
    k_ui_color color;
    k_ui_image img;
    uint16_t l, t, r, b;
} k_ui_style_item;

typedef struct {
    k_ui_color color;
    k_ui_float2 padding;
    float color_factor, disabled_factor;
} k_ui_style_text;

typedef struct {
    k_ui_color text_normal, text_hover, text_active;
    int32_t underline;
    float underline_thickness;
    k_ui_float2 padding, touch_padding;
    float color_factor, disabled_factor;
} k_ui_style_link;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    float color_factor_background;
    k_ui_color text_background, text_normal, text_hover, text_active;
    uint32_t text_alignment;
    float color_factor_text;
    float border, rounding;
    k_ui_float2 padding, image_padding, touch_padding;
    float disabled_factor;
} k_ui_style_button;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_style_item cursor_normal, cursor_hover;
    k_ui_color text_normal, text_hover, text_active, text_background;
    uint32_t text_alignment;
    k_ui_float2 padding, touch_padding;
    float spacing, border, color_factor, disabled_factor;
} k_ui_style_toggle;

typedef struct {
    k_ui_style_item normal, hover, pressed;
    k_ui_style_item normal_active, hover_active, pressed_active;
    k_ui_color text_normal, text_hover, text_pressed;
    k_ui_color text_normal_active, text_hover_active, text_pressed_active;
    k_ui_color text_background;
    uint32_t text_alignment;
    float rounding;
    k_ui_float2 padding, touch_padding, image_padding;
    float color_factor, disabled_factor;
} k_ui_style_selectable;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_color bar_normal, bar_hover, bar_active, bar_filled;
    k_ui_style_item cursor_normal, cursor_hover, cursor_active;
    float border, rounding, bar_height;
    k_ui_float2 padding, spacing, cursor_size;
    float color_factor, disabled_factor;
    uint8_t show_buttons;
    k_ui_style_button inc_button, dec_button;
    int32_t inc_symbol, dec_symbol;
} k_ui_style_slider;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_color knob_normal, knob_hover, knob_active, knob_border_color;
    k_ui_color cursor_normal, cursor_hover, cursor_active;
    float border, knob_border;
    k_ui_float2 padding, spacing;
    float cursor_width, color_factor, disabled_factor;
} k_ui_style_knob;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_style_item cursor_normal, cursor_hover, cursor_active;
    k_ui_color cursor_border_color;
    float rounding, border, cursor_border, cursor_rounding;
    k_ui_float2 padding;
    float color_factor, disabled_factor;
} k_ui_style_progress;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_style_item cursor_normal, cursor_hover, cursor_active;
    k_ui_color cursor_border_color;
    float border, rounding, border_cursor, rounding_cursor;
    k_ui_float2 padding;
    float color_factor, disabled_factor;
    uint8_t show_buttons;
    k_ui_style_button inc_button, dec_button;
    int32_t inc_symbol, dec_symbol;
} k_ui_style_scrollbar;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_style_scrollbar scrollbar;
    k_ui_color cursor_normal, cursor_hover, cursor_text_normal, cursor_text_hover;
    k_ui_color text_normal, text_hover, text_active;
    k_ui_color selected_normal, selected_hover, selected_text_normal, selected_text_hover;
    float border, rounding, cursor_size;
    k_ui_float2 scrollbar_size, padding;
    float row_padding, color_factor, disabled_factor;
} k_ui_style_edit;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_color label_normal, label_hover, label_active;
    int32_t sym_left, sym_right;
    float border, rounding;
    k_ui_float2 padding;
    float color_factor, disabled_factor;
    k_ui_style_edit edit;
    k_ui_style_button inc_button, dec_button;
} k_ui_style_property;

typedef struct {
    k_ui_style_item background;
    k_ui_color border_color, selected_color, color;
    float border, rounding;
    k_ui_float2 padding;
    float color_factor, disabled_factor;
    uint8_t show_markers;
} k_ui_style_chart;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_color border_color;
    k_ui_color label_normal, label_hover, label_active;
    k_ui_color symbol_normal, symbol_hover, symbol_active;
    k_ui_style_button button;
    int32_t sym_normal, sym_hover, sym_active;
    float border, rounding;
    k_ui_float2 content_padding, button_padding, spacing;
    float color_factor, disabled_factor;
} k_ui_style_combo;

typedef struct {
    k_ui_style_item background;
    k_ui_color border_color, text;
    k_ui_style_button tab_maximize_button, tab_minimize_button;
    k_ui_style_button node_maximize_button, node_minimize_button;
    int32_t sym_minimize, sym_maximize;
    float border, rounding, indent;
    k_ui_float2 padding, spacing;
    float color_factor, disabled_factor;
} k_ui_style_tab;

typedef struct {
    k_ui_style_item normal, hover, active;
    k_ui_style_button close_button, minimize_button;
    int32_t close_symbol, minimize_symbol, maximize_symbol;
    k_ui_color label_normal, label_hover, label_active;
    int32_t align;
    k_ui_float2 padding, label_padding, spacing;
} k_ui_style_window_header;

typedef struct {
    k_ui_style_window_header header;
    k_ui_style_item fixed_background;
    k_ui_color background;
    k_ui_color border_color, popup_border_color, combo_border_color, contextual_border_color;
    k_ui_color menu_border_color, group_border_color, tooltip_border_color;
    k_ui_style_item scaler;
    float border, combo_border, contextual_border, menu_border, group_border, tooltip_border;
    float popup_border, min_row_height_padding;
    float rounding;
    k_ui_float2 spacing, scrollbar_size, min_size;
    k_ui_float2 padding, group_padding, popup_padding, combo_padding, contextual_padding;
    k_ui_float2 menu_padding, tooltip_padding;
    int32_t tooltip_origin;
    k_ui_float2 tooltip_offset;
    float tooltip_delay;
} k_ui_style_window;

typedef struct {
    k_ui_style_text text;
    k_ui_style_link link;
    k_ui_style_button button, contextual_button, menu_button;
    k_ui_style_toggle option, checkbox;
    k_ui_style_selectable selectable;
    k_ui_style_slider slider;
    k_ui_style_knob knob;
    k_ui_style_progress progress;
    k_ui_style_property property;
    k_ui_style_edit edit;
    k_ui_style_chart chart;
    k_ui_style_scrollbar scrollh, scrollv;
    k_ui_style_tab tab;
    k_ui_style_combo combo;
    k_ui_style_window window;
} k_ui_style;

#pragma pack(pop)

#define K_UI_API(X) \
    /* The library. */ \
    X(uint8_t, k_ui_available, (void)) \
    X(int64_t, k_ui_error, (k_ui_bytes out)) \
    X(int64_t, k_ui_misuse_count, (void)) \
    /* Font atlases and fonts. */ \
    X(k_ui_font_atlas, k_ui_create_font_atlas, (void)) \
    X(void, k_ui_destroy_font_atlas, (k_ui_font_atlas a)) \
    X(uint8_t, k_ui_font_atlas_is_valid, (k_ui_font_atlas a)) \
    X(k_ui_font_config, k_ui_default_font_config, (void)) \
    X(k_ui_font, k_ui_add_default_font, (k_ui_font_atlas a, float height, k_ui_font_config config, k_ui_u32_slice ranges)) \
    X(k_ui_font, k_ui_add_font_from_memory, (k_ui_font_atlas a, k_ui_bytes ttf, float height, k_ui_font_config config, k_ui_u32_slice ranges)) \
    X(k_ui_font, k_ui_add_font_from_file, (k_ui_font_atlas a, k_ui_bytes path, float height, k_ui_font_config config, k_ui_u32_slice ranges)) \
    X(uint8_t, k_ui_bake_font_atlas, (k_ui_font_atlas a, float scale)) \
    X(uint8_t, k_ui_font_atlas_is_baked, (k_ui_font_atlas a)) \
    X(void, k_ui_font_atlas_size, (k_ui_font_atlas a, k_ui_int2 *out)) \
    X(int64_t, k_ui_font_atlas_pixels, (k_ui_font_atlas a, k_ui_bytes out)) \
    X(void, k_ui_set_font_atlas_texture, (k_ui_font_atlas a, uint32_t texture)) \
    X(uint32_t, k_ui_font_atlas_texture, (k_ui_font_atlas a)) \
    X(k_ui_font_atlas, k_ui_font_atlas_without_texture, (void)) \
    X(void, k_ui_forget_font_atlas_textures, (void)) \
    X(int64_t, k_ui_released_textures, (k_ui_u32_slice out)) \
    X(int64_t, k_ui_font_atlas_fonts, (k_ui_font_atlas a, k_ui_font_slice out)) \
    X(k_ui_font_atlas, k_ui_font_atlas_of, (k_ui_font f)) \
    X(uint8_t, k_ui_font_is_valid, (k_ui_font f)) \
    X(k_ui_font_info, k_ui_font_info_of, (k_ui_font f)) \
    X(uint8_t, k_ui_font_find_glyph, (k_ui_font f, uint32_t codepoint, k_ui_font_glyph *out)) \
    X(float, k_ui_font_text_width, (k_ui_font f, k_ui_bytes text)) \
    X(int64_t, k_ui_font_glyph_ranges, (int64_t which, k_ui_u32_slice out)) \
    /* Contexts. */ \
    X(k_ui_context, k_ui_create_context, (k_ui_font font)) \
    X(k_ui_context, k_ui_create_default_context, (float font_height)) \
    X(k_ui_context, k_ui_create_context_from_file, (k_ui_bytes path, float font_height)) \
    X(k_ui_context, k_ui_create_context_from_memory, (k_ui_bytes ttf, float font_height)) \
    X(k_ui_font_atlas, k_ui_context_atlas, (k_ui_context c)) \
    X(void, k_ui_destroy_context, (k_ui_context c)) \
    X(uint8_t, k_ui_context_is_valid, (k_ui_context c)) \
    X(void, k_ui_clear, (k_ui_context c)) \
    X(void, k_ui_set_delta_time, (k_ui_context c, float seconds)) \
    X(float, k_ui_delta_time, (k_ui_context c)) \
    X(k_ui_font, k_ui_context_font, (k_ui_context c)) \
    X(void, k_ui_set_clipboard, (k_ui_context c, k_ui_bytes text)) \
    X(int64_t, k_ui_copied, (k_ui_context c, k_ui_bytes out)) \
    X(uint32_t, k_ui_null_texture, (k_ui_context c)) \
    X(uint8_t, k_ui_set_scale, (k_ui_context c, float scale)) \
    X(float, k_ui_scale, (k_ui_context c)) \
    /* Input. */ \
    X(void, k_ui_input_begin, (k_ui_context c)) \
    X(void, k_ui_input_motion, (k_ui_context c, int64_t x, int64_t y)) \
    X(void, k_ui_input_key, (k_ui_context c, int64_t key, uint8_t down)) \
    X(void, k_ui_input_button, (k_ui_context c, int64_t button, int64_t x, int64_t y, uint8_t down)) \
    X(void, k_ui_input_scroll, (k_ui_context c, k_ui_float2 amount)) \
    X(void, k_ui_input_char, (k_ui_context c, uint8_t ch)) \
    X(void, k_ui_input_glyph, (k_ui_context c, k_ui_bytes utf8)) \
    X(void, k_ui_input_unicode, (k_ui_context c, uint32_t rune)) \
    X(void, k_ui_input_end, (k_ui_context c)) \
    X(k_ui_mouse, k_ui_input_mouse, (k_ui_context c)) \
    X(uint8_t, k_ui_input_has_mouse_click, (k_ui_context c, int64_t button)) \
    X(uint8_t, k_ui_input_has_mouse_click_in_rect, (k_ui_context c, int64_t button, k_ui_rect r)) \
    X(uint8_t, k_ui_input_has_mouse_click_in_button_rect, (k_ui_context c, int64_t button, k_ui_rect r)) \
    X(uint8_t, k_ui_input_has_mouse_click_down_in_rect, (k_ui_context c, int64_t button, k_ui_rect r, uint8_t down)) \
    X(uint8_t, k_ui_input_is_mouse_click_in_rect, (k_ui_context c, int64_t button, k_ui_rect r)) \
    X(uint8_t, k_ui_input_is_mouse_click_down_in_rect, (k_ui_context c, int64_t button, k_ui_rect r, uint8_t down)) \
    X(uint8_t, k_ui_input_any_mouse_click_in_rect, (k_ui_context c, k_ui_rect r)) \
    X(uint8_t, k_ui_input_is_mouse_prev_hovering_rect, (k_ui_context c, k_ui_rect r)) \
    X(uint8_t, k_ui_input_is_mouse_hovering_rect, (k_ui_context c, k_ui_rect r)) \
    X(uint8_t, k_ui_input_is_mouse_hovering_still_rect, (k_ui_context c, k_ui_rect r)) \
    X(uint8_t, k_ui_input_is_mouse_hovering_delay_rect, (k_ui_context c, k_ui_rect r, float *timer, float delay)) \
    X(uint8_t, k_ui_input_is_mouse_hovering_still_delay_rect, (k_ui_context c, k_ui_rect r, float *timer, float delay)) \
    X(uint8_t, k_ui_input_is_mouse_hovering_still_delay_clicked_rect, (k_ui_context c, k_ui_rect r, float *timer, float delay, uint8_t *clicked)) \
    X(uint8_t, k_ui_input_is_mouse_moved, (k_ui_context c)) \
    X(uint8_t, k_ui_input_mouse_clicked, (k_ui_context c, int64_t button, k_ui_rect r)) \
    X(uint8_t, k_ui_input_is_mouse_down, (k_ui_context c, int64_t button)) \
    X(uint8_t, k_ui_input_is_mouse_pressed, (k_ui_context c, int64_t button)) \
    X(uint8_t, k_ui_input_is_mouse_released, (k_ui_context c, int64_t button)) \
    X(uint8_t, k_ui_input_is_key_pressed, (k_ui_context c, int64_t key)) \
    X(uint8_t, k_ui_input_is_key_released, (k_ui_context c, int64_t key)) \
    X(uint8_t, k_ui_input_is_key_down, (k_ui_context c, int64_t key)) \
    /* What was drawn: the command list, or triangles. */ \
    X(int64_t, k_ui_commands, (k_ui_context c, k_ui_command_slice out)) \
    X(int64_t, k_ui_command_points, (k_ui_context c, k_ui_int2_slice out)) \
    X(int64_t, k_ui_command_text, (k_ui_context c, k_ui_bytes out)) \
    X(int64_t, k_ui_convert, (k_ui_context c, const k_ui_convert_config *config)) \
    X(int64_t, k_ui_vertices, (k_ui_context c, k_ui_vertex_slice out)) \
    X(int64_t, k_ui_indices, (k_ui_context c, k_ui_u32_slice out)) \
    X(int64_t, k_ui_draw_commands, (k_ui_context c, k_ui_draw_command_slice out)) \
    /* Windows. */ \
    X(uint8_t, k_ui_begin, (k_ui_context c, k_ui_bytes title, k_ui_rect bounds, int64_t flags)) \
    X(uint8_t, k_ui_begin_titled, (k_ui_context c, k_ui_bytes name, k_ui_bytes title, k_ui_rect bounds, int64_t flags)) \
    X(void, k_ui_end, (k_ui_context c)) \
    X(uint8_t, k_ui_window_find, (k_ui_context c, k_ui_bytes name)) \
    X(k_ui_rect, k_ui_window_get_bounds, (k_ui_context c)) \
    X(k_ui_float2, k_ui_window_get_position, (k_ui_context c)) \
    X(k_ui_float2, k_ui_window_get_size, (k_ui_context c)) \
    X(float, k_ui_window_get_width, (k_ui_context c)) \
    X(float, k_ui_window_get_height, (k_ui_context c)) \
    X(k_ui_rect, k_ui_window_get_content_region, (k_ui_context c)) \
    X(k_ui_float2, k_ui_window_get_content_region_min, (k_ui_context c)) \
    X(k_ui_float2, k_ui_window_get_content_region_max, (k_ui_context c)) \
    X(k_ui_float2, k_ui_window_get_content_region_size, (k_ui_context c)) \
    X(k_ui_scroll, k_ui_window_get_scroll, (k_ui_context c)) \
    X(uint8_t, k_ui_window_has_focus, (k_ui_context c)) \
    X(uint8_t, k_ui_window_is_hovered, (k_ui_context c)) \
    X(uint8_t, k_ui_window_is_collapsed, (k_ui_context c, k_ui_bytes name)) \
    X(uint8_t, k_ui_window_is_closed, (k_ui_context c, k_ui_bytes name)) \
    X(uint8_t, k_ui_window_is_hidden, (k_ui_context c, k_ui_bytes name)) \
    X(uint8_t, k_ui_window_is_active, (k_ui_context c, k_ui_bytes name)) \
    X(uint8_t, k_ui_window_is_any_hovered, (k_ui_context c)) \
    X(uint8_t, k_ui_item_is_any_active, (k_ui_context c)) \
    X(uint8_t, k_ui_wants_keyboard, (k_ui_context c)) \
    X(uint8_t, k_ui_wants_mouse, (k_ui_context c)) \
    X(void, k_ui_window_set_bounds, (k_ui_context c, k_ui_bytes name, k_ui_rect bounds)) \
    X(void, k_ui_window_set_position, (k_ui_context c, k_ui_bytes name, k_ui_float2 pos)) \
    X(void, k_ui_window_set_size, (k_ui_context c, k_ui_bytes name, k_ui_float2 size)) \
    X(void, k_ui_window_set_focus, (k_ui_context c, k_ui_bytes name)) \
    X(void, k_ui_window_set_scroll, (k_ui_context c, int64_t x, int64_t y)) \
    X(void, k_ui_window_close, (k_ui_context c, k_ui_bytes name)) \
    X(void, k_ui_window_collapse, (k_ui_context c, k_ui_bytes name, int64_t state)) \
    X(void, k_ui_window_collapse_if, (k_ui_context c, k_ui_bytes name, int64_t state, uint8_t cond)) \
    X(void, k_ui_window_show, (k_ui_context c, k_ui_bytes name, int64_t state)) \
    X(void, k_ui_window_show_if, (k_ui_context c, k_ui_bytes name, int64_t state, uint8_t cond)) \
    X(void, k_ui_rule_horizontal, (k_ui_context c, k_ui_color color, uint8_t rounding)) \
    /* Layout. */ \
    X(void, k_ui_layout_set_min_row_height, (k_ui_context c, float height)) \
    X(void, k_ui_layout_reset_min_row_height, (k_ui_context c)) \
    X(k_ui_rect, k_ui_layout_widget_bounds, (k_ui_context c)) \
    X(float, k_ui_layout_ratio_from_pixel, (k_ui_context c, float pixel_width)) \
    X(void, k_ui_layout_row_dynamic, (k_ui_context c, float height, int64_t cols)) \
    X(void, k_ui_layout_row_static, (k_ui_context c, float height, int64_t item_width, int64_t cols)) \
    X(void, k_ui_layout_row_begin, (k_ui_context c, int64_t format, float row_height, int64_t cols)) \
    X(void, k_ui_layout_row_push, (k_ui_context c, float value)) \
    X(void, k_ui_layout_row_end, (k_ui_context c)) \
    X(void, k_ui_layout_row, (k_ui_context c, int64_t format, float height, k_ui_f32_slice ratios)) \
    X(void, k_ui_layout_row_template_begin, (k_ui_context c, float row_height)) \
    X(void, k_ui_layout_row_template_push_dynamic, (k_ui_context c)) \
    X(void, k_ui_layout_row_template_push_variable, (k_ui_context c, float min_width)) \
    X(void, k_ui_layout_row_template_push_static, (k_ui_context c, float width)) \
    X(void, k_ui_layout_row_template_end, (k_ui_context c)) \
    X(void, k_ui_layout_space_begin, (k_ui_context c, int64_t format, float height, int64_t widget_count)) \
    X(void, k_ui_layout_space_push, (k_ui_context c, k_ui_rect bounds)) \
    X(void, k_ui_layout_space_end, (k_ui_context c)) \
    X(k_ui_rect, k_ui_layout_space_bounds, (k_ui_context c)) \
    X(k_ui_float2, k_ui_layout_space_to_screen, (k_ui_context c, k_ui_float2 v)) \
    X(k_ui_float2, k_ui_layout_space_to_local, (k_ui_context c, k_ui_float2 v)) \
    X(k_ui_rect, k_ui_layout_space_rect_to_screen, (k_ui_context c, k_ui_rect r)) \
    X(k_ui_rect, k_ui_layout_space_rect_to_local, (k_ui_context c, k_ui_rect r)) \
    X(void, k_ui_spacer, (k_ui_context c)) \
    /* Groups. */ \
    X(uint8_t, k_ui_group_begin, (k_ui_context c, k_ui_bytes title, int64_t flags)) \
    X(uint8_t, k_ui_group_begin_titled, (k_ui_context c, k_ui_bytes id, k_ui_bytes title, int64_t flags)) \
    X(void, k_ui_group_end, (k_ui_context c)) \
    X(uint8_t, k_ui_group_scrolled_begin, (k_ui_context c, k_ui_scroll offset, k_ui_bytes title, int64_t flags)) \
    X(k_ui_scroll, k_ui_group_scrolled_end, (k_ui_context c)) \
    X(k_ui_scroll, k_ui_group_get_scroll, (k_ui_context c, k_ui_bytes id)) \
    X(void, k_ui_group_set_scroll, (k_ui_context c, k_ui_bytes id, int64_t x, int64_t y)) \
    /* Trees. */ \
    X(uint8_t, k_ui_tree_push_hashed, (k_ui_context c, int64_t type, k_ui_bytes title, int64_t initial_state, k_ui_bytes hash, int64_t seed)) \
    X(uint8_t, k_ui_tree_image_push_hashed, (k_ui_context c, int64_t type, k_ui_image img, k_ui_bytes title, int64_t initial_state, k_ui_bytes hash, int64_t seed)) \
    X(void, k_ui_tree_pop, (k_ui_context c)) \
    X(uint8_t, k_ui_tree_state_push, (k_ui_context c, int64_t type, k_ui_bytes title, int64_t *state)) \
    X(uint8_t, k_ui_tree_state_image_push, (k_ui_context c, int64_t type, k_ui_image img, k_ui_bytes title, int64_t *state)) \
    X(void, k_ui_tree_state_pop, (k_ui_context c)) \
    X(uint8_t, k_ui_tree_element_push_hashed, (k_ui_context c, int64_t type, k_ui_bytes title, int64_t initial_state, uint8_t *selected, k_ui_bytes hash, int64_t seed)) \
    X(uint8_t, k_ui_tree_element_image_push_hashed, (k_ui_context c, int64_t type, k_ui_image img, k_ui_bytes title, int64_t initial_state, uint8_t *selected, k_ui_bytes hash, int64_t seed)) \
    X(void, k_ui_tree_element_pop, (k_ui_context c)) \
    /* List views. */ \
    X(uint8_t, k_ui_list_view_begin, (k_ui_context c, k_ui_list_view *out, k_ui_bytes id, int64_t flags, int64_t row_height, int64_t row_count)) \
    X(void, k_ui_list_view_end, (k_ui_context c)) \
    /* Widgets in general. */ \
    X(int64_t, k_ui_widget, (k_ui_context c, k_ui_rect *bounds)) \
    X(k_ui_rect, k_ui_widget_bounds, (k_ui_context c)) \
    X(k_ui_float2, k_ui_widget_position, (k_ui_context c)) \
    X(k_ui_float2, k_ui_widget_size, (k_ui_context c)) \
    X(float, k_ui_widget_width, (k_ui_context c)) \
    X(float, k_ui_widget_height, (k_ui_context c)) \
    X(uint8_t, k_ui_widget_is_hovered, (k_ui_context c)) \
    X(uint8_t, k_ui_widget_is_mouse_clicked, (k_ui_context c, int64_t button)) \
    X(uint8_t, k_ui_widget_has_mouse_click_down, (k_ui_context c, int64_t button, uint8_t down)) \
    X(void, k_ui_spacing, (k_ui_context c, int64_t cols)) \
    X(void, k_ui_widget_disable_begin, (k_ui_context c)) \
    X(void, k_ui_widget_disable_end, (k_ui_context c)) \
    /* Text and images. */ \
    X(void, k_ui_label, (k_ui_context c, k_ui_bytes text, int64_t align)) \
    X(void, k_ui_label_colored, (k_ui_context c, k_ui_bytes text, int64_t align, k_ui_color color)) \
    X(void, k_ui_label_wrap, (k_ui_context c, k_ui_bytes text)) \
    X(void, k_ui_label_colored_wrap, (k_ui_context c, k_ui_bytes text, k_ui_color color)) \
    X(void, k_ui_image_widget, (k_ui_context c, k_ui_image img)) \
    X(void, k_ui_image_color, (k_ui_context c, k_ui_image img, k_ui_color color)) \
    X(void, k_ui_value_bool, (k_ui_context c, k_ui_bytes prefix, uint8_t value)) \
    X(void, k_ui_value_int, (k_ui_context c, k_ui_bytes prefix, int64_t value)) \
    X(void, k_ui_value_uint, (k_ui_context c, k_ui_bytes prefix, uint64_t value)) \
    X(void, k_ui_value_float, (k_ui_context c, k_ui_bytes prefix, float value)) \
    X(void, k_ui_value_color_byte, (k_ui_context c, k_ui_bytes prefix, k_ui_color color)) \
    X(void, k_ui_value_color_float, (k_ui_context c, k_ui_bytes prefix, k_ui_color color)) \
    X(void, k_ui_value_color_hex, (k_ui_context c, k_ui_bytes prefix, k_ui_color color)) \
    X(uint8_t, k_ui_link_label, (k_ui_context c, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_link_label_colored, (k_ui_context c, k_ui_bytes text, int64_t align, k_ui_color color)) \
    X(uint8_t, k_ui_link_label_underline, (k_ui_context c, k_ui_bytes text, int64_t align, int64_t underline)) \
    X(uint8_t, k_ui_link_label_styled, (k_ui_context c, const k_ui_style_link *style, k_ui_bytes text, int64_t align)) \
    /* Buttons. */ \
    X(uint8_t, k_ui_button_label, (k_ui_context c, k_ui_bytes text)) \
    X(uint8_t, k_ui_button_color, (k_ui_context c, k_ui_color color)) \
    X(uint8_t, k_ui_button_symbol, (k_ui_context c, int64_t symbol)) \
    X(uint8_t, k_ui_button_image, (k_ui_context c, k_ui_image img)) \
    X(uint8_t, k_ui_button_symbol_label, (k_ui_context c, int64_t symbol, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_button_image_label, (k_ui_context c, k_ui_image img, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_button_label_styled, (k_ui_context c, const k_ui_style_button *style, k_ui_bytes text)) \
    X(uint8_t, k_ui_button_symbol_styled, (k_ui_context c, const k_ui_style_button *style, int64_t symbol)) \
    X(uint8_t, k_ui_button_image_styled, (k_ui_context c, const k_ui_style_button *style, k_ui_image img)) \
    X(uint8_t, k_ui_button_symbol_label_styled, (k_ui_context c, const k_ui_style_button *style, int64_t symbol, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_button_image_label_styled, (k_ui_context c, const k_ui_style_button *style, k_ui_image img, k_ui_bytes text, int64_t align)) \
    X(void, k_ui_button_set_behavior, (k_ui_context c, int64_t behavior)) \
    X(uint8_t, k_ui_button_push_behavior, (k_ui_context c, int64_t behavior)) \
    X(uint8_t, k_ui_button_pop_behavior, (k_ui_context c)) \
    /* Checkboxes, radio buttons and selectables. */ \
    X(uint8_t, k_ui_check_label, (k_ui_context c, k_ui_bytes text, uint8_t active)) \
    X(uint8_t, k_ui_check_label_align, (k_ui_context c, k_ui_bytes text, uint8_t active, int64_t widget_align, int64_t text_align)) \
    X(int64_t, k_ui_check_flags_label, (k_ui_context c, k_ui_bytes text, int64_t flags, int64_t value)) \
    X(uint8_t, k_ui_checkbox_label, (k_ui_context c, k_ui_bytes text, uint8_t *active)) \
    X(uint8_t, k_ui_checkbox_label_align, (k_ui_context c, k_ui_bytes text, uint8_t *active, int64_t widget_align, int64_t text_align)) \
    X(uint8_t, k_ui_checkbox_flags_label, (k_ui_context c, k_ui_bytes text, int64_t *flags, int64_t value)) \
    X(uint8_t, k_ui_radio_label, (k_ui_context c, k_ui_bytes text, uint8_t *active)) \
    X(uint8_t, k_ui_radio_label_align, (k_ui_context c, k_ui_bytes text, uint8_t *active, int64_t widget_align, int64_t text_align)) \
    X(uint8_t, k_ui_option_label, (k_ui_context c, k_ui_bytes text, uint8_t active)) \
    X(uint8_t, k_ui_option_label_align, (k_ui_context c, k_ui_bytes text, uint8_t active, int64_t widget_align, int64_t text_align)) \
    X(uint8_t, k_ui_selectable_label, (k_ui_context c, k_ui_bytes text, int64_t align, uint8_t *value)) \
    X(uint8_t, k_ui_selectable_image_label, (k_ui_context c, k_ui_image img, k_ui_bytes text, int64_t align, uint8_t *value)) \
    X(uint8_t, k_ui_selectable_symbol_label, (k_ui_context c, int64_t symbol, k_ui_bytes text, int64_t align, uint8_t *value)) \
    X(uint8_t, k_ui_select_label, (k_ui_context c, k_ui_bytes text, int64_t align, uint8_t value)) \
    X(uint8_t, k_ui_select_image_label, (k_ui_context c, k_ui_image img, k_ui_bytes text, int64_t align, uint8_t value)) \
    X(uint8_t, k_ui_select_symbol_label, (k_ui_context c, int64_t symbol, k_ui_bytes text, int64_t align, uint8_t value)) \
    /* Sliders, knobs, progress bars and color pickers. */ \
    X(float, k_ui_slide_float, (k_ui_context c, float min, float value, float max, float step)) \
    X(int64_t, k_ui_slide_int, (k_ui_context c, int64_t min, int64_t value, int64_t max, int64_t step)) \
    X(uint8_t, k_ui_slider_float, (k_ui_context c, float min, float *value, float max, float step)) \
    X(uint8_t, k_ui_slider_int, (k_ui_context c, int64_t min, int64_t *value, int64_t max, int64_t step)) \
    X(uint8_t, k_ui_knob_float, (k_ui_context c, float min, float *value, float max, float step, int64_t zero_direction, float dead_zone_degrees)) \
    X(uint8_t, k_ui_knob_int, (k_ui_context c, int64_t min, int64_t *value, int64_t max, int64_t step, int64_t zero_direction, float dead_zone_degrees)) \
    X(uint8_t, k_ui_progress, (k_ui_context c, int64_t *cur, int64_t max, uint8_t modifiable)) \
    X(int64_t, k_ui_prog, (k_ui_context c, int64_t cur, int64_t max, uint8_t modifiable)) \
    X(k_ui_colorf, k_ui_color_picker, (k_ui_context c, k_ui_colorf color, int64_t format)) \
    X(uint8_t, k_ui_color_pick, (k_ui_context c, k_ui_colorf *color, int64_t format)) \
    /* Properties: a number dragged, stepped or typed. */ \
    X(uint8_t, k_ui_property_int, (k_ui_context c, k_ui_bytes name, int64_t min, int64_t *value, int64_t max, int64_t step, float inc_per_pixel)) \
    X(uint8_t, k_ui_property_float, (k_ui_context c, k_ui_bytes name, float min, float *value, float max, float step, float inc_per_pixel)) \
    X(uint8_t, k_ui_property_double, (k_ui_context c, k_ui_bytes name, double min, double *value, double max, double step, float inc_per_pixel)) \
    X(int64_t, k_ui_propertyi, (k_ui_context c, k_ui_bytes name, int64_t min, int64_t value, int64_t max, int64_t step, float inc_per_pixel)) \
    X(float, k_ui_propertyf, (k_ui_context c, k_ui_bytes name, float min, float value, float max, float step, float inc_per_pixel)) \
    X(double, k_ui_propertyd, (k_ui_context c, k_ui_bytes name, double min, double value, double max, double step, float inc_per_pixel)) \
    /* Text editing. */ \
    X(int64_t, k_ui_edit_string, (k_ui_context c, int64_t flags, k_ui_bytes buffer, int64_t *len, int64_t filter)) \
    X(int64_t, k_ui_edit_buffer, (k_ui_context c, int64_t flags, k_ui_text_edit edit, int64_t filter)) \
    X(void, k_ui_edit_focus, (k_ui_context c, int64_t flags)) \
    X(void, k_ui_edit_unfocus, (k_ui_context c)) \
    X(uint8_t, k_ui_filter_accepts, (int64_t filter, uint32_t rune)) \
    X(k_ui_text_edit, k_ui_create_text_edit, (void)) \
    X(void, k_ui_destroy_text_edit, (k_ui_text_edit e)) \
    X(uint8_t, k_ui_text_edit_is_valid, (k_ui_text_edit e)) \
    X(int64_t, k_ui_text_edit_text, (k_ui_text_edit e, k_ui_bytes out)) \
    X(void, k_ui_text_edit_set_text, (k_ui_text_edit e, k_ui_bytes text)) \
    X(k_ui_text_edit_state, k_ui_text_edit_state_of, (k_ui_text_edit e)) \
    X(void, k_ui_text_edit_set_cursor, (k_ui_text_edit e, int64_t cursor, int64_t select_start, int64_t select_end)) \
    X(void, k_ui_text_edit_set_mode, (k_ui_text_edit e, int64_t mode)) \
    X(void, k_ui_textedit_text, (k_ui_text_edit e, k_ui_bytes text)) \
    X(void, k_ui_textedit_delete, (k_ui_text_edit e, int64_t where, int64_t len)) \
    X(void, k_ui_textedit_delete_selection, (k_ui_text_edit e)) \
    X(void, k_ui_textedit_select_all, (k_ui_text_edit e)) \
    X(uint8_t, k_ui_textedit_cut, (k_ui_text_edit e)) \
    X(uint8_t, k_ui_textedit_paste, (k_ui_text_edit e, k_ui_bytes text)) \
    X(void, k_ui_textedit_undo, (k_ui_text_edit e)) \
    X(void, k_ui_textedit_redo, (k_ui_text_edit e)) \
    /* Charts. */ \
    X(uint8_t, k_ui_chart_begin, (k_ui_context c, int64_t type, int64_t count, float min, float max)) \
    X(uint8_t, k_ui_chart_begin_colored, (k_ui_context c, int64_t type, k_ui_color color, k_ui_color active, int64_t count, float min, float max)) \
    X(void, k_ui_chart_add_slot, (k_ui_context c, int64_t type, int64_t count, float min, float max)) \
    X(void, k_ui_chart_add_slot_colored, (k_ui_context c, int64_t type, k_ui_color color, k_ui_color active, int64_t count, float min, float max)) \
    X(int64_t, k_ui_chart_push, (k_ui_context c, float value)) \
    X(int64_t, k_ui_chart_push_slot, (k_ui_context c, float value, int64_t slot)) \
    X(void, k_ui_chart_end, (k_ui_context c)) \
    X(void, k_ui_plot, (k_ui_context c, int64_t type, k_ui_f32_slice values)) \
    /* Popups. */ \
    X(uint8_t, k_ui_popup_begin, (k_ui_context c, int64_t type, k_ui_bytes title, int64_t flags, k_ui_rect rect)) \
    X(void, k_ui_popup_close, (k_ui_context c)) \
    X(void, k_ui_popup_end, (k_ui_context c)) \
    X(k_ui_scroll, k_ui_popup_get_scroll, (k_ui_context c)) \
    X(void, k_ui_popup_set_scroll, (k_ui_context c, int64_t x, int64_t y)) \
    /* Combo boxes. */ \
    X(int64_t, k_ui_combo_separator, (k_ui_context c, k_ui_bytes items, int64_t separator, int64_t selected, int64_t count, int64_t item_height, k_ui_float2 size)) \
    X(uint8_t, k_ui_combobox_separator, (k_ui_context c, k_ui_bytes items, int64_t separator, int64_t *selected, int64_t count, int64_t item_height, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_begin_label, (k_ui_context c, k_ui_bytes selected, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_begin_color, (k_ui_context c, k_ui_color color, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_begin_symbol, (k_ui_context c, int64_t symbol, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_begin_symbol_label, (k_ui_context c, k_ui_bytes selected, int64_t symbol, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_begin_image, (k_ui_context c, k_ui_image img, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_begin_image_label, (k_ui_context c, k_ui_bytes selected, k_ui_image img, k_ui_float2 size)) \
    X(uint8_t, k_ui_combo_item_label, (k_ui_context c, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_combo_item_image_label, (k_ui_context c, k_ui_image img, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_combo_item_symbol_label, (k_ui_context c, int64_t symbol, k_ui_bytes text, int64_t align)) \
    X(void, k_ui_combo_close, (k_ui_context c)) \
    X(void, k_ui_combo_end, (k_ui_context c)) \
    /* Contextual menus. */ \
    X(uint8_t, k_ui_contextual_begin, (k_ui_context c, int64_t flags, k_ui_float2 size, k_ui_rect trigger_bounds)) \
    X(uint8_t, k_ui_contextual_item_label, (k_ui_context c, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_contextual_item_image_label, (k_ui_context c, k_ui_image img, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_contextual_item_symbol_label, (k_ui_context c, int64_t symbol, k_ui_bytes text, int64_t align)) \
    X(void, k_ui_contextual_close, (k_ui_context c)) \
    X(void, k_ui_contextual_end, (k_ui_context c)) \
    /* Tooltips. */ \
    X(void, k_ui_tooltip, (k_ui_context c, k_ui_bytes text)) \
    X(void, k_ui_tooltip_offset, (k_ui_context c, k_ui_bytes text, int64_t position, k_ui_float2 offset)) \
    X(uint8_t, k_ui_tooltip_begin, (k_ui_context c, float width)) \
    X(uint8_t, k_ui_tooltip_begin_offset, (k_ui_context c, float width, int64_t position, k_ui_float2 offset)) \
    X(void, k_ui_tooltip_end, (k_ui_context c)) \
    X(void, k_ui_do_tooltip, (k_ui_context c, k_ui_bytes text, k_ui_rect bounds)) \
    X(void, k_ui_do_tooltip_delay, (k_ui_context c, k_ui_bytes text, k_ui_rect bounds, float *timer)) \
    X(void, k_ui_do_tooltip_delay_clicked, (k_ui_context c, k_ui_bytes text, k_ui_rect bounds, float *timer, uint8_t *clicked)) \
    /* Menus. */ \
    X(void, k_ui_menubar_begin, (k_ui_context c)) \
    X(void, k_ui_menubar_end, (k_ui_context c)) \
    X(uint8_t, k_ui_menu_begin_label, (k_ui_context c, k_ui_bytes text, int64_t align, k_ui_float2 size)) \
    X(uint8_t, k_ui_menu_begin_image, (k_ui_context c, k_ui_bytes id, k_ui_image img, k_ui_float2 size)) \
    X(uint8_t, k_ui_menu_begin_image_label, (k_ui_context c, k_ui_bytes text, int64_t align, k_ui_image img, k_ui_float2 size)) \
    X(uint8_t, k_ui_menu_begin_symbol, (k_ui_context c, k_ui_bytes id, int64_t symbol, k_ui_float2 size)) \
    X(uint8_t, k_ui_menu_begin_symbol_label, (k_ui_context c, k_ui_bytes text, int64_t align, int64_t symbol, k_ui_float2 size)) \
    X(uint8_t, k_ui_menu_item_label, (k_ui_context c, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_menu_item_image_label, (k_ui_context c, k_ui_image img, k_ui_bytes text, int64_t align)) \
    X(uint8_t, k_ui_menu_item_symbol_label, (k_ui_context c, int64_t symbol, k_ui_bytes text, int64_t align)) \
    X(void, k_ui_menu_close, (k_ui_context c)) \
    X(void, k_ui_menu_end, (k_ui_context c)) \
    /* Style. */ \
    X(k_ui_style, k_ui_style_of, (k_ui_context c)) \
    X(void, k_ui_set_style, (k_ui_context c, const k_ui_style *style)) \
    X(uint8_t, k_ui_push_style, (k_ui_context c, const k_ui_style *style)) \
    X(uint8_t, k_ui_pop_style, (k_ui_context c)) \
    X(void, k_ui_style_default, (k_ui_context c)) \
    X(void, k_ui_style_from_table, (k_ui_context c, k_ui_color_slice table)) \
    X(int64_t, k_ui_default_color_table, (k_ui_color_slice out)) \
    X(int64_t, k_ui_style_get_color_by_name, (int64_t color, k_ui_bytes out)) \
    X(void, k_ui_style_set_font, (k_ui_context c, k_ui_font font)) \
    X(uint8_t, k_ui_style_push_font, (k_ui_context c, k_ui_font font)) \
    X(uint8_t, k_ui_style_pop_font, (k_ui_context c)) \
    X(void, k_ui_style_load_cursor, (k_ui_context c, int64_t cursor, k_ui_cursor image)) \
    X(void, k_ui_style_load_all_cursors, (k_ui_context c, k_ui_cursor_slice cursors)) \
    X(uint8_t, k_ui_style_set_cursor, (k_ui_context c, int64_t cursor)) \
    X(void, k_ui_style_show_cursor, (k_ui_context c)) \
    X(void, k_ui_style_hide_cursor, (k_ui_context c)) \
    /* Colors. */ \
    X(k_ui_color, k_ui_rgb, (int64_t r, int64_t g, int64_t b)) \
    X(k_ui_color, k_ui_rgba, (int64_t r, int64_t g, int64_t b, int64_t a)) \
    X(k_ui_color, k_ui_rgba_u32, (uint32_t rgba)) \
    X(k_ui_color, k_ui_rgb_f, (float r, float g, float b)) \
    X(k_ui_color, k_ui_rgba_f, (float r, float g, float b, float a)) \
    X(k_ui_color, k_ui_rgb_cf, (k_ui_colorf c)) \
    X(k_ui_color, k_ui_rgba_cf, (k_ui_colorf c)) \
    X(k_ui_color, k_ui_rgb_hex, (k_ui_bytes text)) \
    X(k_ui_color, k_ui_rgba_hex, (k_ui_bytes text)) \
    X(k_ui_color, k_ui_rgb_factor, (k_ui_color c, float factor)) \
    X(k_ui_color, k_ui_hsv, (int64_t h, int64_t s, int64_t v)) \
    X(k_ui_color, k_ui_hsva, (int64_t h, int64_t s, int64_t v, int64_t a)) \
    X(k_ui_color, k_ui_hsv_f, (float h, float s, float v)) \
    X(k_ui_color, k_ui_hsva_f, (float h, float s, float v, float a)) \
    X(k_ui_colorf, k_ui_hsva_colorf, (float h, float s, float v, float a)) \
    X(k_ui_float4, k_ui_colorf_hsva_f, (k_ui_colorf c)) \
    X(k_ui_colorf, k_ui_color_cf, (k_ui_color c)) \
    X(uint32_t, k_ui_color_u32, (k_ui_color c)) \
    X(int64_t, k_ui_color_hex_rgba, (k_ui_color c, k_ui_bytes out)) \
    X(int64_t, k_ui_color_hex_rgb, (k_ui_color c, k_ui_bytes out)) \
    X(k_ui_float3, k_ui_color_hsv_f, (k_ui_color c)) \
    X(k_ui_float4, k_ui_color_hsva_f, (k_ui_color c)) \
    /* Drawing on the window's canvas. */ \
    X(void, k_ui_stroke_line, (k_ui_context c, float x0, float y0, float x1, float y1, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_curve, (k_ui_context c, float ax, float ay, float ctrl0x, float ctrl0y, float ctrl1x, float ctrl1y, float bx, float by, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_rect, (k_ui_context c, k_ui_rect r, float rounding, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_circle, (k_ui_context c, k_ui_rect r, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_arc, (k_ui_context c, float cx, float cy, float radius, float a_min, float a_max, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_triangle, (k_ui_context c, float x0, float y0, float x1, float y1, float x2, float y2, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_polyline, (k_ui_context c, k_ui_float2_slice points, float line_thickness, k_ui_color color)) \
    X(void, k_ui_stroke_polygon, (k_ui_context c, k_ui_float2_slice points, float line_thickness, k_ui_color color)) \
    X(void, k_ui_fill_rect, (k_ui_context c, k_ui_rect r, float rounding, k_ui_color color)) \
    X(void, k_ui_fill_rect_multi_color, (k_ui_context c, k_ui_rect r, k_ui_color left, k_ui_color top, k_ui_color right, k_ui_color bottom)) \
    X(void, k_ui_fill_circle, (k_ui_context c, k_ui_rect r, k_ui_color color)) \
    X(void, k_ui_fill_arc, (k_ui_context c, float cx, float cy, float radius, float a_min, float a_max, k_ui_color color)) \
    X(void, k_ui_fill_triangle, (k_ui_context c, float x0, float y0, float x1, float y1, float x2, float y2, k_ui_color color)) \
    X(void, k_ui_fill_polygon, (k_ui_context c, k_ui_float2_slice points, k_ui_color color)) \
    X(void, k_ui_draw_image, (k_ui_context c, k_ui_rect r, k_ui_image img, k_ui_color color)) \
    X(void, k_ui_draw_nine_slice, (k_ui_context c, k_ui_rect r, k_ui_nine_slice slice, k_ui_color color)) \
    X(void, k_ui_draw_text, (k_ui_context c, k_ui_rect r, k_ui_bytes text, k_ui_font font, k_ui_color background, k_ui_color foreground)) \
    X(void, k_ui_push_scissor, (k_ui_context c, k_ui_rect r)) \
    /* Utilities. */ \
    X(uint32_t, k_ui_murmur_hash, (k_ui_bytes key, uint32_t seed)) \
    X(void, k_ui_triangle_from_direction, (k_ui_rect r, float pad_x, float pad_y, int64_t direction, k_ui_float2_slice out)) \
    X(uint8_t, k_ui_strfilter, (k_ui_bytes text, k_ui_bytes regexp)) \
    X(uint8_t, k_ui_strmatch_fuzzy_text, (k_ui_bytes text, k_ui_bytes pattern, int64_t *score))

#define K_UI_PROTO(ret, name, params) ret name params;
K_UI_API(K_UI_PROTO)
#undef K_UI_PROTO

/* The constants stdlib/ui.k declares, with their K types; here they
   are K_UI_<name>. Each has the value of the Nuklear constant of the same
   name with NK_ in front, except where a comment says otherwise. */
#define K_UI_CONSTANTS(X) \
    /* Window flags. */ \
    X(i32, WINDOW_BORDER, 1) \
    X(i32, WINDOW_MOVABLE, 2) \
    X(i32, WINDOW_SCALABLE, 4) \
    X(i32, WINDOW_CLOSABLE, 8) \
    X(i32, WINDOW_MINIMIZABLE, 16) \
    X(i32, WINDOW_NO_SCROLLBAR, 32) \
    X(i32, WINDOW_TITLE, 64) \
    X(i32, WINDOW_SCROLL_AUTO_HIDE, 128) \
    X(i32, WINDOW_BACKGROUND, 256) \
    X(i32, WINDOW_SCALE_LEFT, 512) \
    X(i32, WINDOW_NO_INPUT, 1024) \
    /* Text alignment. */ \
    X(i32, TEXT_ALIGN_LEFT, 1) \
    X(i32, TEXT_ALIGN_CENTERED, 2) \
    X(i32, TEXT_ALIGN_RIGHT, 4) \
    X(i32, TEXT_ALIGN_TOP, 8) \
    X(i32, TEXT_ALIGN_MIDDLE, 16) \
    X(i32, TEXT_ALIGN_BOTTOM, 32) \
    X(i32, TEXT_LEFT, 17) \
    X(i32, TEXT_CENTERED, 18) \
    X(i32, TEXT_RIGHT, 20) \
    /* Widget alignment, for the _align toggles. */ \
    X(i32, WIDGET_ALIGN_LEFT, 1) \
    X(i32, WIDGET_ALIGN_CENTERED, 2) \
    X(i32, WIDGET_ALIGN_RIGHT, 4) \
    X(i32, WIDGET_ALIGN_TOP, 8) \
    X(i32, WIDGET_ALIGN_MIDDLE, 16) \
    X(i32, WIDGET_ALIGN_BOTTOM, 32) \
    X(i32, WIDGET_LEFT, 17) \
    X(i32, WIDGET_CENTERED, 18) \
    X(i32, WIDGET_RIGHT, 20) \
    /* What ui::widget found room for (nk_widget_layout_states). */ \
    X(i32, WIDGET_INVALID, 0) \
    X(i32, WIDGET_VALID, 1) \
    X(i32, WIDGET_ROM, 2) \
    X(i32, WIDGET_DISABLED, 3) \
    /* How a widget was used (nk_widget_states), in edit and chart results. */ \
    X(i32, WIDGET_STATE_MODIFIED, 2) \
    X(i32, WIDGET_STATE_INACTIVE, 4) \
    X(i32, WIDGET_STATE_ENTERED, 8) \
    X(i32, WIDGET_STATE_HOVER, 16) \
    X(i32, WIDGET_STATE_ACTIVED, 32) \
    X(i32, WIDGET_STATE_LEFT, 64) \
    X(i32, WIDGET_STATE_HOVERED, 18) \
    X(i32, WIDGET_STATE_ACTIVE, 34) \
    /* Symbols. */ \
    X(i32, SYMBOL_NONE, 0) \
    X(i32, SYMBOL_X, 1) \
    X(i32, SYMBOL_UNDERSCORE, 2) \
    X(i32, SYMBOL_CIRCLE_SOLID, 3) \
    X(i32, SYMBOL_CIRCLE_OUTLINE, 4) \
    X(i32, SYMBOL_RECT_SOLID, 5) \
    X(i32, SYMBOL_RECT_OUTLINE, 6) \
    X(i32, SYMBOL_TRIANGLE_UP, 7) \
    X(i32, SYMBOL_TRIANGLE_DOWN, 8) \
    X(i32, SYMBOL_TRIANGLE_LEFT, 9) \
    X(i32, SYMBOL_TRIANGLE_RIGHT, 10) \
    X(i32, SYMBOL_PLUS, 11) \
    X(i32, SYMBOL_MINUS, 12) \
    X(i32, SYMBOL_TRIANGLE_UP_OUTLINE, 13) \
    X(i32, SYMBOL_TRIANGLE_DOWN_OUTLINE, 14) \
    X(i32, SYMBOL_TRIANGLE_LEFT_OUTLINE, 15) \
    X(i32, SYMBOL_TRIANGLE_RIGHT_OUTLINE, 16) \
    X(i32, SYMBOL_CHEVRON_UP, 17) \
    X(i32, SYMBOL_CHEVRON_RIGHT, 18) \
    X(i32, SYMBOL_CHEVRON_DOWN, 19) \
    X(i32, SYMBOL_CHEVRON_LEFT, 20) \
    X(i32, SYMBOL_HAMBURGER, 21) \
    /* Keys. */ \
    X(i32, KEY_NONE, 0) \
    X(i32, KEY_SHIFT, 1) \
    X(i32, KEY_CTRL, 2) \
    X(i32, KEY_DEL, 3) \
    X(i32, KEY_ENTER, 4) \
    X(i32, KEY_TAB, 5) \
    X(i32, KEY_BACKSPACE, 6) \
    X(i32, KEY_COPY, 7) \
    X(i32, KEY_CUT, 8) \
    X(i32, KEY_PASTE, 9) \
    X(i32, KEY_UP, 10) \
    X(i32, KEY_DOWN, 11) \
    X(i32, KEY_LEFT, 12) \
    X(i32, KEY_RIGHT, 13) \
    X(i32, KEY_TEXT_INSERT_MODE, 14) \
    X(i32, KEY_TEXT_REPLACE_MODE, 15) \
    X(i32, KEY_TEXT_RESET_MODE, 16) \
    X(i32, KEY_TEXT_LINE_START, 17) \
    X(i32, KEY_TEXT_LINE_END, 18) \
    X(i32, KEY_TEXT_START, 19) \
    X(i32, KEY_TEXT_END, 20) \
    X(i32, KEY_TEXT_UNDO, 21) \
    X(i32, KEY_TEXT_REDO, 22) \
    X(i32, KEY_TEXT_SELECT_ALL, 23) \
    X(i32, KEY_TEXT_WORD_LEFT, 24) \
    X(i32, KEY_TEXT_WORD_RIGHT, 25) \
    X(i32, KEY_SCROLL_START, 26) \
    X(i32, KEY_SCROLL_END, 27) \
    X(i32, KEY_SCROLL_DOWN, 28) \
    X(i32, KEY_SCROLL_UP, 29) \
    /* Mouse buttons. */ \
    X(i32, BUTTON_LEFT, 0) \
    X(i32, BUTTON_MIDDLE, 1) \
    X(i32, BUTTON_RIGHT, 2) \
    X(i32, BUTTON_DOUBLE, 3) \
    X(i32, BUTTON_X1, 4) \
    X(i32, BUTTON_X2, 5) \
    /* Button behavior. */ \
    X(i32, BUTTON_DEFAULT, 0) \
    X(i32, BUTTON_REPEATER, 1) \
    /* Directions (nk_heading). */ \
    X(i32, UP, 0) \
    X(i32, RIGHT, 1) \
    X(i32, DOWN, 2) \
    X(i32, LEFT, 3) \
    /* Progress bars. */ \
    X(i32, FIXED, 0) \
    X(i32, MODIFIABLE, 1) \
    /* Orientation. */ \
    X(i32, VERTICAL, 0) \
    X(i32, HORIZONTAL, 1) \
    /* Collapse and show states. */ \
    X(i32, MINIMIZED, 0) \
    X(i32, MAXIMIZED, 1) \
    X(i32, HIDDEN, 0) \
    X(i32, SHOWN, 1) \
    /* Charts, and what chart_push says happened. */ \
    X(i32, CHART_LINES, 0) \
    X(i32, CHART_COLUMN, 1) \
    X(i32, CHART_HOVERING, 1) \
    X(i32, CHART_CLICKED, 2) \
    /* Color formats. */ \
    X(i32, RGB, 0) \
    X(i32, RGBA, 1) \
    /* Popups, layouts, trees. */ \
    X(i32, POPUP_STATIC, 0) \
    X(i32, POPUP_DYNAMIC, 1) \
    X(i32, DYNAMIC, 0) \
    X(i32, STATIC, 1) \
    X(i32, TREE_NODE, 0) \
    X(i32, TREE_TAB, 1) \
    /* Where a tooltip sits (nk_tooltip_pos). */ \
    X(i32, TOP_LEFT, 0) \
    X(i32, TOP_CENTER, 1) \
    X(i32, TOP_RIGHT, 2) \
    X(i32, MIDDLE_LEFT, 3) \
    X(i32, MIDDLE_CENTER, 4) \
    X(i32, MIDDLE_RIGHT, 5) \
    X(i32, BOTTOM_LEFT, 6) \
    X(i32, BOTTOM_CENTER, 7) \
    X(i32, BOTTOM_RIGHT, 8) \
    /* Links. */ \
    X(i32, LINK_UNDERLINE_NONE, 0) \
    X(i32, LINK_UNDERLINE_HOVER, 1) \
    X(i32, LINK_UNDERLINE_ALWAYS, 2) \
    /* Text edits: flags, the usual combinations, and what edit_* returns. */ \
    X(i32, EDIT_DEFAULT, 0) \
    X(i32, EDIT_READ_ONLY, 1) \
    X(i32, EDIT_AUTO_SELECT, 2) \
    X(i32, EDIT_SIG_ENTER, 4) \
    X(i32, EDIT_ALLOW_TAB, 8) \
    X(i32, EDIT_NO_CURSOR, 16) \
    X(i32, EDIT_SELECTABLE, 32) \
    X(i32, EDIT_CLIPBOARD, 64) \
    X(i32, EDIT_CTRL_ENTER_NEWLINE, 128) \
    X(i32, EDIT_NO_HORIZONTAL_SCROLL, 256) \
    X(i32, EDIT_ALWAYS_INSERT_MODE, 512) \
    X(i32, EDIT_MULTILINE, 1024) \
    X(i32, EDIT_GOTO_END_ON_ACTIVATE, 2048) \
    X(i32, EDIT_SIMPLE, 512) \
    X(i32, EDIT_FIELD, 608) \
    X(i32, EDIT_BOX, 1640) \
    X(i32, EDIT_EDITOR, 1128) \
    X(i32, EDIT_ACTIVE, 1) \
    X(i32, EDIT_INACTIVE, 2) \
    X(i32, EDIT_ACTIVATED, 4) \
    X(i32, EDIT_DEACTIVATED, 8) \
    X(i32, EDIT_COMMITTED, 16) \
    /* What a text edit accepts: Nuklear's nk_filter_* (not Nuklear values). */ \
    X(i32, FILTER_DEFAULT, 0) \
    X(i32, FILTER_ASCII, 1) \
    X(i32, FILTER_FLOAT, 2) \
    X(i32, FILTER_DECIMAL, 3) \
    X(i32, FILTER_HEX, 4) \
    X(i32, FILTER_OCT, 5) \
    X(i32, FILTER_BINARY, 6) \
    /* A text editor's mode. */ \
    X(i32, TEXT_EDIT_MODE_VIEW, 0) \
    X(i32, TEXT_EDIT_MODE_INSERT, 1) \
    X(i32, TEXT_EDIT_MODE_REPLACE, 2) \
    /* Style colors, the indices of a color table. */ \
    X(i32, COLOR_TEXT, 0) \
    X(i32, COLOR_WINDOW, 1) \
    X(i32, COLOR_HEADER, 2) \
    X(i32, COLOR_BORDER, 3) \
    X(i32, COLOR_BUTTON, 4) \
    X(i32, COLOR_BUTTON_HOVER, 5) \
    X(i32, COLOR_BUTTON_ACTIVE, 6) \
    X(i32, COLOR_TOGGLE, 7) \
    X(i32, COLOR_TOGGLE_HOVER, 8) \
    X(i32, COLOR_TOGGLE_CURSOR, 9) \
    X(i32, COLOR_SELECT, 10) \
    X(i32, COLOR_SELECT_ACTIVE, 11) \
    X(i32, COLOR_SLIDER, 12) \
    X(i32, COLOR_SLIDER_CURSOR, 13) \
    X(i32, COLOR_SLIDER_CURSOR_HOVER, 14) \
    X(i32, COLOR_SLIDER_CURSOR_ACTIVE, 15) \
    X(i32, COLOR_PROPERTY, 16) \
    X(i32, COLOR_EDIT, 17) \
    X(i32, COLOR_EDIT_CURSOR, 18) \
    X(i32, COLOR_COMBO, 19) \
    X(i32, COLOR_CHART, 20) \
    X(i32, COLOR_CHART_COLOR, 21) \
    X(i32, COLOR_CHART_COLOR_HIGHLIGHT, 22) \
    X(i32, COLOR_SCROLLBAR, 23) \
    X(i32, COLOR_SCROLLBAR_CURSOR, 24) \
    X(i32, COLOR_SCROLLBAR_CURSOR_HOVER, 25) \
    X(i32, COLOR_SCROLLBAR_CURSOR_ACTIVE, 26) \
    X(i32, COLOR_TAB_HEADER, 27) \
    X(i32, COLOR_KNOB, 28) \
    X(i32, COLOR_KNOB_CURSOR, 29) \
    X(i32, COLOR_KNOB_CURSOR_HOVER, 30) \
    X(i32, COLOR_KNOB_CURSOR_ACTIVE, 31) \
    X(i32, COLOR_COUNT, 32) \
    /* Cursors. */ \
    X(i32, CURSOR_ARROW, 0) \
    X(i32, CURSOR_TEXT, 1) \
    X(i32, CURSOR_MOVE, 2) \
    X(i32, CURSOR_RESIZE_VERTICAL, 3) \
    X(i32, CURSOR_RESIZE_HORIZONTAL, 4) \
    X(i32, CURSOR_RESIZE_TOP_LEFT_DOWN_RIGHT, 5) \
    X(i32, CURSOR_RESIZE_TOP_RIGHT_DOWN_LEFT, 6) \
    X(i32, CURSOR_COUNT, 7) \
    /* Style items and window header alignment. */ \
    X(i32, STYLE_ITEM_COLOR, 0) \
    X(i32, STYLE_ITEM_IMAGE, 1) \
    X(i32, STYLE_ITEM_NINE_SLICE, 2) \
    X(i32, HEADER_LEFT, 0) \
    X(i32, HEADER_RIGHT, 1) \
    /* Command kinds (nk_command_type). */ \
    X(i32, COMMAND_NOP, 0) \
    X(i32, COMMAND_SCISSOR, 1) \
    X(i32, COMMAND_LINE, 2) \
    X(i32, COMMAND_CURVE, 3) \
    X(i32, COMMAND_RECT, 4) \
    X(i32, COMMAND_RECT_FILLED, 5) \
    X(i32, COMMAND_RECT_MULTI_COLOR, 6) \
    X(i32, COMMAND_CIRCLE, 7) \
    X(i32, COMMAND_CIRCLE_FILLED, 8) \
    X(i32, COMMAND_ARC, 9) \
    X(i32, COMMAND_ARC_FILLED, 10) \
    X(i32, COMMAND_TRIANGLE, 11) \
    X(i32, COMMAND_TRIANGLE_FILLED, 12) \
    X(i32, COMMAND_POLYGON, 13) \
    X(i32, COMMAND_POLYGON_FILLED, 14) \
    X(i32, COMMAND_POLYLINE, 15) \
    X(i32, COMMAND_TEXT, 16) \
    X(i32, COMMAND_IMAGE, 17) \
    /* What convert returns: 0, or which buffer ran out (nk_convert_result). */ \
    X(i32, CONVERT_SUCCESS, 0) \
    X(i32, CONVERT_INVALID_PARAM, 1) \
    X(i32, CONVERT_COMMAND_BUFFER_FULL, 2) \
    X(i32, CONVERT_VERTEX_BUFFER_FULL, 4) \
    X(i32, CONVERT_ELEMENT_BUFFER_FULL, 8) \
    /* The characters a font is baked with (not Nuklear values). */ \
    X(i32, RANGE_DEFAULT, 0) \
    X(i32, RANGE_CHINESE, 1) \
    X(i32, RANGE_CYRILLIC, 2) \
    X(i32, RANGE_KOREAN, 3)

#define K_UI_ENUM(type, name, value) K_UI_##name = value,
enum { K_UI_CONSTANTS(K_UI_ENUM) };
#undef K_UI_ENUM

#ifdef __cplusplus
}
#endif

#endif
