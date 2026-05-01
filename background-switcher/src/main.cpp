#include <cstdint.hpp>
#include <cstring.hpp>
#include <cmath.hpp>
#include <fcntl.h>
#include <math.h>
#include <new.hpp>
#include <service_protocol.hpp>
#ifdef NULL
#undef NULL
#endif
#define NULL 0

static double stbtt_local_fmod(double value, double modulus) {
    return std::fmod(value, modulus);
}

static double stbtt_local_cuberoot(double value) {
    if (value == 0.0) {
        return 0.0;
    }

    const double sign = value < 0.0 ? -1.0 : 1.0;
    double guess = std::fabs(value);
    if (guess < 1.0) {
        guess = 1.0;
    }

    for (int iteration = 0; iteration < 24; ++iteration) {
        guess = (2.0 * guess + std::fabs(value) / (guess * guess)) / 3.0;
    }
    return sign * guess;
}

static double stbtt_local_pow(double value, double exponent) {
    const double oneThird = 1.0 / 3.0;
    const double diff = exponent - oneThird;
    if (diff > -0.000001 && diff < 0.000001) {
        return stbtt_local_cuberoot(value);
    }

    if (exponent == 0.0) {
        return 1.0;
    }
    if (exponent == 1.0) {
        return value;
    }

    const double integer = std::floor(exponent);
    if (exponent == integer) {
        const bool negative = integer < 0.0;
        std::uint64_t count = static_cast<std::uint64_t>(negative ? -integer : integer);
        double result = 1.0;
        for (std::uint64_t index = 0; index < count; ++index) {
            result *= value;
        }
        return negative ? (result == 0.0 ? 0.0 : 1.0 / result) : result;
    }

    return 1.0;
}

static double stbtt_local_acos(double value) {
    if (value <= -1.0) {
        return 3.14159265358979323846;
    }
    if (value >= 1.0) {
        return 0.0;
    }
    return static_cast<double>(acosf(static_cast<float>(value)));
}

#define STBTT_ifloor(x) static_cast<int>(std::floor(x))
#define STBTT_iceil(x) static_cast<int>(std::ceil(x))
#define STBTT_sqrt(x) std::sqrt(x)
#define STBTT_fmod(x, y) stbtt_local_fmod((x), (y))
#define STBTT_pow(x, y) stbtt_local_pow((x), (y))
#define STBTT_cos(x) std::cos(x)
#define STBTT_acos(x) stbtt_local_acos((x))
#define STBTT_fabs(x) std::fabs(x)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <syscall.hpp>

