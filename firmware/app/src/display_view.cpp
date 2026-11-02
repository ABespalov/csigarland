/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Draws the main screen with LVGL, on a panel or in memory
 * Contributors: None
 *
 * Knows nothing of the panel, so that tools/screens can draw the same screen to a file for the
 * documents. A picture from this code cannot disagree with the garland about a layout.
 */

#include "display_view.h"

#include "glyphs.h"
#include "wordmark.h"

namespace garland {
namespace {

// csiPass's composition on this square panel: wordmark, build line and rule, then
// the framed body and the framed footer.
constexpr int kWordmarkTop = 5;
constexpr int kVersionTop = 27;
constexpr int kRuleTop = 45;
constexpr int kRuleInset = 10;
constexpr int kPanelInset = 8;
constexpr int kPanelBorder = 2;
constexpr int kPanelCorner = 8;
constexpr int kBodyTop = 51;
constexpr int kBodyHeight = 143;
constexpr int kFooterTop = 198;
constexpr int kFooterHeight = 36;
constexpr int kHeadlineTop = 56;
constexpr int kIconSize = 32;
constexpr int kIconCorner = 6;
constexpr int kHeadlineGap = 8;
constexpr int kTextWidth = 208;
constexpr int kStatsTop = 160;
constexpr int kTextBoxTop = kHeadlineTop + kIconSize;
constexpr int kStatsWidth = kScreenSize - 2 * (kPanelInset + kPanelBorder);
constexpr int kStatGlyphGap = 3;
constexpr int kStatCount = 3;
constexpr int kUpdateBarTop = 146;
constexpr int kUpdateBarHeight = 12;
constexpr int kUpdatePercentTop = 163;
// On the wordmark's lowercase rows, 2 to 6, and at the end of the rule below, as in csiPass.
constexpr int kClockGlyphTop = kWordmarkTop + 2 * kWordmarkScale;
constexpr int kClockGlyphLeft = kScreenSize - kRuleInset - kClockGlyphSize;

const lv_font_t* const kVersionFont = &lv_font_montserrat_14;
const lv_font_t* const kTitleFont = &lv_font_montserrat_28;
const lv_font_t* const kBodyFont = &lv_font_montserrat_20;
const lv_font_t* const kStatFont = &lv_font_montserrat_16;
const lv_font_t* const kLabelFont = &lv_font_montserrat_16;

// Only ever touched from the LVGL task, or under its lock during setup.
struct MainScreen {
    lv_obj_t* root;
    lv_obj_t* rule;
    lv_obj_t* body_panel;
    lv_obj_t* footer_panel;
    lv_obj_t* icon;
    lv_obj_t* icon_glyph;
    lv_obj_t* clock_glyph;
    lv_obj_t* wordmark;
    lv_obj_t* version;
    lv_obj_t* title;
    lv_obj_t* text_box;
    lv_obj_t* text;
    lv_obj_t* footer;
    lv_obj_t* footer_twin;
    lv_obj_t* stats[kStatCount];
    lv_obj_t* stat_glyphs[kStatCount];
    lv_obj_t* stat_values[kStatCount];
    lv_obj_t* update_bar;
    lv_obj_t* update_fill;
    lv_obj_t* update_percent;
};

MainScreen s_screen;
uint16_t s_wordmark_pixels[kWordmarkPixels];
lv_image_dsc_t s_wordmark;
// What the wordmark was last drawn as, as drawing it again is every pixel.
bool s_header_drawn = false;
bool s_header_dimmed = false;

lv_obj_t* add_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int y) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text_static(label, "");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

}  // namespace

// A bare rectangle with none of a default object's theme, scrolling or clicks.
lv_obj_t* display_add_block(lv_obj_t* parent) {
    lv_obj_t* block = lv_obj_create(parent);
    lv_obj_remove_style_all(block);
    lv_obj_set_scrollable(block, false);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    return block;
}

