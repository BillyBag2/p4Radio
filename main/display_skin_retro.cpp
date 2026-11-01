#include "display_skin.hpp"
#include "radio.hpp"

namespace p4radio {
namespace {

constexpr uint32_t kBrass = 0xb98b50;
constexpr uint32_t kIvory = 0xf2d9a6;

// Decorations are drawn with LVGL and never intercept a button or QR tap.
lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height,
                uint32_t color, int radius, int border = 0)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(kBrass), 0);
    lv_obj_set_style_border_width(object, border, 0);
    return object;
}

lv_obj_t *caption(lv_obj_t *parent, const char *text, int x, int y, int width,
                  const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

void line(lv_layer_t *layer, int x1, int y1, int x2, int y2,
          uint32_t color, lv_opa_t opacity = LV_OPA_COVER, int width = 1)
{
    lv_draw_line_dsc_t draw;
    lv_draw_line_dsc_init(&draw);
    draw.p1 = {x1, y1};
    draw.p2 = {x2, y2};
    draw.color = lv_color_hex(color);
    draw.opa = opacity;
    draw.width = width;
    lv_draw_line(layer, &draw);
}

void draw_wood(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_current_target(event)), &bounds);
    lv_layer_t *layer = lv_event_get_layer(event);
    // Sparse grain keeps the cabinet warm without noisy text backgrounds.
    for (int y = 48; y < 650; y += 17) {
        const int inset = 20 + (y % 5) * 3;
        line(layer, bounds.x1 + inset, bounds.y1 + y,
             bounds.x2 - inset, bounds.y1 + y, 0xd39b59, 24);
        line(layer, bounds.x1 + inset + 12, bounds.y1 + y + 2,
             bounds.x2 - inset - 8, bounds.y1 + y + 2, 0x170c06, 50);
    }
}

void draw_cloth(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_current_target(event)), &bounds);
    lv_layer_t *layer = lv_event_get_layer(event);
    // Crossing light/dark yarns produce a woven grille without bitmap assets
    // or hundreds of LVGL objects. All yarns stay inside the rounded frame.
    for (int x = bounds.x1 + 20; x <= bounds.x2 - 20; x += 8) {
        line(layer, x, bounds.y1 + 20, x, bounds.y2 - 20, 0xc2a16b, 160, 2);
        line(layer, x + 3, bounds.y1 + 20, x + 3, bounds.y2 - 20, 0x2c2115, 190);
    }
    for (int y = bounds.y1 + 20; y <= bounds.y2 - 20; y += 6) {
        line(layer, bounds.x1 + 20, y, bounds.x2 - 20, y, 0xe0c48c, 95);
        line(layer, bounds.x1 + 20, y + 2, bounds.x2 - 20, y + 2, 0x241a10, 150);
    }
}

void draw_dial(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_current_target(event)), &bounds);
    lv_layer_t *layer = lv_event_get_layer(event);
    for (int tick = 0; tick <= 40; ++tick) {
        const int x = bounds.x1 + 48 + tick * 516 / 40;
        line(layer, x, bounds.y1 + (tick % 10 == 0 ? 130 : 138),
             x, bounds.y1 + 144, 0x60401e, 210);
    }
    line(layer, bounds.x1 + 48, bounds.y1 + 144,
         bounds.x1 + 564, bounds.y1 + 144, 0x60401e, 210);
}

class RetroSkin final : public DisplaySkin {
public:
    const char *name() const override { return "Retro"; }

private:
    lv_obj_t *dial_pointer_ = nullptr;

    bool prepare_resources() override { return prepare_drawing_pool(); }

