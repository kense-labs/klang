/* Drawing straight onto the current window's canvas: lines, curves,
   rectangles, circles, arcs, triangles, polygons, images and text, and
   the scissor they are clipped to. */

#include "ui_internal.h"

#include <math.h>

/* The window's command buffer, or NULL and a misuse naming `fn`. */
static struct nk_command_buffer *ui_canvas(k_ui_context c, const char *fn) {
    ui_ctx *u = ui_ctx_get(c, fn);
    if (!u || !ui_in_panel(u, fn)) return NULL;
    return nk_window_get_canvas(&u->nk);
}

/* Nuklear stores coordinates, thicknesses and roundings as 16-bit integers:
   what cannot be one is a misuse. */
static bool ui_coord_ok(float v) {
    return isfinite(v) && v > -32768.0f && v < 32768.0f;
}

static bool ui_coords_ok(const char *fn, int n, const float *v) {
    for (int i = 0; i < n; i++)
        if (!ui_coord_ok(v[i]))
            return ui_misuse("%s: a coordinate or size of %g, where it is from -32768 to 32767", fn,
                             (double)v[i]);
    return true;
}

static bool ui_thickness_ok(float t, const char *fn) {
    if (isfinite(t) && t >= 0 && t < 65536) return true;
    return ui_misuse("%s: a line %g thick", fn, (double)t);
}

static bool ui_rect_coords_ok(k_ui_rect r, const char *fn) {
    float v[4] = { r.x, r.y, r.w, r.h };
    return ui_coords_ok(fn, 4, v);
}

static bool ui_points_ok(k_ui_float2_slice points, const char *fn) {
    if (points.len > 65535)
        return ui_misuse("%s: %lld points, of at most 65535", fn, (long long)points.len);
    return ui_coords_ok(fn, (int)points.len * 2, (const float *)points.data);
}

void k_ui_stroke_line(k_ui_context c, float x0, float y0, float x1, float y1,
                       float line_thickness, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_line");
    float v[4] = { x0, y0, x1, y1 };
    if (b && ui_coords_ok("ui::stroke_line", 4, v) &&
        ui_thickness_ok(line_thickness, "ui::stroke_line"))
        nk_stroke_line(b, x0, y0, x1, y1, line_thickness, ui_nk_color(color));
}

void k_ui_stroke_curve(k_ui_context c, float ax, float ay, float ctrl0x, float ctrl0y,
                        float ctrl1x, float ctrl1y, float bx, float by, float line_thickness,
                        k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_curve");
    float v[8] = { ax, ay, ctrl0x, ctrl0y, ctrl1x, ctrl1y, bx, by };
    if (b && ui_coords_ok("ui::stroke_curve", 8, v) &&
        ui_thickness_ok(line_thickness, "ui::stroke_curve"))
        nk_stroke_curve(b, ax, ay, ctrl0x, ctrl0y, ctrl1x, ctrl1y, bx, by, line_thickness,
                        ui_nk_color(color));
}

void k_ui_stroke_rect(k_ui_context c, k_ui_rect r, float rounding, float line_thickness,
                       k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_rect");
    if (b && ui_rect_coords_ok(r, "ui::stroke_rect") &&
        ui_thickness_ok(rounding, "ui::stroke_rect") &&
        ui_thickness_ok(line_thickness, "ui::stroke_rect"))
        nk_stroke_rect(b, ui_nk_rect(r), rounding, line_thickness, ui_nk_color(color));
}

void k_ui_stroke_circle(k_ui_context c, k_ui_rect r, float line_thickness, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_circle");
    if (b && ui_rect_coords_ok(r, "ui::stroke_circle") &&
        ui_thickness_ok(line_thickness, "ui::stroke_circle"))
        nk_stroke_circle(b, ui_nk_rect(r), line_thickness, ui_nk_color(color));
}

static bool ui_arc_ok(float cx, float cy, float radius, float a_min, float a_max, const char *fn) {
    float v[3] = { cx, cy, radius };
    if (!ui_coords_ok(fn, 3, v)) return false;
    if (radius >= 0 && isfinite(a_min) && isfinite(a_max)) return true;
    return ui_misuse("%s: an arc of radius %g from %g to %g", fn, (double)radius, (double)a_min,
                     (double)a_max);
}

void k_ui_stroke_arc(k_ui_context c, float cx, float cy, float radius, float a_min, float a_max,
                      float line_thickness, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_arc");
    if (b && ui_arc_ok(cx, cy, radius, a_min, a_max, "ui::stroke_arc") &&
        ui_thickness_ok(line_thickness, "ui::stroke_arc"))
        nk_stroke_arc(b, cx, cy, radius, a_min, a_max, line_thickness, ui_nk_color(color));
}

void k_ui_stroke_triangle(k_ui_context c, float x0, float y0, float x1, float y1, float x2,
                           float y2, float line_thickness, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_triangle");
    float v[6] = { x0, y0, x1, y1, x2, y2 };
    if (b && ui_coords_ok("ui::stroke_triangle", 6, v) &&
        ui_thickness_ok(line_thickness, "ui::stroke_triangle"))
        nk_stroke_triangle(b, x0, y0, x1, y1, x2, y2, line_thickness, ui_nk_color(color));
}