namespace {

lv_obj_t* add_panel(lv_obj_t* parent, int top, int height) {
    lv_obj_t* panel = display_add_block(parent);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, kPanelBorder, 0);
    lv_obj_set_style_radius(panel, kPanelCorner, 0);
    lv_obj_set_size(panel, kScreenSize - 2 * kPanelInset, height);
    lv_obj_set_pos(panel, kPanelInset, top);
    return panel;
}

// A glyph is coverage, drawn in the image's recolour.
lv_obj_t* add_glyph(lv_obj_t* parent, lv_image_dsc_t& image, const uint8_t* glyph, int size) {
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_A8;
    image.header.w = size;
    image.header.h = size;
    image.header.stride = size;
    image.data_size = size * size;
    image.data = glyph;

    lv_obj_t* icon = lv_image_create(parent);
    lv_image_set_src(icon, &image);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    return icon;
}

// A glyph and its value, as one group in the status row.
void add_stat(lv_obj_t* parent, int index, const uint8_t* glyph) {
    static lv_image_dsc_t s_images[kStatCount];

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, kStatGlyphGap, 0);

    lv_obj_t* icon = add_glyph(row, s_images[index], glyph, kGlyphSize);
    lv_obj_t* value = lv_label_create(row);
    lv_obj_set_style_text_font(value, kStatFont, 0);
    lv_obj_set_style_text_color(value, kWhite, 0);
    lv_label_set_text_static(value, "");

    s_screen.stats[index] = row;
    s_screen.stat_glyphs[index] = icon;
    s_screen.stat_values[index] = value;
}

void add_wordmark(lv_obj_t* parent) {
    render_wordmark(s_wordmark_pixels, lv_color_to_u16(kBackground), lv_color_to_u16(kWhite),
                    lv_color_to_u16(kBrandBlue));
    s_wordmark.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_wordmark.header.cf = LV_COLOR_FORMAT_RGB565;
    s_wordmark.header.w = kWordmarkWidth;
    s_wordmark.header.h = kWordmarkHeight;
    s_wordmark.header.stride = kWordmarkWidth * sizeof(uint16_t);
    s_wordmark.data_size = sizeof(s_wordmark_pixels);
    s_wordmark.data = reinterpret_cast<const uint8_t*>(s_wordmark_pixels);

    s_screen.wordmark = lv_image_create(parent);
    lv_image_set_src(s_screen.wordmark, &s_wordmark);
    lv_obj_align(s_screen.wordmark, LV_ALIGN_TOP_MID, 0, kWordmarkTop);
}

// On a grey frame the wordmark goes grey too, "csi" still the brighter, and the version with it. On
// any coloured frame it is the brand's white and blue.
void set_header(bool dimmed) {
    if (s_header_drawn && dimmed == s_header_dimmed) {
        return;
    }
    s_header_drawn = true;
    s_header_dimmed = dimmed;
    lv_obj_set_style_text_color(s_screen.version, dimmed ? kMuted : kVersionText, 0);
    render_wordmark(s_wordmark_pixels, lv_color_to_u16(kBackground), lv_color_to_u16(dimmed ? kDimmedBrand : kWhite),
                    lv_color_to_u16(dimmed ? kMuted : kBrandBlue));
    lv_obj_invalidate(s_screen.wordmark);
}

}  // namespace

