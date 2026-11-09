/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: An effect file in memory, and the parser that fills it
 * Contributors: None
 */

#include <cJSON.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "effect_spec.h"
#include "json_limits.h"

namespace garland {
namespace {

constexpr char kEffectFormat[] = "csigarland.effect/1";

struct AlgorithmName {
    const char* name;
    Algorithm algorithm;
};

constexpr AlgorithmName kAlgorithms[] = {
    {"solid", Algorithm::Solid},     {"fade", Algorithm::Fade},       {"chase", Algorithm::Chase},
    {"fire", Algorithm::Fire},       {"candle", Algorithm::Candle},   {"twinkle", Algorithm::Twinkle},
    {"sparkle", Algorithm::Sparkle}, {"stripes", Algorithm::Stripes}, {"aurora", Algorithm::Aurora},
    {"rainbow", Algorithm::Rainbow}, {"breathe", Algorithm::Breathe}, {"wipe", Algorithm::Wipe},
    {"theater", Algorithm::Theater}, {"meteor", Algorithm::Meteor},   {"scanner", Algorithm::Scanner},
    {"colorloop", Algorithm::Colorloop}, {"drift", Algorithm::Drift}, {"fireworks", Algorithm::Fireworks},
    {"heartbeat", Algorithm::Heartbeat}, {"steady", Algorithm::Steady}, {"waves", Algorithm::Waves},
    {"sequential", Algorithm::Sequential}, {"slo-glo", Algorithm::SloGlo}, {"chasing", Algorithm::Chasing},
    {"build-up", Algorithm::BuildUp}, {"cross-fade", Algorithm::CrossFade}, {"all-glow", Algorithm::AllGlow},
    {"random-group", Algorithm::RandomGroup},
    {"slow-fade", Algorithm::SlowFade}, {"flash", Algorithm::Flash}, {"combination", Algorithm::Combination},
    {"running", Algorithm::Running}, {"random-colors", Algorithm::RandomColors}, {"dissolve", Algorithm::Dissolve},
    {"bouncing-balls", Algorithm::BouncingBalls}, {"juggle", Algorithm::Juggle}, {"ripple", Algorithm::Ripple},
    {"sunrise", Algorithm::Sunrise}, {"drip", Algorithm::Drip}, {"rain", Algorithm::Rain},
    {"bands", Algorithm::Bands}, {"saw", Algorithm::Saw}, {"sweep", Algorithm::Sweep},
    {"sweep-random", Algorithm::SweepRandom}, {"wipe-random", Algorithm::WipeRandom}, {"scan", Algorithm::Scan},
    {"android", Algorithm::Android}, {"band", Algorithm::Band}, {"band-random", Algorithm::BandRandom},
    {"chase-flash", Algorithm::ChaseFlash}, {"chase-flash-random", Algorithm::ChaseFlashRandom},
    {"stream", Algorithm::Stream}, {"stream-smooth", Algorithm::StreamSmooth}, {"beam", Algorithm::Beam},
    {"gradient", Algorithm::Gradient}, {"loading", Algorithm::Loading}, {"sinelon", Algorithm::Sinelon},
    {"perlin-move", Algorithm::PerlinMove}, {"oscillate", Algorithm::Oscillate}, {"icu", Algorithm::Icu},
    {"tetrix", Algorithm::Tetrix}, {"percent", Algorithm::Percent}, {"flock", Algorithm::Flock},
    {"washing", Algorithm::Washing}, {"traffic-light", Algorithm::TrafficLight}, {"eyes", Algorithm::Eyes},
    {"spots", Algorithm::Spots}, {"spots-fade", Algorithm::SpotsFade}, {"shimmer", Algorithm::Shimmer},
    {"twinkle-up", Algorithm::TwinkleUp}, {"icicle", Algorithm::Icicle}, {"bubble", Algorithm::Bubble}, {"dynamic", Algorithm::Dynamic}, {"pride", Algorithm::Pride},
    {"bpm", Algorithm::Bpm}, {"noise", Algorithm::Noise}, {"noise-glow", Algorithm::NoiseGlow},
    {"plasma", Algorithm::Plasma}, {"phased", Algorithm::Phased}, {"phased-noise", Algorithm::PhasedNoise},
    {"sine", Algorithm::Sine}, {"flow", Algorithm::Flow}, {"flow-stripe", Algorithm::FlowStripe},
    {"shadows", Algorithm::Shadows}, {"tv", Algorithm::Tv}, {"starburst", Algorithm::Starburst},
    {"rocket", Algorithm::Rocket}, {"rolling", Algorithm::Rolling}, {"popcorn", Algorithm::Popcorn},
    {"blink", Algorithm::Blink}, {"strobe-burst", Algorithm::StrobeBurst}, {"alternate", Algorithm::Alternate},
    {"lightning", Algorithm::Lightning}, {"hourglass", Algorithm::Hourglass}, {"sparkler", Algorithm::Sparkler},
    {"water-wave", Algorithm::WaterWave}, {"comet-collision", Algorithm::CometCollision},
    {"figure-parade", Algorithm::FigureParade},
};

// The order of the figurines along the cable, from the control unit, repeated: docs/guides/effects.md,
// section 7. deploy/model_renders.py draws the garland in it.
constexpr const char* kFigurines[kFigurineCount] = {"tree",    "star", "snowman", "pinecone",
                                                    "lantern", "bell", "icicle",  "gingerbread"};

constexpr Rgb kDefaultColor{255, 170, 80};

constexpr std::array<Rgb, 7> kRainbowStops = {{{255, 0, 0},
                                               {255, 160, 0},
                                               {255, 255, 0},
                                               {0, 255, 0},
                                               {0, 80, 255},
                                               {160, 0, 255},
                                               {255, 0, 0}}};

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_color(const cJSON* item, Rgb& out) {
    if (cJSON_IsString(item)) {
        if (std::strlen(item->valuestring) != 6) {
            return false;
        }
        uint8_t channels[3] = {};
        for (int i = 0; i < 3; ++i) {
            const int high = hex_digit(item->valuestring[i * 2]);
            const int low = hex_digit(item->valuestring[i * 2 + 1]);
            if (high < 0 || low < 0) {
                return false;
            }
            channels[i] = static_cast<uint8_t>(high * 16 + low);
        }
        out = Rgb{channels[0], channels[1], channels[2]};
        return true;
    }
    if (cJSON_IsArray(item) && cJSON_GetArraySize(item) == 3) {
        uint8_t channels[3] = {};
        for (int i = 0; i < 3; ++i) {
            const cJSON* channel = cJSON_GetArrayItem(item, i);
            if (!cJSON_IsNumber(channel) || channel->valuedouble != channel->valueint || channel->valueint < 0 ||
                channel->valueint > 255) {
                return false;
            }
            channels[i] = static_cast<uint8_t>(channel->valueint);
        }
        out = Rgb{channels[0], channels[1], channels[2]};
        return true;
    }
    return false;
}

// Reads an optional integer field. Absent keeps the default; out of range fails.
template <typename T>
bool read_int(const cJSON* object, const char* key, int min, int max, T& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint || item->valueint < min ||
        item->valueint > max) {
        return false;
    }
    out = static_cast<T>(item->valueint);
    return true;
}

bool read_bool(const cJSON* object, const char* key, bool& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsBool(item)) {
        return false;
    }
    out = cJSON_IsTrue(item);
    return true;
}