namespace {
constexpr std::uint64_t fail = static_cast<std::uint64_t>(-1);
constexpr std::uint32_t kSurfaceWidth = 720;
constexpr std::uint32_t kSurfaceHeight = 460;
constexpr std::uint32_t kFontPixelHeight = 15;
constexpr std::uint32_t kFrameIntervalMs = 33;
constexpr std::uint32_t kMaxEntries = 32;
constexpr int kPaddingX = 0;
constexpr int kPaddingY = 16;
constexpr int kHeaderHeight = 0;
constexpr int kFooterHeight = 0;
constexpr int kRowHeight = 34;
constexpr char kFontPath[] = "/bin/JetBrainsMono-Regular.ttf";
constexpr char kBackgroundDirectory[] = "/bin/backgrounds";
constexpr unsigned char kFirstCachedGlyph = 32;
constexpr unsigned char kLastCachedGlyph = 126;
constexpr std::size_t kCachedGlyphCount = static_cast<std::size_t>(kLastCachedGlyph - kFirstCachedGlyph + 1);

constexpr std::uint32_t kColorBackgroundTop = 0x00131a25U;
constexpr std::uint32_t kColorBackgroundBottom = 0x001d2837U;
constexpr std::uint32_t kColorHeader = 0x00233446U;
constexpr std::uint32_t kColorPanel = 0x00141d2dU;
constexpr std::uint32_t kColorPanelSoft = 0x001b2739U;
constexpr std::uint32_t kColorSelected = 0x00398dd0U;
constexpr std::uint32_t kColorText = 0x00dff1eeU;
constexpr std::uint32_t kColorDim = 0x007f9fabU;
constexpr std::uint32_t kColorSuccess = 0x0087d89fU;
constexpr std::uint32_t kColorError = 0x00ec8d95U;

struct GlyphBitmap {
    unsigned char* pixels;
    int width;
    int height;
    int xOffset;
    int yOffset;
    int advance;
};

struct UIFont {
    bool valid;
    unsigned char* data;
    stbtt_fontinfo info;
    float scale;
    int ascent;
    int descent;
    int lineGap;
    int cellWidth;
    int lineHeight;
    int baseline;
    GlyphBitmap glyphs[kCachedGlyphCount];
};

struct BackgroundEntry {
    char name[128];
    char path[192];
};

struct AppState {
    BackgroundEntry entries[kMaxEntries];
    std::uint32_t entryCount;
    std::uint32_t selected;
    std::uint32_t scroll;
    bool focused;
    bool running;
    char status[160];
    std::uint32_t statusColor;
};

void write_str(const char* s) {
    std::write(std::STDOUT_HANDLE, s, std::strlen(s));
}

std::Handle connect_service(const char* name) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        const std::Handle handle = std::service_connect(name);
        if (handle != fail) {
            return handle;
        }
        std::yield();
    }

    return fail;
}

std::uint8_t color_r(std::uint32_t color) {
    return static_cast<std::uint8_t>((color >> 16) & 0xFFU);
}

std::uint8_t color_g(std::uint32_t color) {
    return static_cast<std::uint8_t>((color >> 8) & 0xFFU);
}

std::uint8_t color_b(std::uint32_t color) {
    return static_cast<std::uint8_t>(color & 0xFFU);
}

std::uint32_t pack_rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(b);
}

std::uint32_t blend_rgb(std::uint32_t dst, std::uint32_t src, std::uint8_t alpha) {
    const std::uint32_t inv = 255U - alpha;
    const std::uint32_t r = (color_r(dst) * inv + color_r(src) * alpha) / 255U;
    const std::uint32_t g = (color_g(dst) * inv + color_g(src) * alpha) / 255U;
    const std::uint32_t b = (color_b(dst) * inv + color_b(src) * alpha) / 255U;
    return pack_rgb(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b));
}

void fill_rect(
    std::uint32_t* pixels,
    std::uint32_t surfaceWidth,
    std::uint32_t surfaceHeight,
    int x,
    int y,
    int width,
    int height,
    std::uint32_t color
) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }

    int startX = x < 0 ? 0 : x;
    int startY = y < 0 ? 0 : y;
    int endX = x + width;
    int endY = y + height;
    if (endX > static_cast<int>(surfaceWidth)) {
        endX = static_cast<int>(surfaceWidth);
    }
    if (endY > static_cast<int>(surfaceHeight)) {
        endY = static_cast<int>(surfaceHeight);
    }

    for (int drawY = startY; drawY < endY; ++drawY) {
        for (int drawX = startX; drawX < endX; ++drawX) {
            pixels[drawY * surfaceWidth + drawX] = color;
        }
    }
}