void k_ui_stroke_polyline(k_ui_context c, k_ui_float2_slice points, float line_thickness,
                           k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_polyline");
    if (b && ui_points_ok(points, "ui::stroke_polyline") &&
        ui_thickness_ok(line_thickness, "ui::stroke_polyline") && points.len)
        nk_stroke_polyline(b, (const float *)points.data, (int)points.len, line_thickness,
                           ui_nk_color(color));
}

void k_ui_stroke_polygon(k_ui_context c, k_ui_float2_slice points, float line_thickness,
                          k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::stroke_polygon");
    if (b && ui_points_ok(points, "ui::stroke_polygon") &&
        ui_thickness_ok(line_thickness, "ui::stroke_polygon") && points.len)
        nk_stroke_polygon(b, (const float *)points.data, (int)points.len, line_thickness,
                          ui_nk_color(color));
}

void k_ui_fill_rect(k_ui_context c, k_ui_rect r, float rounding, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::fill_rect");
    if (b && ui_rect_coords_ok(r, "ui::fill_rect") && ui_thickness_ok(rounding, "ui::fill_rect"))
        nk_fill_rect(b, ui_nk_rect(r), rounding, ui_nk_color(color));
}

void k_ui_fill_rect_multi_color(k_ui_context c, k_ui_rect r, k_ui_color left, k_ui_color top,
                                 k_ui_color right, k_ui_color bottom) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::fill_rect_multi_color");
    if (b && ui_rect_coords_ok(r, "ui::fill_rect_multi_color"))
        nk_fill_rect_multi_color(b, ui_nk_rect(r), ui_nk_color(left), ui_nk_color(top),
                                 ui_nk_color(right), ui_nk_color(bottom));
}

void k_ui_fill_circle(k_ui_context c, k_ui_rect r, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::fill_circle");
    if (b && ui_rect_coords_ok(r, "ui::fill_circle"))
        nk_fill_circle(b, ui_nk_rect(r), ui_nk_color(color));
}

void k_ui_fill_arc(k_ui_context c, float cx, float cy, float radius, float a_min, float a_max,
                    k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::fill_arc");
    if (b && ui_arc_ok(cx, cy, radius, a_min, a_max, "ui::fill_arc"))
        nk_fill_arc(b, cx, cy, radius, a_min, a_max, ui_nk_color(color));
}

void k_ui_fill_triangle(k_ui_context c, float x0, float y0, float x1, float y1, float x2,
                         float y2, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::fill_triangle");
    float v[6] = { x0, y0, x1, y1, x2, y2 };
    if (b && ui_coords_ok("ui::fill_triangle", 6, v))
        nk_fill_triangle(b, x0, y0, x1, y1, x2, y2, ui_nk_color(color));
}

void k_ui_fill_polygon(k_ui_context c, k_ui_float2_slice points, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::fill_polygon");
    if (b && ui_points_ok(points, "ui::fill_polygon") && points.len)
        nk_fill_polygon(b, (const float *)points.data, (int)points.len, ui_nk_color(color));
}

void k_ui_draw_image(k_ui_context c, k_ui_rect r, k_ui_image img, k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::draw_image");
    if (!b || !ui_rect_coords_ok(r, "ui::draw_image")) return;
    struct nk_image n = ui_nk_image(img);
    nk_draw_image(b, ui_nk_rect(r), &n, ui_nk_color(color));
}

void k_ui_draw_nine_slice(k_ui_context c, k_ui_rect r, k_ui_nine_slice slice,
                           k_ui_color color) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::draw_nine_slice");
    if (!b || !ui_rect_coords_ok(r, "ui::draw_nine_slice")) return;
    struct nk_nine_slice n;
    memset(&n, 0, sizeof n);
    n.img = ui_nk_image(slice.img);
    n.l = slice.l;
    n.t = slice.t;
    n.r = slice.r;
    n.b = slice.b;
    nk_draw_nine_slice(b, ui_nk_rect(r), &n, ui_nk_color(color));
}

void k_ui_draw_text(k_ui_context c, k_ui_rect r, k_ui_bytes text, k_ui_font font,
                     k_ui_color background, k_ui_color foreground) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::draw_text");
    if (!b || !ui_rect_coords_ok(r, "ui::draw_text")) return;
    ui_font *f = ui_font_get(font, "ui::draw_text");
    if (!f) return;
    ui_atlas *a = (ui_atlas *)ui_table_find(&ui_atlases, f->atlas);
    if (!a->baked) {
        ui_misuse("ui::draw_text: the font's atlas is not baked yet");
        return;
    }
    nk_draw_text(b, ui_nk_rect(r), (const char *)text.data, ui_len(text, "ui::draw_text"),
                 &f->font->handle, ui_nk_color(background), ui_nk_color(foreground));
}

void k_ui_push_scissor(k_ui_context c, k_ui_rect r) {
    struct nk_command_buffer *b = ui_canvas(c, "ui::push_scissor");
    if (b && ui_rect_coords_ok(r, "ui::push_scissor")) nk_push_scissor(b, ui_nk_rect(r));
}