void display_view_build(const char* firmware_label) {
    lv_obj_t* root = lv_screen_active();
    s_screen.root = root;
    lv_obj_set_style_bg_color(root, kBackground, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);

    add_wordmark(root);
    s_screen.version = add_label(root, kVersionFont, kVersionText, kVersionTop);
    lv_label_set_text_static(s_screen.version, firmware_label);
    s_screen.rule = display_add_block(root);
    lv_obj_set_size(s_screen.rule, kScreenSize - 2 * kRuleInset, kPanelBorder);
    lv_obj_set_pos(s_screen.rule, kRuleInset, kRuleTop);

    static lv_image_dsc_t s_clock_image;
    s_screen.clock_glyph = add_glyph(root, s_clock_image, kClockGlyph, kClockGlyphSize);
    lv_obj_set_style_image_recolor(s_screen.clock_glyph, kMuted, 0);
    lv_obj_set_pos(s_screen.clock_glyph, kClockGlyphLeft, kClockGlyphTop);
    lv_obj_set_hidden(s_screen.clock_glyph, true);

    s_screen.body_panel = add_panel(root, kBodyTop, kBodyHeight);
    s_screen.footer_panel = add_panel(root, kFooterTop, kFooterHeight);

    // The icon and the short status word sit side by side and stay centred together.
    lv_obj_t* headline = lv_obj_create(root);
    lv_obj_remove_style_all(headline);
    lv_obj_set_scrollable(headline, false);
    lv_obj_set_size(headline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(headline, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headline, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(headline, kHeadlineGap, 0);
    lv_obj_align(headline, LV_ALIGN_TOP_MID, 0, kHeadlineTop);

    s_screen.icon = display_add_block(headline);
    lv_obj_set_size(s_screen.icon, kIconSize, kIconSize);
    lv_obj_set_style_radius(s_screen.icon, kIconCorner, 0);
    s_screen.icon_glyph = lv_label_create(s_screen.icon);
    lv_obj_set_style_text_font(s_screen.icon_glyph, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_screen.icon_glyph, kBackground, 0);
    lv_label_set_text_static(s_screen.icon_glyph, "");
    lv_obj_center(s_screen.icon_glyph);

    s_screen.title = lv_label_create(headline);
    lv_obj_set_style_text_font(s_screen.title, kTitleFont, 0);
    lv_label_set_text_static(s_screen.title, "");

    // Centred in the room between the headline and what is below it, however many lines it
    // takes: effect names come from files and can take two.
    s_screen.text_box = lv_obj_create(root);
    lv_obj_remove_style_all(s_screen.text_box);
    lv_obj_set_scrollable(s_screen.text_box, false);
    lv_obj_set_size(s_screen.text_box, kTextWidth, kStatsTop - kTextBoxTop);
    lv_obj_align(s_screen.text_box, LV_ALIGN_TOP_MID, 0, kTextBoxTop);
    s_screen.text = add_label(s_screen.text_box, kBodyFont, kWhite, 0);
    lv_obj_set_width(s_screen.text, kTextWidth);
    lv_label_set_long_mode(s_screen.text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_screen.text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_screen.text);

    // Equal gaps between the groups and at the ends, whatever their widths.
    lv_obj_t* stats_row = lv_obj_create(root);
    lv_obj_remove_style_all(stats_row);
    lv_obj_set_scrollable(stats_row, false);
    lv_obj_set_size(stats_row, kStatsWidth, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(stats_row, LV_ALIGN_TOP_MID, 0, kStatsTop);
    add_stat(stats_row, 0, kBrightnessGlyph);
    add_stat(stats_row, 1, kSpeedGlyph);
    add_stat(stats_row, 2, kLedsGlyph);

    // In the place of the stats, which an update hides.
    s_screen.update_bar = display_add_block(root);
    lv_obj_set_size(s_screen.update_bar, kTextWidth, kUpdateBarHeight);
    lv_obj_set_pos(s_screen.update_bar, (kScreenSize - kTextWidth) / 2, kUpdateBarTop);
    lv_obj_set_style_radius(s_screen.update_bar, kUpdateBarHeight / 2, 0);
    lv_obj_set_style_bg_color(s_screen.update_bar, lv_color_mix(kMuted, kBackground, LV_OPA_30), 0);
    s_screen.update_fill = display_add_block(s_screen.update_bar);
    lv_obj_set_height(s_screen.update_fill, kUpdateBarHeight);
    lv_obj_set_style_radius(s_screen.update_fill, kUpdateBarHeight / 2, 0);
    s_screen.update_percent = add_label(root, kStatFont, kWhite, kUpdatePercentTop);
    lv_obj_set_hidden(s_screen.update_bar, true);
    lv_obj_set_hidden(s_screen.update_percent, true);

    const int label_height = lv_font_get_line_height(kLabelFont);
    s_screen.footer = add_label(root, kLabelFont, kWhite, kFooterTop);
    lv_obj_align(s_screen.footer, LV_ALIGN_TOP_MID, 0, kFooterTop + (kFooterHeight - label_height) / 2);
    // The built-in fonts have no bold: the same text a pixel to the right thickens the strokes.
    s_screen.footer_twin = add_label(root, kLabelFont, kWhite, kFooterTop);
    lv_obj_align(s_screen.footer_twin, LV_ALIGN_TOP_MID, 1, kFooterTop + (kFooterHeight - label_height) / 2);
}

lv_obj_t* display_view_root() {
    return s_screen.root;
}

namespace {

void set_footer(const char* text) {
    lv_label_set_text(s_screen.footer, text);
    lv_label_set_text(s_screen.footer_twin, text);
}

void set_accent(lv_color_t accent) {
    lv_obj_set_style_bg_color(s_screen.rule, accent, 0);
    lv_obj_set_style_border_color(s_screen.body_panel, accent, 0);
    lv_obj_set_style_border_color(s_screen.footer_panel, accent, 0);
    lv_obj_set_style_bg_color(s_screen.icon, accent, 0);
    lv_obj_set_style_text_color(s_screen.title, accent, 0);
    lv_obj_set_style_bg_color(s_screen.update_fill, accent, 0);
    for (int i = 0; i < kStatCount; ++i) {
        lv_obj_set_style_image_recolor(s_screen.stat_glyphs[i], accent, 0);
    }
}

void set_stats_hidden(bool hidden) {
    for (lv_obj_t* stat : s_screen.stats) {
        lv_obj_set_hidden(stat, hidden);
    }
}

lv_color_t tone_color(ScreenTone tone) {
    switch (tone) {
        case ScreenTone::Idle:
            return kMuted;
        case ScreenTone::Attention:
            return kAttention;
        case ScreenTone::Success:
            return kSuccess;
        default:
            return kBrandBlue;
    }
}

const char* icon_text(ScreenIcon icon) {
    switch (icon) {
        case ScreenIcon::Update:
            return LV_SYMBOL_DOWNLOAD;
        case ScreenIcon::Play:
            return LV_SYMBOL_PLAY;
        case ScreenIcon::Wifi:
            return LV_SYMBOL_WIFI;
        case ScreenIcon::Warning:
            return LV_SYMBOL_WARNING;
        default:
            return LV_SYMBOL_POWER;
    }
}

}  // namespace

// The only drawing of the main screen. screen_model.h decides what it shows.
void display_view_draw(const ScreenModel& model) {
    set_accent(tone_color(model.tone));
    set_header(model.tone == ScreenTone::Idle);
    lv_obj_set_style_image_recolor(s_screen.clock_glyph, model.clock_glyph_lit ? kWhite : kMuted, 0);
    set_stats_hidden(!model.shows_stats);
    lv_obj_set_hidden(s_screen.update_bar, !model.shows_progress);
    lv_obj_set_hidden(s_screen.update_percent, !model.shows_progress);
    lv_obj_set_hidden(s_screen.clock_glyph, !model.shows_clock_glyph);
    lv_obj_set_height(s_screen.text_box, (model.shows_progress ? kUpdateBarTop : kStatsTop) - kTextBoxTop);

    lv_label_set_text_static(s_screen.icon_glyph, icon_text(model.icon));
    lv_label_set_text_static(s_screen.title, model.title);
    lv_label_set_text(s_screen.text, model.text);
    set_footer(model.footer);
    if (model.shows_progress) {
        lv_obj_set_width(s_screen.update_fill, update_bar_pixels(model.percent, kTextWidth));
        lv_label_set_text_fmt(s_screen.update_percent, "%u%%", model.percent);
    }
    if (model.shows_stats) {
        for (int i = 0; i < kStatCount; ++i) {
            lv_label_set_text(s_screen.stat_values[i], model.stats[i]);
        }
    }
}

}  // namespace garland