void draw_gradient(std::uint32_t* pixels) {
    for (std::uint32_t y = 0; y < kSurfaceHeight; ++y) {
        const std::uint32_t mix = (y * 255U) / (kSurfaceHeight - 1U);
        const std::uint32_t red = ((color_r(kColorBackgroundTop) * (255U - mix)) +
                                   (color_r(kColorBackgroundBottom) * mix)) / 255U;
        const std::uint32_t green = ((color_g(kColorBackgroundTop) * (255U - mix)) +
                                     (color_g(kColorBackgroundBottom) * mix)) / 255U;
        const std::uint32_t blue = ((color_b(kColorBackgroundTop) * (255U - mix)) +
                                    (color_b(kColorBackgroundBottom) * mix)) / 255U;
        const std::uint32_t rowColor = pack_rgb(
            static_cast<std::uint8_t>(red),
            static_cast<std::uint8_t>(green),
            static_cast<std::uint8_t>(blue)
        );
        for (std::uint32_t x = 0; x < kSurfaceWidth; ++x) {
            pixels[y * kSurfaceWidth + x] = rowColor;
        }
    }
}

bool initialize_font(UIFont* font) {
    if (!font) {
        return false;
    }

    std::memset(font, 0, sizeof(*font));
    std::Stat stat = {};
    if (std::stat(kFontPath, &stat) == fail || stat.st_size == 0) {
        return false;
    }

    const std::Handle file = std::open(kFontPath, O_RDONLY);
    if (file == fail) {
        return false;
    }

    const std::size_t fontSize = static_cast<std::size_t>(stat.st_size);
    font->data = new (std::nothrow) unsigned char[fontSize];
    if (!font->data) {
        std::close(file);
        return false;
    }

    std::size_t totalRead = 0;
    while (totalRead < fontSize) {
        const std::uint64_t bytesRead = std::read(file, font->data + totalRead, fontSize - totalRead);
        if (bytesRead == fail || bytesRead == 0) {
            delete[] font->data;
            font->data = nullptr;
            std::close(file);
            std::memset(font, 0, sizeof(*font));
            return false;
        }
        totalRead += static_cast<std::size_t>(bytesRead);
    }
    std::close(file);

    const int fontOffset = stbtt_GetFontOffsetForIndex(font->data, 0);
    if (fontOffset < 0 || stbtt_InitFont(&font->info, font->data, fontOffset) == 0) {
        delete[] font->data;
        font->data = nullptr;
        std::memset(font, 0, sizeof(*font));
        return false;
    }

    font->scale = stbtt_ScaleForPixelHeight(&font->info, static_cast<float>(kFontPixelHeight));
    if (font->scale <= 0.0f) {
        delete[] font->data;
        font->data = nullptr;
        std::memset(font, 0, sizeof(*font));
        return false;
    }

    stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->lineGap);
    font->baseline = static_cast<int>(font->ascent * font->scale + 0.999f);
    font->lineHeight = static_cast<int>(((font->ascent - font->descent + font->lineGap) * font->scale) + 0.999f);
    if (font->lineHeight <= 0) {
        font->lineHeight = static_cast<int>(kFontPixelHeight) + 4;
    }

    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&font->info, 'M', &advance, &leftSideBearing);
    (void) leftSideBearing;
    font->cellWidth = static_cast<int>(advance * font->scale + 0.999f);
    if (font->cellWidth <= 0) {
        font->cellWidth = 8;
    }

    for (std::size_t index = 0; index < kCachedGlyphCount; ++index) {
        const int codepoint = static_cast<int>(kFirstCachedGlyph + index);
        GlyphBitmap& glyph = font->glyphs[index];

        int advanceWidth = 0;
        int glyphLeftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advanceWidth, &glyphLeftSideBearing);
        (void) glyphLeftSideBearing;

        glyph.advance = static_cast<int>(advanceWidth * font->scale + 0.999f);
        if (glyph.advance <= 0) {
            glyph.advance = font->cellWidth;
        }

        glyph.pixels = stbtt_GetCodepointBitmap(
            &font->info,
            font->scale,
            font->scale,
            codepoint,
            &glyph.width,
            &glyph.height,
            &glyph.xOffset,
            &glyph.yOffset
        );
        if (!glyph.pixels) {
            glyph.width = 0;
            glyph.height = 0;
            glyph.xOffset = 0;
            glyph.yOffset = 0;
        }
    }

    font->valid = true;
    return true;
}