void set_rainbow(Palette& palette) {
    const int last = static_cast<int>(kRainbowStops.size()) - 1;
    for (int i = 0; i <= last; ++i) {
        palette.stops[i] = GradientStop{static_cast<uint8_t>(i * 255 / last), kRainbowStops[i]};
    }
    palette.count = static_cast<uint8_t>(kRainbowStops.size());
}

bool parse_palette(const cJSON* item, Palette& palette, const char*& error) {
    if (cJSON_IsString(item)) {
        if (std::strcmp(item->valuestring, "Rainbow") == 0) {
            set_rainbow(palette);
            return true;
        }
        error = "unknown palette name";
        return false;
    }
    const int size = cJSON_IsArray(item) ? cJSON_GetArraySize(item) : 0;
    if (size < 4 || size % 2 != 0 || size / 2 > kMaxGradientStops) {
        error = "gradient must be 2 to 16 position and colour pairs";
        return false;
    }
    int previous = -1;
    for (int i = 0; i < size; i += 2) {
        const cJSON* position = cJSON_GetArrayItem(item, i);
        Rgb color{};
        if (!cJSON_IsNumber(position) || position->valuedouble != position->valueint ||
            position->valueint <= previous || position->valueint > 255 ||
            !parse_color(cJSON_GetArrayItem(item, i + 1), color)) {
            error = "gradient needs ascending positions 0-255 and valid colours";
            return false;
        }
        previous = position->valueint;
        palette.stops[i / 2] = GradientStop{static_cast<uint8_t>(previous), color};
    }
    palette.count = static_cast<uint8_t>(size / 2);
    return true;
}