    static void bakelite(lv_obj_t *button, bool round)
    {
        lv_obj_set_style_bg_color(button, lv_color_hex(0x4b3425), 0);
        lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x17100c), 0);
        lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x9e784c), 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_radius(button, round ? LV_RADIUS_CIRCLE : 10, 0);
        lv_obj_set_style_shadow_color(button, lv_color_hex(0x080503), 0);
        lv_obj_set_style_shadow_opa(button, 180, 0);
        lv_obj_set_style_shadow_width(button, 10, 0);
        lv_obj_set_style_shadow_offset_y(button, 5, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x100a06), LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x382417), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button, lv_color_hex(kIvory), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 2, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_offset_y(button, 1, LV_STATE_PRESSED);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kIvory), 0);
        lv_obj_set_style_text_font(label, round ? &lv_font_montserrat_48 : &lv_font_montserrat_24, 0);
        lv_obj_set_style_transform_scale_x(label, 256, 0);
        lv_obj_set_style_transform_scale_y(label, 256, 0);
        lv_obj_center(label);
        if (round) {
            lv_obj_t *ring = panel(button, 0, 0, 68, 68, 0, LV_RADIUS_CIRCLE, 1);
            lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(ring, lv_color_hex(0x765438), 0);
            lv_obj_center(ring);
            lv_obj_t *index = panel(button, 0, 0, 3, 6, kIvory, 1);
            lv_obj_align(index, LV_ALIGN_TOP_MID, 0, 2);
            lv_obj_move_foreground(label);
        }
    }

    void build_controls() override
    {
        lv_obj_t *cabinet = panel(surface_, 18, 18, 684, 684, 0x704326, 38, 3);
        lv_obj_set_style_bg_grad_color(cabinet, lv_color_hex(0x352013), 0);
        lv_obj_set_style_bg_grad_dir(cabinet, LV_GRAD_DIR_VER, 0);
        lv_obj_add_event_cb(cabinet, draw_wood, LV_EVENT_DRAW_MAIN, nullptr);
        // Common labels and meters already exist: put the entire cabinet
        // decoration behind them, then build the real interactive controls.
        lv_obj_move_background(cabinet);
        lv_obj_t *brand = caption(cabinet, "THE WIRELESS", 72, 80, 540, &lv_font_montserrat_24, kIvory);
        lv_obj_set_style_text_letter_space(brand, 5, 0);

        lv_obj_t *cloth = panel(cabinet, 36, 126, 612, 198, 0x796040, 22, 5);
        lv_obj_add_event_cb(cloth, draw_cloth, LV_EVENT_DRAW_MAIN, nullptr);
        lv_obj_t *badge = panel(cloth, 202, 65, 208, 44, 0xbc9356, 22, 1);
        caption(badge, "HIGH FIDELITY", 0, 7, 208, &lv_font_montserrat_24, 0x322314);

        lv_obj_t *dial = panel(cabinet, 36, 348, 612, 150, 0xf2dea5, 14, 4);
        lv_obj_set_style_bg_grad_color(dial, lv_color_hex(0xc5a466), 0);
        lv_obj_set_style_bg_grad_dir(dial, LV_GRAD_DIR_VER, 0);
        lv_obj_add_event_cb(dial, draw_dial, LV_EVENT_DRAW_MAIN, nullptr);
        // Account for the dial's 4-pixel content border when aligning to ticks.
        dial_pointer_ = panel(dial, 43, 119, 3, 21, 0x9e3c27, 1);

        caption(surface_, "VOLUME", 74, 526, 200, &lv_font_montserrat_24, kIvory);
        caption(surface_, "STATION", 446, 526, 200, &lv_font_montserrat_24, kIvory);
        bakelite(add_button("-", 76, 562, 88, 88, Action::VolumeDown), true);
        bakelite(add_button("+", 184, 562, 88, 88, Action::VolumeUp), true);
        bakelite(add_button("<", 448, 562, 88, 88, Action::PreviousStation), true);
        bakelite(add_button(">", 556, 562, 88, 88, Action::NextStation), true);
        bakelite(add_button("OFF", 302, 548, 116, 50, Action::Off), false);
        bakelite(add_button("TUNE", 302, 611, 116, 50, Action::Tune), false);
        panel(cabinet, 50, 658, 584, 2, kBrass, 0);
    }

    void arrange_widgets() override
    {
        lv_obj_set_style_bg_color(surface_, lv_color_hex(0x180f09), 0);
        lv_obj_set_width(hostname_label_, 560);
        lv_obj_set_style_text_color(hostname_label_, lv_color_hex(kIvory), 0);
        lv_obj_align(hostname_label_, LV_ALIGN_TOP_MID, 0, 51);
        lv_obj_set_width(station_label_, 560);
        lv_obj_set_style_text_font(station_label_, &lv_font_montserrat_40, 0);
        lv_obj_set_style_text_color(station_label_, lv_color_hex(0x392715), 0);
        lv_obj_align(station_label_, LV_ALIGN_TOP_MID, 0, 382);
        lv_obj_set_size(status_label_, 560, 50);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0x52391e), 0);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
        lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 435);

        lv_obj_align(power_meter_, LV_ALIGN_TOP_MID, 0, 300);
        lv_obj_set_style_bg_color(power_meter_, lv_color_hex(0xa4563e), 0);
        lv_obj_set_style_bg_color(lv_obj_get_child(power_meter_, 0), lv_color_hex(0x8b9956), 0);
        lv_obj_set_style_bg_color(bandwidth_needle_, lv_color_hex(0x352315), 0);
        lv_obj_set_style_border_color(lv_obj_get_child(power_meter_, 2), lv_color_hex(kBrass), 0);
        buffer_color_ = 0xc1a477;
        buffer_low_color_ = 0xe77843;
        for (lv_obj_t *bar : {buffer_bar_, volume_bar_}) {
            lv_obj_set_size(bar, 6, 176);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x291b10), LV_PART_MAIN);
        }
        lv_obj_align(buffer_bar_, LV_ALIGN_TOP_LEFT, 32, 155);
        lv_obj_align(volume_bar_, LV_ALIGN_TOP_RIGHT, -32, 155);
        lv_obj_set_style_bg_color(volume_bar_, lv_color_hex(kIvory), LV_PART_INDICATOR);
    }

    void refresh_theme() override
    {
        if (dial_pointer_) lv_obj_set_x(dial_pointer_, 43 + station_dial_position());
    }
};

} // namespace

DisplaySkin &retro_display_skin()
{
    static RetroSkin skin;
    return skin;
}

} // namespace p4radio