void destroy_font(UIFont* font) {
    if (!font || !font->valid) {
        return;
    }

    for (std::size_t index = 0; index < kCachedGlyphCount; ++index) {
        if (font->glyphs[index].pixels) {
            stbtt_FreeBitmap(font->glyphs[index].pixels, nullptr);
            font->glyphs[index].pixels = nullptr;
        }
    }
    delete[] font->data;
    std::memset(font, 0, sizeof(*font));
}

void draw_text(
    std::uint32_t* pixels,
    std::uint32_t surfaceWidth,
    std::uint32_t surfaceHeight,
    UIFont& font,
    int x,
    int baselineY,
    const char* text,
    std::uint32_t color
) {
    if (!pixels || !font.valid || !text) {
        return;
    }

    int penX = x;
    for (std::size_t index = 0; text[index] != '\0'; ++index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch < kFirstCachedGlyph || ch > kLastCachedGlyph) {
            penX += font.cellWidth;
            continue;
        }

        const GlyphBitmap& glyph = font.glyphs[ch - kFirstCachedGlyph];
        if (!glyph.pixels || glyph.width <= 0 || glyph.height <= 0) {
            penX += glyph.advance > 0 ? glyph.advance : font.cellWidth;
            continue;
        }

        const int startX = penX + glyph.xOffset;
        const int startY = baselineY + glyph.yOffset;

        for (int drawY = 0; drawY < glyph.height; ++drawY) {
            const int dstY = startY + drawY;
            if (dstY < 0 || dstY >= static_cast<int>(surfaceHeight)) {
                continue;
            }

            for (int drawX = 0; drawX < glyph.width; ++drawX) {
                const int dstX = startX + drawX;
                if (dstX < 0 || dstX >= static_cast<int>(surfaceWidth)) {
                    continue;
                }

                const std::uint8_t alpha = glyph.pixels[drawY * glyph.width + drawX];
                if (alpha == 0) {
                    continue;
                }

                std::uint32_t& dst = pixels[dstY * surfaceWidth + dstX];
                dst = blend_rgb(dst, color, alpha);
            }
        }

        penX += glyph.advance > 0 ? glyph.advance : font.cellWidth;
    }
}

bool has_png_suffix(const char* name) {
    const std::size_t length = std::strlen(name);
    if (length < 4) {
        return false;
    }

    const char* suffix = name + length - 4;
    return (suffix[0] == '.' &&
            (suffix[1] == 'p' || suffix[1] == 'P') &&
            (suffix[2] == 'n' || suffix[2] == 'N') &&
            (suffix[3] == 'g' || suffix[3] == 'G'));
}

void append_text(char* buffer, std::size_t capacity, const char* text) {
    if (!buffer || capacity == 0 || !text) {
        return;
    }

    std::size_t len = std::strlen(buffer);
    std::size_t index = 0;
    while (text[index] != '\0' && len + 1 < capacity) {
        buffer[len++] = text[index++];
    }
    buffer[len] = '\0';
}

void set_status(AppState* state, const char* text, std::uint32_t color) {
    if (!state) {
        return;
    }

    std::strncpy(state->status, text ? text : "", sizeof(state->status) - 1);
    state->status[sizeof(state->status) - 1] = '\0';
    state->statusColor = color;
}

void sort_entries(AppState* state) {
    if (!state) {
        return;
    }

    for (std::uint32_t i = 0; i < state->entryCount; ++i) {
        for (std::uint32_t j = i + 1; j < state->entryCount; ++j) {
            if (std::strcmp(state->entries[j].name, state->entries[i].name) < 0) {
                BackgroundEntry temp = state->entries[i];
                state->entries[i] = state->entries[j];
                state->entries[j] = temp;
            }
        }
    }
}