void palette_from_colors(Layer& layer) {
    const int count = layer.col_count > 0 ? layer.col_count : 1;
    for (int i = 0; i < count; ++i) {
        const Rgb color = layer.col_count > 0 ? layer.col[i] : kDefaultColor;
        layer.palette.stops[i] = GradientStop{
            static_cast<uint8_t>(count > 1 ? i * 255 / (count - 1) : 0), color};
    }
    layer.palette.count = static_cast<uint8_t>(count);
}

bool parse_zone(const cJSON* item, Layer& layer, const char*& error) {
    if (cJSON_IsObject(item)) {
        int every = 0;
        if (!read_int(item, "every", 1, 255, every) || every == 0 ||
            !read_int(item, "offset", 0, 254, layer.offset) || layer.offset >= every) {
            error = "zone needs every 1-255 and a smaller offset";
            return false;
        }
        layer.every = static_cast<uint8_t>(every);
        layer.zone = ZoneKind::Every;
        return true;
    }
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) == 0) {
        error = "zone must be a list of figurines or an every/offset object";
        return false;
    }
    const cJSON* name = nullptr;
    cJSON_ArrayForEach(name, item) {
        int found = -1;
        for (int i = 0; i < kFigurineCount; ++i) {
            if (cJSON_IsString(name) && std::strcmp(name->valuestring, kFigurines[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            error = "unknown figurine in zone";
            return false;
        }
        layer.figurine_mask = static_cast<uint8_t>(layer.figurine_mask | (1u << found));
    }
    layer.zone = ZoneKind::Figurines;
    return true;
}

bool parse_layer(const cJSON* item, Layer& layer, const char*& error) {
    const cJSON* fx = cJSON_GetObjectItemCaseSensitive(item, "fx");
    bool known = false;
    if (cJSON_IsString(fx)) {
        for (const AlgorithmName& entry : kAlgorithms) {
            if (std::strcmp(fx->valuestring, entry.name) == 0) {
                layer.algorithm = entry.algorithm;
                known = true;
            }
        }
    }
    if (!known) {
        error = "unknown or missing fx";
        return false;
    }

    if (!read_int(item, "sx", 0, 255, layer.sx) || !read_int(item, "ix", 0, 255, layer.ix) ||
        !read_int(item, "bri", 0, 255, layer.bri) || !read_int(item, "start", 0, 65535, layer.start) ||
        !read_int(item, "stop", 0, 65535, layer.stop) || !read_int(item, "grp", 1, 255, layer.grp) ||
        !read_int(item, "spc", 0, 255, layer.spc) || !read_int(item, "of", -32768, 32767, layer.of) ||
        !read_bool(item, "rev", layer.rev) || !read_bool(item, "mi", layer.mi)) {
        error = "a numeric or boolean layer field is invalid";
        return false;
    }

    const cJSON* col = cJSON_GetObjectItemCaseSensitive(item, "col");
    if (col != nullptr) {
        const int size = cJSON_IsArray(col) ? cJSON_GetArraySize(col) : 0;
        if (size < 1 || size > kMaxLayerColors) {
            error = "col must hold 1 to 3 colours";
            return false;
        }
        for (int i = 0; i < size; ++i) {
            if (!parse_color(cJSON_GetArrayItem(col, i), layer.col[i])) {
                error = "invalid colour in col";
                return false;
            }
        }
        layer.col_count = static_cast<uint8_t>(size);
    }

    const cJSON* pal = cJSON_GetObjectItemCaseSensitive(item, "pal");
    if (pal != nullptr) {
        if (!parse_palette(pal, layer.palette, error)) {
            return false;
        }
        layer.explicit_palette = true;
    } else if (layer.algorithm == Algorithm::Rainbow) {
        set_rainbow(layer.palette);
    } else {
        palette_from_colors(layer);
    }

    const cJSON* zone = cJSON_GetObjectItemCaseSensitive(item, "zone");
    if (zone != nullptr && !parse_zone(zone, layer, error)) {
        return false;
    }

    const cJSON* blend = cJSON_GetObjectItemCaseSensitive(item, "blend");
    if (blend != nullptr) {
        if (cJSON_IsString(blend) && std::strcmp(blend->valuestring, "replace") == 0) {
            layer.blend = Blend::Replace;
        } else if (cJSON_IsString(blend) && std::strcmp(blend->valuestring, "add") == 0) {
            layer.blend = Blend::Add;
        } else if (cJSON_IsString(blend) && std::strcmp(blend->valuestring, "max") == 0) {
            layer.blend = Blend::Max;
        } else {
            error = "unknown blend";
            return false;
        }
    }
    return true;
}

// Names reach JSON responses and the display, so they stay printable and need no escaping.
bool is_safe_name(const char* name) {
    const size_t length = std::strlen(name);
    if (length == 0 || length > kMaxEffectNameLength) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (name[i] < 0x20 || name[i] > 0x7E || name[i] == '"' || name[i] == '\\') {
            return false;
        }
    }
    return true;
}

struct JsonDeleter {
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};

}  // namespace