bool load_backgrounds(AppState* state) {
    if (!state) {
        return false;
    }

    state->entryCount = 0;
    std::DirEntry entries[kMaxEntries] = {};
    const std::uint64_t found = std::readdir(kBackgroundDirectory, entries, kMaxEntries);
    if (found == fail) {
        set_status(state, "Failed to read /bin/backgrounds", kColorError);
        return false;
    }

    for (std::uint64_t index = 0; index < found && state->entryCount < kMaxEntries; ++index) {
        if (entries[index].type != std::FileType::Regular || !has_png_suffix(entries[index].name)) {
            continue;
        }

        BackgroundEntry& entry = state->entries[state->entryCount++];
        std::strncpy(entry.name, entries[index].name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.path[0] = '\0';
        append_text(entry.path, sizeof(entry.path), kBackgroundDirectory);
        append_text(entry.path, sizeof(entry.path), "/");
        append_text(entry.path, sizeof(entry.path), entries[index].name);
    }

    sort_entries(state);
    if (state->entryCount == 0) {
        set_status(state, "No PNG files found in /bin/backgrounds", kColorError);
        return false;
    }

    state->selected = 0;
    state->scroll = 0;
    set_status(state, "Enter applies the selected background", kColorDim);
    return true;
}

bool apply_background(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    const std::Handle compositor = connect_service(std::services::graphics_compositor::NAME);
    if (compositor == fail) {
        return false;
    }

    std::services::graphics_compositor::SetBackgroundRequest request = {};
    request.header.version = std::services::graphics_compositor::VERSION;
    request.header.opcode = static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::SetBackground);
    std::strncpy(request.path, path, sizeof(request.path) - 1);
    request.path[sizeof(request.path) - 1] = '\0';

    std::IPCMessage message = {};
    if (!std::services::encode_request(&message, request)) {
        std::close(compositor);
        return false;
    }

    std::services::graphics_compositor::SetBackgroundReply reply = {};
    std::uint64_t responseSize = 0;
    const std::uint64_t result = std::queue_request(compositor, &message, &reply, sizeof(reply), &responseSize);
    std::close(compositor);
    if (result == fail || responseSize < sizeof(reply)) {
        return false;
    }

    return reply.status == std::services::STATUS_OK;
}

void ensure_visible(AppState* state, std::uint32_t visibleRows) {
    if (!state || visibleRows == 0) {
        return;
    }

    if (state->selected < state->scroll) {
        state->scroll = state->selected;
    } else if (state->selected >= state->scroll + visibleRows) {
        state->scroll = state->selected - visibleRows + 1;
    }
}

void draw_ui(std::uint32_t* pixels, UIFont& font, const AppState& state) {
    draw_gradient(pixels);

    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 0, kHeaderHeight, kSurfaceWidth, kSurfaceHeight - kHeaderHeight - kFooterHeight, kColorPanel);
    
    const int listTop = kHeaderHeight + 12;
    const std::uint32_t visibleRows = static_cast<std::uint32_t>((kSurfaceHeight - kHeaderHeight - kFooterHeight - 24) / kRowHeight);

    if (state.entryCount == 0) {
        draw_text(pixels, kSurfaceWidth, kSurfaceHeight, font, kPaddingX, listTop + font.baseline, "(no backgrounds)", kColorDim);
    }

    for (std::uint32_t row = 0; row < visibleRows; ++row) {
        const std::uint32_t index = state.scroll + row;
        if (index >= state.entryCount) {
            break;
        }

        const int rowY = listTop + static_cast<int>(row) * kRowHeight;
        const bool selected = index == state.selected;
        fill_rect(
            pixels,
            kSurfaceWidth,
            kSurfaceHeight,
            kPaddingX - 6,
            rowY,
            static_cast<int>(kSurfaceWidth) - (kPaddingX * 2) + 12,
            kRowHeight - 4,
            selected ? kColorSelected : kColorPanelSoft
        );
        draw_text(
            pixels,
            kSurfaceWidth,
            kSurfaceHeight,
            font,
            kPaddingX + 8,
            rowY + font.baseline + 6,
            state.entries[index].name,
            kColorText
        );
    }
}

}

int main() {
    const std::Handle compositor = connect_service(std::services::graphics_compositor::NAME);
    if (compositor == fail) {
        write_str("[background-switcher] FAIL service_connect graphics.compositor\n");
        return 1;
    }

    const std::Handle surface = std::surface_create(kSurfaceWidth, kSurfaceHeight, std::services::surfaces::FORMAT_BGRA8);
    if (surface == fail) {
        std::close(compositor);
        return 1;
    }

    auto* pixels = static_cast<std::uint32_t*>(std::shared_map(surface));
    if (pixels == reinterpret_cast<std::uint32_t*>(fail) || pixels == nullptr) {
        std::close(surface);
        std::close(compositor);
        return 1;
    }

    const std::Handle window = std::compositor_create_window(compositor, kSurfaceWidth, kSurfaceHeight, 0);
    if (window == fail) {
        std::close(surface);
        std::close(compositor);
        return 1;
    }

    if (std::window_set_title(window, "Background Switcher") == fail ||
        std::window_attach_surface(window, surface) == fail) {
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }

    const std::Handle events = std::window_event_queue(window);
    if (events == fail) {
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }

    UIFont font = {};
    if (!initialize_font(&font)) {
        std::close(events);
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }

    AppState state = {};
    state.running = true;
    state.statusColor = kColorDim;
    load_backgrounds(&state);

    while (state.running) {
        std::uint32_t visibleRows = static_cast<std::uint32_t>((kSurfaceHeight - kHeaderHeight - kFooterHeight - 24) / kRowHeight);
        ensure_visible(&state, visibleRows == 0 ? 1 : visibleRows);

        for (;;) {
            std::Event event = {};
            if (std::event_poll(events, &event) == fail) {
                break;
            }

            if (event.type == std::EventType::Window) {
                if (event.window.action == std::WindowEventAction::FocusGained) {
                    state.focused = true;
                } else if (event.window.action == std::WindowEventAction::FocusLost) {
                    state.focused = false;
                } else if (event.window.action == std::WindowEventAction::CloseRequested) {
                    state.running = false;
                }
            } else if (event.type == std::EventType::Key && event.key.action == std::KeyEventAction::Press) {
                const char key = event.key.text[0] != '\0' ? event.key.text[0] : static_cast<char>(event.key.keycode);
                if (event.key.keycode == 27) {
                    state.running = false;
                } else if (event.key.keycode == '\n' || event.key.keycode == '\r' || key == ' ') {
                    if (state.entryCount != 0 && apply_background(state.entries[state.selected].path)) {
                        set_status(&state, "Background updated", kColorSuccess);
                    } else {
                        set_status(&state, "Failed to update background", kColorError);
                    }
                } else if (event.key.keycode == 'k' || event.key.keycode == 'K') {
                    if (state.selected > 0) {
                        state.selected--;
                    }
                } else if (event.key.keycode == 'j' || event.key.keycode == 'J') {
                    if (state.selected + 1 < state.entryCount) {
                        state.selected++;
                    }
                } else if (event.key.keycode == 'w' || event.key.keycode == 'W') {
                    if (state.selected > 0) {
                        state.selected--;
                    }
                } else if (event.key.keycode == 's' || event.key.keycode == 'S') {
                    if (state.selected + 1 < state.entryCount) {
                        state.selected++;
                    }
                }
            }
        }

        draw_ui(pixels, font, state);
        if (std::surface_commit(surface, 0, 0, kSurfaceWidth, kSurfaceHeight) == fail) {
            break;
        }
        std::sleep(kFrameIntervalMs);
    }

    destroy_font(&font);
    std::close(events);
    std::close(window);
    std::close(surface);
    std::close(compositor);
    return 0;
}