bool parse_effect(const char* json, const char* id, EffectSpec& out, const char*& error) {
    const std::unique_ptr<cJSON, JsonDeleter> root(json_parse_shallow(json));
    if (root == nullptr) {
        // Said apart, as the text is JSON: see json_has_escaped_nul.
        error = json_has_escaped_nul(json) ? "a key or text holds \\u0000, which is refused: take it out" : "not valid JSON";
        return false;
    }
    const cJSON* format = cJSON_GetObjectItemCaseSensitive(root.get(), "format");
    if (!cJSON_IsString(format) || std::strcmp(format->valuestring, kEffectFormat) != 0) {
        error = "format is not csigarland.effect/1";
        return false;
    }
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root.get(), "name");
    if (!cJSON_IsString(name) || !is_safe_name(name->valuestring)) {
        error = "name must be 1 to 32 printable ASCII characters without quotes";
        return false;
    }
    if (std::strlen(id) == 0 || std::strlen(id) > kMaxEffectIdLength) {
        error = "id must be 1 to 32 characters";
        return false;
    }
    const cJSON* layers = cJSON_GetObjectItemCaseSensitive(root.get(), "layers");
    const int layer_count = cJSON_IsArray(layers) ? cJSON_GetArraySize(layers) : 0;
    if (layer_count < 1 || layer_count > kMaxLayers) {
        error = "layers must hold 1 to 4 layers";
        return false;
    }

    EffectSpec spec;
    std::snprintf(spec.id, sizeof(spec.id), "%s", id);
    std::snprintf(spec.name, sizeof(spec.name), "%s", name->valuestring);
    for (int i = 0; i < layer_count; ++i) {
        if (!parse_layer(cJSON_GetArrayItem(layers, i), spec.layers[i], error)) {
            return false;
        }
    }
    spec.layer_count = static_cast<uint8_t>(layer_count);
    out = spec;
    return true;
}

}  // namespace garland
