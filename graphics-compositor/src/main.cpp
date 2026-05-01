#include <cstdint.hpp>
#include <cstring.hpp>
#include <cmath.hpp>
#include <fcntl.h>
#include <math.h>
#include <new.hpp>
#include <service_protocol.hpp>
#include <syscall.hpp>

#ifdef NULL
#undef NULL
#endif
#define NULL 0

#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

namespace {
constexpr std::uint64_t fail = static_cast<std::uint64_t>(-1);
constexpr std::uint32_t kFrameIntervalMs = 16;
constexpr std::uint32_t kCompositorBufferCount = 3;
constexpr std::uint32_t kBackground = 0x00101820;
constexpr std::uint32_t kBackgroundBottom = 0x001d2b3a;
constexpr std::uint32_t kWindowBackground = 0x00d7dde5;
constexpr std::uint32_t kWindowBorder = 0x001b1f26;
constexpr std::uint32_t kWindowBorderFocused = 0x00323a45;
constexpr std::uint32_t kTitleBar = 0x0012161d;
constexpr std::uint32_t kTitleBarFocused = 0x001b2028;
constexpr std::uint32_t kTitleBarHighlight = 0x00272f3a;
constexpr std::uint32_t kButtonClose = 0x00ff5f57;
constexpr std::uint32_t kButtonMin = 0x00ffbd2e;
constexpr std::uint32_t kButtonMax = 0x0028c840;
constexpr std::uint32_t kResizeGrip = 0x00556777;
constexpr std::uint32_t kTitleText = 0x00f2f5f7;
constexpr int kBorder = 2;
constexpr int kTitleBarHeight = 28;
constexpr int kWindowCornerRadius = 10;
constexpr int kButtonSize = 10;
constexpr int kButtonGap = 8;
constexpr int kButtonMarginLeft = 14;
constexpr int kResizeGripSize = 14;
constexpr std::uint64_t kMaxWindows = 64;
constexpr std::uint64_t kMaxSurfaceCache = 64;
constexpr char kUIFontPath[] = "/bin/JetBrainsMono-Regular.ttf";
constexpr char kBackgroundDirectory[] = "/bin/backgrounds";
constexpr char kDefaultBackground[] = "/bin/backgrounds/instantos_2.png";
constexpr char kDefaultCursorPath[] = "/bin/cursors/default.png";
constexpr int kMaxCursorSize = 24;
constexpr std::uint32_t kUIFontPixelHeight = 13;

struct UIFont {
    bool valid;
    unsigned char* data;
    stbtt_fontinfo info;
    float scale;
    int ascent;
    int descent;
    int lineGap;
    int lineHeight;
    int baseline;
};

struct CursorState {
    int x;
    int y;
};

struct Rect {
    int x;
    int y;
    int width;
    int height;
};

struct DragState {
    std::uint64_t windowId;
    bool moving;
    bool resizing;
    int grabOffsetX;
    int grabOffsetY;
    int anchorX;
    int anchorY;
    int startWidth;
    int startHeight;
};

struct FramebufferView {
    std::uint32_t* pixels;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
};

struct RGBATexel {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

struct CursorImage {
    RGBATexel* pixels;
    int width;
    int height;
    int hotspotX;
    int hotspotY;
    bool ready;
};

struct RenderBuffer {
    RGBATexel* pixels;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
};

struct TripleBufferState {
    RenderBuffer buffers[kCompositorBufferCount];
    std::uint32_t nextIndex;
};

struct SurfaceCacheEntry {
    bool valid;
    std::uint64_t id;
    std::SurfaceInfo info;
};

struct DesktopBackground {
    RGBATexel* pixels;
    std::uint32_t width;
    std::uint32_t height;
    bool ready;
    char currentPath[192];
};

static std::WindowInfo gWindowsScratch[kMaxWindows];
static SurfaceCacheEntry gSurfaceCache[kMaxSurfaceCache];
static UIFont gUIFont = {};

void write_str(const char* s) {
    std::write(std::STDOUT_HANDLE, s, std::strlen(s));
}

bool launch_file_browser() {
    return std::spawn("/bin/file-browser.exe") != fail;
}

bool launch_cube() {
    return std::spawn("/bin/cube.exe") != fail;
}

bool launch_background_switcher() {
    return std::spawn("/bin/background-switcher.exe") != fail;
}

bool decode_event_message(const std::IPCMessage& message, std::Event* event) {
    return std::event_from_message(message, event);
}

RGBATexel rgba_from_rgb(std::uint32_t color) {
    RGBATexel pixel = {};
    pixel.r = static_cast<std::uint8_t>((color >> 16) & 0xFFU);
    pixel.g = static_cast<std::uint8_t>((color >> 8) & 0xFFU);
    pixel.b = static_cast<std::uint8_t>(color & 0xFFU);
    pixel.a = 0xFFU;
    return pixel;
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

    std::size_t length = std::strlen(buffer);
    std::size_t index = 0;
    while (text[index] != '\0' && length + 1 < capacity) {
        buffer[length++] = text[index++];
    }
    buffer[length] = '\0';
}

std::uint32_t rgb_to_screen(const RGBATexel& pixel) {
    return (static_cast<std::uint32_t>(pixel.r) << 16) |
           (static_cast<std::uint32_t>(pixel.g) << 8) |
           static_cast<std::uint32_t>(pixel.b);
}

RGBATexel blend(const RGBATexel& dst, const RGBATexel& src, std::uint8_t alpha);

void clear_buffer(const RenderBuffer& buffer, const RGBATexel& color) {
    for (std::uint32_t y = 0; y < buffer.height; ++y) {
        for (std::uint32_t x = 0; x < buffer.width; ++x) {
            buffer.pixels[y * buffer.pitch + x] = color;
        }
    }
}

void draw_gradient(const RenderBuffer& buffer) {
    const RGBATexel top = rgba_from_rgb(kBackground);
    const RGBATexel bottom = rgba_from_rgb(kBackgroundBottom);
    for (std::uint32_t y = 0; y < buffer.height; ++y) {
        const std::uint32_t mix = (y * 255U) / (buffer.height > 1 ? (buffer.height - 1U) : 1U);
        RGBATexel color = {};
        color.r = static_cast<std::uint8_t>((top.r * (255U - mix) + bottom.r * mix) / 255U);
        color.g = static_cast<std::uint8_t>((top.g * (255U - mix) + bottom.g * mix) / 255U);
        color.b = static_cast<std::uint8_t>((top.b * (255U - mix) + bottom.b * mix) / 255U);
        color.a = 0xFFU;
        for (std::uint32_t x = 0; x < buffer.width; ++x) {
            buffer.pixels[y * buffer.pitch + x] = color;
        }
    }
}

bool load_cursor_image(CursorImage* cursor, const char* path) {
    if (!cursor || !path || path[0] == '\0') {
        return false;
    }

    std::memset(cursor, 0, sizeof(*cursor));

    std::Stat stat = {};
    if (std::stat(path, &stat) == fail || stat.st_size == 0) {
        return false;
    }

    const std::Handle file = std::open(path, O_RDONLY);
    if (file == fail) {
        return false;
    }

    const std::size_t fileSize = static_cast<std::size_t>(stat.st_size);
    unsigned char* encoded = new (std::nothrow) unsigned char[fileSize];
    if (!encoded) {
        std::close(file);
        return false;
    }

    std::size_t totalRead = 0;
    while (totalRead < fileSize) {
        const std::uint64_t bytesRead = std::read(file, encoded + totalRead, fileSize - totalRead);
        if (bytesRead == fail || bytesRead == 0) {
            delete[] encoded;
            std::close(file);
            return false;
        }
        totalRead += static_cast<std::size_t>(bytesRead);
    }
    std::close(file);

    int imageWidth = 0;
    int imageHeight = 0;
    int components = 0;
    unsigned char* imagePixels = stbi_load_from_memory(encoded, static_cast<int>(fileSize), &imageWidth, &imageHeight, &components, 4);
    delete[] encoded;
    if (!imagePixels || imageWidth <= 0 || imageHeight <= 0) {
        return false;
    }

    int targetWidth = imageWidth;
    int targetHeight = imageHeight;
    if (targetWidth > kMaxCursorSize || targetHeight > kMaxCursorSize) {
        if (targetWidth >= targetHeight) {
            targetHeight = (targetHeight * kMaxCursorSize) / targetWidth;
            targetWidth = kMaxCursorSize;
        } else {
            targetWidth = (targetWidth * kMaxCursorSize) / targetHeight;
            targetHeight = kMaxCursorSize;
        }
        if (targetWidth < 1) {
            targetWidth = 1;
        }
        if (targetHeight < 1) {
            targetHeight = 1;
        }
    }

    cursor->pixels = new (std::nothrow) RGBATexel[static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight)];
    if (!cursor->pixels) {
        stbi_image_free(imagePixels);
        return false;
    }

    for (int y = 0; y < targetHeight; ++y) {
        const double srcTop = (static_cast<double>(y) * static_cast<double>(imageHeight)) / static_cast<double>(targetHeight);
        const double srcBottom = (static_cast<double>(y + 1) * static_cast<double>(imageHeight)) / static_cast<double>(targetHeight);
        int startY = static_cast<int>(std::floor(srcTop));
        int endY = static_cast<int>(std::ceil(srcBottom));
        if (startY < 0) {
            startY = 0;
        }
        if (endY > imageHeight) {
            endY = imageHeight;
        }

        for (int x = 0; x < targetWidth; ++x) {
            const double srcLeft = (static_cast<double>(x) * static_cast<double>(imageWidth)) / static_cast<double>(targetWidth);
            const double srcRight = (static_cast<double>(x + 1) * static_cast<double>(imageWidth)) / static_cast<double>(targetWidth);
            int startX = static_cast<int>(std::floor(srcLeft));
            int endX = static_cast<int>(std::ceil(srcRight));
            if (startX < 0) {
                startX = 0;
            }
            if (endX > imageWidth) {
                endX = imageWidth;
            }

            double totalWeight = 0.0;
            double alphaSum = 0.0;
            double redSum = 0.0;
            double greenSum = 0.0;
            double blueSum = 0.0;

            for (int sampleY = startY; sampleY < endY; ++sampleY) {
                const double overlapTop = srcTop > static_cast<double>(sampleY) ? srcTop : static_cast<double>(sampleY);
                const double overlapBottom = srcBottom < static_cast<double>(sampleY + 1) ? srcBottom : static_cast<double>(sampleY + 1);
                const double yWeight = overlapBottom - overlapTop;
                if (yWeight <= 0.0) {
                    continue;
                }

                for (int sampleX = startX; sampleX < endX; ++sampleX) {
                    const double overlapLeft = srcLeft > static_cast<double>(sampleX) ? srcLeft : static_cast<double>(sampleX);
                    const double overlapRight = srcRight < static_cast<double>(sampleX + 1) ? srcRight : static_cast<double>(sampleX + 1);
                    const double xWeight = overlapRight - overlapLeft;
                    if (xWeight <= 0.0) {
                        continue;
                    }

                    const double weight = xWeight * yWeight;
                    const unsigned char* src = imagePixels + ((sampleY * imageWidth + sampleX) * 4);
                    const double alpha = (static_cast<double>(src[3]) / 255.0) * weight;
                    totalWeight += weight;
                    alphaSum += alpha;
                    redSum += static_cast<double>(src[0]) * alpha;
                    greenSum += static_cast<double>(src[1]) * alpha;
                    blueSum += static_cast<double>(src[2]) * alpha;
                }
            }

            const double outAlpha = totalWeight > 0.0 ? (alphaSum / totalWeight) : 0.0;
            RGBATexel& dst = cursor->pixels[y * targetWidth + x];
            if (outAlpha <= 0.0) {
                dst = {};
                continue;
            }

            dst.r = static_cast<std::uint8_t>((redSum / alphaSum) + 0.5);
            dst.g = static_cast<std::uint8_t>((greenSum / alphaSum) + 0.5);
            dst.b = static_cast<std::uint8_t>((blueSum / alphaSum) + 0.5);
            dst.a = static_cast<std::uint8_t>((outAlpha * 255.0) + 0.5);
        }
    }

    stbi_image_free(imagePixels);
    cursor->width = targetWidth;
    cursor->height = targetHeight;
    cursor->hotspotX = 0;
    cursor->hotspotY = 0;
    cursor->ready = true;
    return true;
}

void destroy_cursor_image(CursorImage* cursor) {
    if (!cursor) {
        return;
    }

    delete[] cursor->pixels;
    std::memset(cursor, 0, sizeof(*cursor));
}

void fill_rect(const RenderBuffer& buffer, int x, int y, int width, int height, const RGBATexel& color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    int startX = x < 0 ? 0 : x;
    int startY = y < 0 ? 0 : y;
    int endX = x + width;
    int endY = y + height;
    if (endX > static_cast<int>(buffer.width)) {
        endX = static_cast<int>(buffer.width);
    }
    if (endY > static_cast<int>(buffer.height)) {
        endY = static_cast<int>(buffer.height);
    }

    for (int drawY = startY; drawY < endY; ++drawY) {
        for (int drawX = startX; drawX < endX; ++drawX) {
            buffer.pixels[drawY * buffer.pitch + drawX] = color;
        }
    }
}

void blend_pixel(const RenderBuffer& buffer, int x, int y, const RGBATexel& color, std::uint8_t alpha) {
    if (alpha == 0) {
        return;
    }
    if (x < 0 || y < 0 || x >= static_cast<int>(buffer.width) || y >= static_cast<int>(buffer.height)) {
        return;
    }

    RGBATexel& dst = buffer.pixels[y * buffer.pitch + x];
    dst = alpha == 255 ? color : blend(dst, color, alpha);
}

int clamp_radius(int width, int height, int radius) {
    if (radius <= 0) {
        return 0;
    }

    const int maxRadiusX = width / 2;
    const int maxRadiusY = height / 2;
    if (radius > maxRadiusX) {
        radius = maxRadiusX;
    }
    if (radius > maxRadiusY) {
        radius = maxRadiusY;
    }
    return radius;
}

bool point_in_rounded_rect(int x, int y, int rectX, int rectY, int width, int height, int radius) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (x < rectX || y < rectY || x >= rectX + width || y >= rectY + height) {
        return false;
    }

    if (radius <= 0) {
        return true;
    }

    radius = clamp_radius(width, height, radius);

    const int left = rectX + radius;
    const int right = rectX + width - radius - 1;
    const int top = rectY + radius;
    const int bottom = rectY + height - radius - 1;
    if ((x >= left && x <= right) || (y >= top && y <= bottom)) {
        return true;
    }

    const int centerX = x < left ? left : right;
    const int centerY = y < top ? top : bottom;
    const int dx = x - centerX;
    const int dy = y - centerY;
    return (dx * dx) + (dy * dy) <= (radius * radius);
}

bool point_in_top_rounded_rect(int x, int y, int rectX, int rectY, int width, int height, int radius) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (x < rectX || y < rectY || x >= rectX + width || y >= rectY + height) {
        return false;
    }

    if (radius <= 0) {
        return true;
    }

    radius = clamp_radius(width, height, radius);
    if (y >= rectY + radius) {
        return true;
    }

    const int leftCenterX = rectX + radius;
    const int rightCenterX = rectX + width - radius - 1;
    const int centerY = rectY + radius;
    if (x >= leftCenterX && x <= rightCenterX) {
        return true;
    }

    const int centerX = x < leftCenterX ? leftCenterX : rightCenterX;
    const int dx = x - centerX;
    const int dy = y - centerY;
    return (dx * dx) + (dy * dy) <= (radius * radius);
}

bool point_in_rounded_rect_sample(double x, double y, int rectX, int rectY, int width, int height, int radius) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (x < static_cast<double>(rectX) || y < static_cast<double>(rectY) ||
        x >= static_cast<double>(rectX + width) || y >= static_cast<double>(rectY + height)) {
        return false;
    }

    radius = clamp_radius(width, height, radius);
    if (radius <= 0) {
        return true;
    }

    const double left = static_cast<double>(rectX + radius);
    const double right = static_cast<double>(rectX + width - radius - 1);
    const double top = static_cast<double>(rectY + radius);
    const double bottom = static_cast<double>(rectY + height - radius - 1);
    if ((x >= left && x <= right) || (y >= top && y <= bottom)) {
        return true;
    }

    const double centerX = x < left ? left : right;
    const double centerY = y < top ? top : bottom;
    const double dx = x - centerX;
    const double dy = y - centerY;
    return (dx * dx) + (dy * dy) <= static_cast<double>(radius * radius);
}

bool point_in_top_rounded_rect_sample(double x, double y, int rectX, int rectY, int width, int height, int radius) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (x < static_cast<double>(rectX) || y < static_cast<double>(rectY) ||
        x >= static_cast<double>(rectX + width) || y >= static_cast<double>(rectY + height)) {
        return false;
    }

    radius = clamp_radius(width, height, radius);
    if (radius <= 0) {
        return true;
    }

    if (y >= static_cast<double>(rectY + radius)) {
        return true;
    }

    const double leftCenterX = static_cast<double>(rectX + radius);
    const double rightCenterX = static_cast<double>(rectX + width - radius - 1);
    const double centerY = static_cast<double>(rectY + radius);
    if (x >= leftCenterX && x <= rightCenterX) {
        return true;
    }

    const double centerX = x < leftCenterX ? leftCenterX : rightCenterX;
    const double dx = x - centerX;
    const double dy = y - centerY;
    return (dx * dx) + (dy * dy) <= static_cast<double>(radius * radius);
}

std::uint8_t sample_coverage_4x4(bool (*contains)(double, double, int, int, int, int, int),
                                 int rectX, int rectY, int width, int height, int radius,
                                 int pixelX, int pixelY) {
    static constexpr double kOffsets[4] = { 0.125, 0.375, 0.625, 0.875 };
    int inside = 0;
    for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
            if (contains(static_cast<double>(pixelX) + kOffsets[sx],
                         static_cast<double>(pixelY) + kOffsets[sy],
                         rectX, rectY, width, height, radius)) {
                ++inside;
            }
        }
    }

    return static_cast<std::uint8_t>((inside * 255 + 8) / 16);
}

void fill_rounded_rect(const RenderBuffer& buffer, int x, int y, int width, int height, int radius, const RGBATexel& color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    radius = clamp_radius(width, height, radius);
    const int startX = x < 0 ? 0 : x;
    const int startY = y < 0 ? 0 : y;
    int endX = x + width;
    int endY = y + height;
    if (endX > static_cast<int>(buffer.width)) {
        endX = static_cast<int>(buffer.width);
    }
    if (endY > static_cast<int>(buffer.height)) {
        endY = static_cast<int>(buffer.height);
    }

    for (int drawY = startY; drawY < endY; ++drawY) {
        for (int drawX = startX; drawX < endX; ++drawX) {
            if (radius <= 0 ||
                ((drawX >= x + radius) && (drawX < x + width - radius)) ||
                ((drawY >= y + radius) && (drawY < y + height - radius))) {
                blend_pixel(buffer, drawX, drawY, color, 255);
                continue;
            }

            const std::uint8_t alpha = sample_coverage_4x4(point_in_rounded_rect_sample, x, y, width, height, radius, drawX, drawY);
            if (alpha == 0) {
                continue;
            }
            blend_pixel(buffer, drawX, drawY, color, alpha);
        }
    }
}

void fill_top_rounded_rect(const RenderBuffer& buffer, int x, int y, int width, int height, int radius, const RGBATexel& color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    radius = clamp_radius(width, height, radius);
    const int startX = x < 0 ? 0 : x;
    const int startY = y < 0 ? 0 : y;
    int endX = x + width;
    int endY = y + height;
    if (endX > static_cast<int>(buffer.width)) {
        endX = static_cast<int>(buffer.width);
    }
    if (endY > static_cast<int>(buffer.height)) {
        endY = static_cast<int>(buffer.height);
    }

    for (int drawY = startY; drawY < endY; ++drawY) {
        for (int drawX = startX; drawX < endX; ++drawX) {
            if (radius <= 0 || drawY >= y + radius || (drawX >= x + radius && drawX < x + width - radius)) {
                blend_pixel(buffer, drawX, drawY, color, 255);
                continue;
            }

            const std::uint8_t alpha = sample_coverage_4x4(point_in_top_rounded_rect_sample, x, y, width, height, radius, drawX, drawY);
            if (alpha == 0) {
                continue;
            }
            blend_pixel(buffer, drawX, drawY, color, alpha);
        }
    }
}

RGBATexel blend(const RGBATexel& dst, const RGBATexel& src, std::uint8_t alpha) {
    const std::uint32_t inv = 255U - alpha;
    RGBATexel out = {};
    out.r = static_cast<std::uint8_t>((dst.r * inv + src.r * alpha) / 255U);
    out.g = static_cast<std::uint8_t>((dst.g * inv + src.g * alpha) / 255U);
    out.b = static_cast<std::uint8_t>((dst.b * inv + src.b * alpha) / 255U);
    out.a = 0xFFU;
    return out;
}

void draw_gradient_pixels(RGBATexel* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t pitch) {
    if (!pixels || width == 0 || height == 0) {
        return;
    }

    const RenderBuffer buffer = { pixels, width, height, pitch };
    draw_gradient(buffer);
}

bool ensure_background_storage(DesktopBackground* background, std::uint32_t width, std::uint32_t height) {
    if (!background || width == 0 || height == 0) {
        return false;
    }

    if (background->pixels && background->width == width && background->height == height) {
        return true;
    }

    delete[] background->pixels;
    background->pixels = new (std::nothrow) RGBATexel[static_cast<std::size_t>(width) * static_cast<std::size_t>(height)];
    if (!background->pixels) {
        background->width = 0;
        background->height = 0;
        background->ready = false;
        background->currentPath[0] = '\0';
        return false;
    }

    background->width = width;
    background->height = height;
    background->ready = false;
    background->currentPath[0] = '\0';
    return true;
}

void scale_background_image(DesktopBackground* background, int imageWidth, int imageHeight, const unsigned char* imagePixels) {
    if (!background || !background->pixels || !imagePixels || imageWidth <= 0 || imageHeight <= 0) {
        return;
    }

    draw_gradient_pixels(background->pixels, background->width, background->height, background->width);

    std::uint32_t drawWidth = background->width;
    std::uint32_t drawHeight = static_cast<std::uint32_t>((static_cast<std::uint64_t>(imageHeight) * background->width) / imageWidth);
    if (drawHeight > background->height) {
        drawHeight = background->height;
        drawWidth = static_cast<std::uint32_t>((static_cast<std::uint64_t>(imageWidth) * background->height) / imageHeight);
    }
    if (drawWidth == 0 || drawHeight == 0) {
        return;
    }

    const int offsetX = static_cast<int>((background->width - drawWidth) / 2U);
    const int offsetY = static_cast<int>((background->height - drawHeight) / 2U);
    for (std::uint32_t y = 0; y < drawHeight; ++y) {
        const int srcY = static_cast<int>((static_cast<std::uint64_t>(y) * imageHeight) / drawHeight);
        for (std::uint32_t x = 0; x < drawWidth; ++x) {
            const int srcX = static_cast<int>((static_cast<std::uint64_t>(x) * imageWidth) / drawWidth);
            const unsigned char* src = imagePixels + ((srcY * imageWidth + srcX) * 4);
            RGBATexel pixel = {};
            pixel.r = src[0];
            pixel.g = src[1];
            pixel.b = src[2];
            pixel.a = 0xFFU;
            RGBATexel& dst = background->pixels[(offsetY + static_cast<int>(y)) * background->width + offsetX + static_cast<int>(x)];
            dst = src[3] == 255 ? pixel : blend(dst, pixel, src[3]);
        }
    }
}

bool load_background(DesktopBackground* background, std::uint32_t width, std::uint32_t height, const char* path) {
    if (!background || !path || path[0] == '\0' || !ensure_background_storage(background, width, height)) {
        return false;
    }

    std::Stat stat = {};
    if (std::stat(path, &stat) == fail || stat.st_size == 0) {
        draw_gradient_pixels(background->pixels, background->width, background->height, background->width);
        background->ready = false;
        return false;
    }

    const std::Handle file = std::open(path, O_RDONLY);
    if (file == fail) {
        draw_gradient_pixels(background->pixels, background->width, background->height, background->width);
        background->ready = false;
        return false;
    }

    const std::size_t fileSize = static_cast<std::size_t>(stat.st_size);
    unsigned char* encoded = new (std::nothrow) unsigned char[fileSize];
    if (!encoded) {
        std::close(file);
        return false;
    }

    std::size_t totalRead = 0;
    while (totalRead < fileSize) {
        const std::uint64_t bytesRead = std::read(file, encoded + totalRead, fileSize - totalRead);
        if (bytesRead == fail || bytesRead == 0) {
            delete[] encoded;
            std::close(file);
            draw_gradient_pixels(background->pixels, background->width, background->height, background->width);
            background->ready = false;
            return false;
        }
        totalRead += static_cast<std::size_t>(bytesRead);
    }
    std::close(file);

    int imageWidth = 0;
    int imageHeight = 0;
    int components = 0;
    unsigned char* imagePixels = stbi_load_from_memory(encoded, static_cast<int>(fileSize), &imageWidth, &imageHeight, &components, 4);
    delete[] encoded;
    if (!imagePixels || imageWidth <= 0 || imageHeight <= 0) {
        draw_gradient_pixels(background->pixels, background->width, background->height, background->width);
        background->ready = false;
        return false;
    }

    scale_background_image(background, imageWidth, imageHeight, imagePixels);
    stbi_image_free(imagePixels);

    std::strncpy(background->currentPath, path, sizeof(background->currentPath) - 1);
    background->currentPath[sizeof(background->currentPath) - 1] = '\0';
    background->ready = true;
    return true;
}

bool load_first_available_background(DesktopBackground* background, std::uint32_t width, std::uint32_t height) {
    if (load_background(background, width, height, kDefaultBackground)) {
        return true;
    }

    std::DirEntry entries[32] = {};
    const std::uint64_t found = std::readdir(kBackgroundDirectory, entries, 32);
    if (found == fail) {
        return false;
    }

    for (std::uint64_t index = 0; index < found; ++index) {
        if (entries[index].type != std::FileType::Regular || !has_png_suffix(entries[index].name)) {
            continue;
        }

        char path[192] = {};
        append_text(path, sizeof(path), kBackgroundDirectory);
        append_text(path, sizeof(path), "/");
        append_text(path, sizeof(path), entries[index].name);
        if (load_background(background, width, height, path)) {
            return true;
        }
    }

    return false;
}

void draw_background(const RenderBuffer& buffer, const DesktopBackground* background) {
    if (!background || !background->pixels || !background->ready ||
        background->width != buffer.width || background->height != buffer.height) {
        draw_gradient(buffer);
        return;
    }

    for (std::uint32_t y = 0; y < buffer.height; ++y) {
        std::memcpy(
            &buffer.pixels[y * buffer.pitch],
            &background->pixels[y * background->width],
            sizeof(RGBATexel) * buffer.width
        );
    }
}

bool initialize_ui_font(UIFont* font) {
    if (!font) {
        return false;
    }

    std::memset(font, 0, sizeof(*font));
    std::Stat stat = {};
    if (std::stat(kUIFontPath, &stat) == fail || stat.st_size == 0) {
        return false;
    }

    const std::Handle file = std::open(kUIFontPath, O_RDONLY);
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

    font->scale = stbtt_ScaleForPixelHeight(&font->info, static_cast<float>(kUIFontPixelHeight));
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
        font->lineHeight = static_cast<int>(kUIFontPixelHeight) + 4;
    }

    font->valid = true;
    return true;
}

void destroy_ui_font(UIFont* font) {
    if (!font || !font->valid) {
        return;
    }

    delete[] font->data;
    std::memset(font, 0, sizeof(*font));
}

int text_width(UIFont& font, const char* text) {
    if (!font.valid || !text) {
        return 0;
    }

    int width = 0;
    for (std::size_t index = 0; text[index] != '\0'; ++index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch < 32 || ch > 126) {
            width += 6;
            continue;
        }

        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&font.info, static_cast<int>(ch), &advance, &leftSideBearing);
        (void) leftSideBearing;
        width += static_cast<int>(advance * font.scale + 0.999f);
    }

    return width;
}

void draw_text(const RenderBuffer& buffer, UIFont& font, int x, int y, const char* text, const RGBATexel& color) {
    if (!font.valid || !text) {
        return;
    }

    int penX = x;
    int baselineY = y;
    for (std::size_t i = 0; text[i] != '\0'; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '\n') {
            penX = x;
            baselineY += font.lineHeight;
            continue;
        }
        if (ch < 32 || ch > 126) {
            penX += 6;
            continue;
        }

        int width = 0;
        int height = 0;
        int xOffset = 0;
        int yOffset = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            &font.info,
            font.scale,
            font.scale,
            static_cast<int>(ch),
            &width,
            &height,
            &xOffset,
            &yOffset
        );
        if (!bitmap) {
            penX += 6;
            continue;
        }

        const int startX = penX + xOffset;
        const int startY = baselineY + yOffset;

        for (int drawY = 0; drawY < height; ++drawY) {
            const int dstY = startY + drawY;
            if (dstY < 0 || dstY >= static_cast<int>(buffer.height)) {
                continue;
            }

            for (int drawX = 0; drawX < width; ++drawX) {
                const int dstX = startX + drawX;
                if (dstX < 0 || dstX >= static_cast<int>(buffer.width)) {
                    continue;
                }

                const std::uint8_t alpha = bitmap[drawY * width + drawX];
                if (alpha == 0) {
                    continue;
                }

                RGBATexel& dst = buffer.pixels[dstY * buffer.pitch + dstX];
                dst = blend(dst, color, alpha);
            }
        }

        stbtt_FreeBitmap(bitmap, nullptr);

        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&font.info, static_cast<int>(ch), &advance, &leftSideBearing);
        (void) leftSideBearing;
        penX += static_cast<int>(advance * font.scale + 0.999f);
    }
}

void fill_circle(const RenderBuffer& buffer, int centerX, int centerY, int radius, const RGBATexel& color) {
    if (radius <= 0) {
        return;
    }

    static constexpr double kOffsets[4] = { 0.125, 0.375, 0.625, 0.875 };
    const int startX = centerX - radius - 1;
    const int endX = centerX + radius + 1;
    const int startY = centerY - radius - 1;
    const int endY = centerY + radius + 1;
    const double radiusSquared = static_cast<double>(radius * radius);

    for (int drawY = startY; drawY <= endY; ++drawY) {
        for (int drawX = startX; drawX <= endX; ++drawX) {
            int inside = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const double sampleX = (static_cast<double>(drawX) + kOffsets[sx]) - static_cast<double>(centerX);
                    const double sampleY = (static_cast<double>(drawY) + kOffsets[sy]) - static_cast<double>(centerY);
                    if ((sampleX * sampleX) + (sampleY * sampleY) <= radiusSquared) {
                        ++inside;
                    }
                }
            }

            if (inside == 0) {
                continue;
            }

            const std::uint8_t alpha = static_cast<std::uint8_t>((inside * 255 + 8) / 16);
            blend_pixel(buffer, drawX, drawY, color, alpha);
        }
    }
}

void draw_cursor(const RenderBuffer& buffer, const CursorState& cursor, const CursorImage& cursorImage) {
    if (!cursorImage.ready || !cursorImage.pixels) {
        return;
    }

    const int originX = cursor.x - cursorImage.hotspotX;
    const int originY = cursor.y - cursorImage.hotspotY;
    for (int y = 0; y < cursorImage.height; ++y) {
        for (int x = 0; x < cursorImage.width; ++x) {
            const int drawX = originX + x;
            const int drawY = originY + y;
            if (drawX < 0 || drawY < 0 ||
                drawX >= static_cast<int>(buffer.width) || drawY >= static_cast<int>(buffer.height)) {
                continue;
            }

            const RGBATexel& src = cursorImage.pixels[y * cursorImage.width + x];
            if (src.a == 0) {
                continue;
            }
            blend_pixel(buffer, drawX, drawY, src, src.a);
        }
    }
}

Rect cursor_rect(const CursorState& cursor, const CursorImage& cursorImage) {
    if (!cursorImage.ready) {
        return { cursor.x, cursor.y, 0, 0 };
    }
    return { cursor.x - cursorImage.hotspotX, cursor.y - cursorImage.hotspotY, cursorImage.width, cursorImage.height };
}

bool rect_is_empty(const Rect& rect) {
    return rect.width <= 0 || rect.height <= 0;
}

Rect union_rect(const Rect& a, const Rect& b) {
    if (rect_is_empty(a)) {
        return b;
    }
    if (rect_is_empty(b)) {
        return a;
    }

    const int left = a.x < b.x ? a.x : b.x;
    const int top = a.y < b.y ? a.y : b.y;
    const int rightA = a.x + a.width;
    const int rightB = b.x + b.width;
    const int bottomA = a.y + a.height;
    const int bottomB = b.y + b.height;
    const int right = rightA > rightB ? rightA : rightB;
    const int bottom = bottomA > bottomB ? bottomA : bottomB;
    return { left, top, right - left, bottom - top };
}

Rect clamp_rect(const Rect& rect, std::uint32_t width, std::uint32_t height) {
    Rect clamped = rect;
    if (clamped.x < 0) {
        clamped.width += clamped.x;
        clamped.x = 0;
    }
    if (clamped.y < 0) {
        clamped.height += clamped.y;
        clamped.y = 0;
    }

    const int maxWidth = static_cast<int>(width);
    const int maxHeight = static_cast<int>(height);
    if (clamped.x + clamped.width > maxWidth) {
        clamped.width = maxWidth - clamped.x;
    }
    if (clamped.y + clamped.height > maxHeight) {
        clamped.height = maxHeight - clamped.y;
    }
    if (clamped.width < 0) {
        clamped.width = 0;
    }
    if (clamped.height < 0) {
        clamped.height = 0;
    }
    return clamped;
}

void present_rect_to_framebuffer(const RenderBuffer& source, const FramebufferView& target, const Rect& rect) {
    const Rect clipped = clamp_rect(rect, source.width < target.width ? source.width : target.width,
        source.height < target.height ? source.height : target.height);
    if (rect_is_empty(clipped)) {
        return;
    }

    for (int y = 0; y < clipped.height; ++y) {
        const int srcY = clipped.y + y;
        for (int x = 0; x < clipped.width; ++x) {
            const int srcX = clipped.x + x;
            target.pixels[srcY * target.pitch + srcX] = rgb_to_screen(source.pixels[srcY * source.pitch + srcX]);
        }
    }

    std::fb_flush(
        static_cast<std::uint32_t>(clipped.x),
        static_cast<std::uint32_t>(clipped.y),
        static_cast<std::uint32_t>(clipped.width),
        static_cast<std::uint32_t>(clipped.height)
    );
}

void draw_cursor_to_framebuffer(const FramebufferView& fb, const CursorState& cursor, const CursorImage& cursorImage) {
    if (!cursorImage.ready || !cursorImage.pixels) {
        return;
    }

    const int originX = cursor.x - cursorImage.hotspotX;
    const int originY = cursor.y - cursorImage.hotspotY;
    for (int y = 0; y < cursorImage.height; ++y) {
        for (int x = 0; x < cursorImage.width; ++x) {
            const int drawX = originX + x;
            const int drawY = originY + y;
            if (drawX < 0 || drawY < 0 ||
                drawX >= static_cast<int>(fb.width) || drawY >= static_cast<int>(fb.height)) {
                continue;
            }

            const RGBATexel& src = cursorImage.pixels[y * cursorImage.width + x];
            if (src.a == 0) {
                continue;
            }
            const std::uint32_t base = fb.pixels[drawY * fb.pitch + drawX];
            RGBATexel dst = {};
            dst.r = static_cast<std::uint8_t>((base >> 16) & 0xFFU);
            dst.g = static_cast<std::uint8_t>((base >> 8) & 0xFFU);
            dst.b = static_cast<std::uint8_t>(base & 0xFFU);
            dst.a = 0xFFU;
            fb.pixels[drawY * fb.pitch + drawX] = rgb_to_screen(src.a == 255 ? src : blend(dst, src, src.a));
        }
    }
}

void present_cursor_move(const RenderBuffer& scene, const FramebufferView& fb, const CursorState& oldCursor, const CursorState& newCursor, const CursorImage& cursorImage) {
    const Rect dirty = clamp_rect(union_rect(cursor_rect(oldCursor, cursorImage), cursor_rect(newCursor, cursorImage)), fb.width, fb.height);
    if (rect_is_empty(dirty)) {
        return;
    }

    for (int y = 0; y < dirty.height; ++y) {
        const int drawY = dirty.y + y;
        for (int x = 0; x < dirty.width; ++x) {
            const int drawX = dirty.x + x;
            RGBATexel color = scene.pixels[drawY * scene.pitch + drawX];

            const int cursorLocalX = drawX - (newCursor.x - cursorImage.hotspotX);
            const int cursorLocalY = drawY - (newCursor.y - cursorImage.hotspotY);
            if (cursorImage.ready &&
                cursorLocalX >= 0 && cursorLocalY >= 0 &&
                cursorLocalX < cursorImage.width && cursorLocalY < cursorImage.height) {
                const RGBATexel& src = cursorImage.pixels[cursorLocalY * cursorImage.width + cursorLocalX];
                if (src.a != 0) {
                    color = src.a == 255 ? src : blend(color, src, src.a);
                }
            }

            fb.pixels[drawY * fb.pitch + drawX] = rgb_to_screen(color);
        }
    }

    std::fb_flush(
        static_cast<std::uint32_t>(dirty.x),
        static_cast<std::uint32_t>(dirty.y),
        static_cast<std::uint32_t>(dirty.width),
        static_cast<std::uint32_t>(dirty.height)
    );
}

void move_cursor(const FramebufferView& fb, CursorState* cursor, int x, int y) {
    if (!cursor) {
        return;
    }

    int maxX = static_cast<int>(fb.width) - 1;
    int maxY = static_cast<int>(fb.height) - 1;
    if (maxX < 0) maxX = 0;
    if (maxY < 0) maxY = 0;
    cursor->x = x < 0 ? 0 : (x > maxX ? maxX : x);
    cursor->y = y < 0 ? 0 : (y > maxY ? maxY : y);
}

bool build_hello_reply(const std::IPCMessage& message, std::services::graphics_compositor::HelloReply* reply) {
    if (!reply) {
        return false;
    }

    std::services::graphics_compositor::HelloRequest request = {};
    if (!std::services::decode_message(message, &request)) {
        return false;
    }

    std::memset(reply, 0, sizeof(*reply));
    reply->header.version = std::services::graphics_compositor::VERSION;
    reply->header.opcode = static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::Hello);
    reply->status = std::services::STATUS_OK;
    std::strncpy(reply->service_name, std::services::graphics_compositor::NAME, sizeof(reply->service_name) - 1);

    if (request.header.version != std::services::graphics_compositor::VERSION) {
        reply->status = std::services::STATUS_BAD_VERSION;
    } else if (request.header.opcode != static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::Hello)) {
        reply->status = std::services::STATUS_BAD_OPCODE;
    }

    return true;
}

bool point_in_rect(int px, int py, int x, int y, int width, int height) {
    return px >= x && py >= y && px < (x + width) && py < (y + height);
}

int frame_width(const std::WindowInfo& window) {
    return window.width + (kBorder * 2);
}

int frame_height(const std::WindowInfo& window) {
    return window.height + kTitleBarHeight + kBorder;
}

bool is_window_visible(const std::WindowInfo& window) {
    return (window.state & (std::WindowStateMinimized | std::WindowStateClosed)) == 0;
}

SurfaceCacheEntry* find_surface_cache(SurfaceCacheEntry* cache, std::uint64_t surfaceId) {
    if (surfaceId == 0) {
        return nullptr;
    }

    for (std::uint64_t i = 0; i < kMaxSurfaceCache; ++i) {
        if (cache[i].valid && cache[i].id == surfaceId) {
            return &cache[i];
        }
    }

    return nullptr;
}

void remember_surface(SurfaceCacheEntry* cache, const std::SurfaceInfo& info) {
    SurfaceCacheEntry* entry = find_surface_cache(cache, info.id);
    if (!entry) {
        for (std::uint64_t i = 0; i < kMaxSurfaceCache; ++i) {
            if (!cache[i].valid) {
                entry = &cache[i];
                break;
            }
        }
    }
    if (!entry) {
        entry = &cache[0];
    }

    entry->valid = true;
    entry->id = info.id;
    entry->info = info;
}

bool pump_surface_updates(SurfaceCacheEntry* cache) {
    bool changed = false;
    for (;;) {
        std::SurfaceInfo surface = {};
        if (std::surface_poll(&surface) == fail) {
            break;
        }

        remember_surface(cache, surface);
        changed = true;
    }
    return changed;
}

void draw_window_frame(const RenderBuffer& buffer, const std::WindowInfo& window) {
    const bool focused = (window.state & std::WindowStateFocused) != 0;
    const int frameX = window.x;
    const int frameY = window.y;
    const int totalWidth = frame_width(window);
    const int totalHeight = frame_height(window);
    const int innerX = frameX + kBorder;
    const int innerY = frameY + kBorder;
    const int innerWidth = window.width;
    const int innerHeight = totalHeight - (kBorder * 2);
    const int innerRadius = kWindowCornerRadius > kBorder ? (kWindowCornerRadius - kBorder) : 0;

    fill_rounded_rect(buffer, frameX, frameY, totalWidth, totalHeight, kWindowCornerRadius, rgba_from_rgb(focused ? kWindowBorderFocused : kWindowBorder));
    fill_rounded_rect(buffer, innerX, innerY, innerWidth, innerHeight, innerRadius, rgba_from_rgb(kWindowBackground));
    fill_top_rounded_rect(buffer, innerX, innerY, innerWidth, kTitleBarHeight - kBorder, innerRadius, rgba_from_rgb(focused ? kTitleBarFocused : kTitleBar));
    fill_top_rounded_rect(buffer, innerX, innerY, innerWidth, 1, innerRadius, rgba_from_rgb(kTitleBarHighlight));

    const int buttonCenterY = frameY + (kTitleBarHeight / 2) + 1;
    const int closeCenterX = frameX + kBorder + kButtonMarginLeft;
    const int minCenterX = closeCenterX + kButtonSize + kButtonGap;
    const int maxCenterX = minCenterX + kButtonSize + kButtonGap;
    fill_circle(buffer, closeCenterX, buttonCenterY, kButtonSize / 2, rgba_from_rgb(kButtonClose));
    fill_circle(buffer, minCenterX, buttonCenterY, kButtonSize / 2, rgba_from_rgb(kButtonMin));
    fill_circle(buffer, maxCenterX, buttonCenterY, kButtonSize / 2, rgba_from_rgb(kButtonMax));

    const int gripX = frameX + totalWidth - kBorder - kResizeGripSize;
    const int gripY = frameY + totalHeight - kBorder - kResizeGripSize;
    fill_rect(buffer, gripX, gripY, kResizeGripSize, kResizeGripSize, rgba_from_rgb(kResizeGrip));

    const char* title = window.title[0] ? window.title : "Window";
    const int titleWidth = text_width(gUIFont, title);
    int titleX = frameX + kBorder + ((window.width - titleWidth) / 2);
    const int minTitleX = frameX + kBorder + kButtonMarginLeft + (kButtonSize * 3) + (kButtonGap * 2) + 14;
    if (titleX < minTitleX) {
        titleX = minTitleX;
    }
    draw_text(buffer, gUIFont, titleX, frameY + 19, title, rgba_from_rgb(kTitleText));
}

void blit_window_surface(const RenderBuffer& buffer, const std::WindowInfo& window, const SurfaceCacheEntry* surfaceEntry) {
    if (!surfaceEntry || !surfaceEntry->valid) {
        return;
    }

    const auto* src = reinterpret_cast<const std::uint32_t*>(surfaceEntry->info.address);
    if (!src) {
        return;
    }

    const int destX = window.x + kBorder;
    const int destY = window.y + kTitleBarHeight;
    const int innerRadius = kWindowCornerRadius > kBorder ? (kWindowCornerRadius - kBorder) : 0;
    const int clipBottomBandStart = window.y + frame_height(window) - kBorder - innerRadius;
    const std::uint32_t sourcePitch = surfaceEntry->info.pitch >= 4
        ? surfaceEntry->info.pitch / 4
        : surfaceEntry->info.width;
    const std::uint32_t copyWidth = surfaceEntry->info.width < static_cast<std::uint32_t>(window.width)
        ? surfaceEntry->info.width
        : static_cast<std::uint32_t>(window.width);
    const std::uint32_t copyHeight = surfaceEntry->info.height < static_cast<std::uint32_t>(window.height)
        ? surfaceEntry->info.height
        : static_cast<std::uint32_t>(window.height);

    for (std::uint32_t y = 0; y < copyHeight; ++y) {
        const int drawY = destY + static_cast<int>(y);
        if (drawY < 0 || drawY >= static_cast<int>(buffer.height)) {
            continue;
        }

        for (std::uint32_t x = 0; x < copyWidth; ++x) {
            const int drawX = destX + static_cast<int>(x);
            if (drawX < 0 || drawX >= static_cast<int>(buffer.width)) {
                continue;
            }
            if (innerRadius > 0 &&
                drawY >= clipBottomBandStart &&
                !point_in_rounded_rect(drawX, drawY, window.x + kBorder, window.y + kBorder,
                                       window.width, frame_height(window) - (kBorder * 2), innerRadius)) {
                continue;
            }

            buffer.pixels[drawY * buffer.pitch + drawX] = rgba_from_rgb(src[y * sourcePitch + x]);
        }
    }
}

std::uint64_t fetch_windows(std::WindowInfo* windows, std::uint64_t capacity) {
    const std::uint64_t count = std::compositor_list_windows(windows, capacity);
    if (count == fail) {
        return 0;
    }
    return count;
}

void redraw_scene(const RenderBuffer& buffer, SurfaceCacheEntry* cache, const DesktopBackground* background) {
    draw_background(buffer, background);

    std::memset(gWindowsScratch, 0, sizeof(gWindowsScratch));
    const std::uint64_t count = fetch_windows(gWindowsScratch, kMaxWindows);
    for (std::uint64_t i = 0; i < count; ++i) {
        if (!is_window_visible(gWindowsScratch[i])) {
            continue;
        }

        draw_window_frame(buffer, gWindowsScratch[i]);
        blit_window_surface(buffer, gWindowsScratch[i], find_surface_cache(cache, gWindowsScratch[i].surfaceID));
    }

}

bool build_background_reply(
    const std::IPCMessage& message,
    std::services::graphics_compositor::SetBackgroundReply* reply,
    DesktopBackground* background,
    std::uint32_t width,
    std::uint32_t height,
    bool* sceneDirty
) {
    if (!reply || !background || !sceneDirty) {
        return false;
    }

    std::services::graphics_compositor::SetBackgroundRequest request = {};
    if (!std::services::decode_message(message, &request)) {
        return false;
    }

    std::memset(reply, 0, sizeof(*reply));
    reply->header.version = std::services::graphics_compositor::VERSION;
    reply->header.opcode = static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::SetBackground);
    reply->status = std::services::STATUS_BAD_PAYLOAD;

    if (request.header.version != std::services::graphics_compositor::VERSION) {
        reply->status = std::services::STATUS_BAD_VERSION;
        return true;
    }
    if (request.header.opcode != static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::SetBackground)) {
        reply->status = std::services::STATUS_BAD_OPCODE;
        return true;
    }

    if (request.path[0] != '\0' && load_background(background, width, height, request.path)) {
        reply->status = std::services::STATUS_OK;
        *sceneDirty = true;
    }
    return true;
}

void present_to_framebuffer(const RenderBuffer& source, const FramebufferView& target) {
    const std::uint32_t copyWidth = source.width < target.width ? source.width : target.width;
    const std::uint32_t copyHeight = source.height < target.height ? source.height : target.height;

    for (std::uint32_t y = 0; y < copyHeight; ++y) {
        for (std::uint32_t x = 0; x < copyWidth; ++x) {
            target.pixels[y * target.pitch + x] = rgb_to_screen(source.pixels[y * source.pitch + x]);
        }
    }

    std::fb_flush(0, 0, copyWidth, copyHeight);
}

bool initialize_triple_buffers(TripleBufferState* state, std::uint32_t width, std::uint32_t height) {
    if (!state || width == 0 || height == 0) {
        return false;
    }

    std::memset(state, 0, sizeof(*state));
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::uint32_t i = 0; i < kCompositorBufferCount; ++i) {
        state->buffers[i].pixels = new (std::nothrow) RGBATexel[pixelCount];
        if (state->buffers[i].pixels == nullptr) {
            return false;
        }
        state->buffers[i].width = width;
        state->buffers[i].height = height;
        state->buffers[i].pitch = width;
        clear_buffer(state->buffers[i], rgba_from_rgb(kBackground));
    }

    state->nextIndex = 0;
    return true;
}

RenderBuffer& acquire_render_buffer(TripleBufferState* state) {
    RenderBuffer& buffer = state->buffers[state->nextIndex];
    state->nextIndex = (state->nextIndex + 1U) % kCompositorBufferCount;
    return buffer;
}

const std::WindowInfo* top_window_at(const std::WindowInfo* windows, std::uint64_t count, int x, int y) {
    for (std::uint64_t i = count; i > 0; --i) {
        const std::WindowInfo& window = windows[i - 1];
        if (!is_window_visible(window)) {
            continue;
        }

        if (point_in_rounded_rect(x, y, window.x, window.y, frame_width(window), frame_height(window), kWindowCornerRadius)) {
            return &window;
        }
    }

    return nullptr;
}

bool pointer_on_close(const std::WindowInfo& window, int x, int y) {
    const int centerX = window.x + kBorder + kButtonMarginLeft;
    const int centerY = window.y + (kTitleBarHeight / 2) + 1;
    const int dx = x - centerX;
    const int dy = y - centerY;
    return (dx * dx) + (dy * dy) <= ((kButtonSize / 2) * (kButtonSize / 2));
}

bool pointer_on_maximize(const std::WindowInfo& window, int x, int y) {
    const int centerX = window.x + kBorder + kButtonMarginLeft + (kButtonSize * 2) + (kButtonGap * 2);
    const int centerY = window.y + (kTitleBarHeight / 2) + 1;
    const int dx = x - centerX;
    const int dy = y - centerY;
    return (dx * dx) + (dy * dy) <= ((kButtonSize / 2) * (kButtonSize / 2));
}

bool pointer_on_minimize(const std::WindowInfo& window, int x, int y) {
    const int centerX = window.x + kBorder + kButtonMarginLeft + kButtonSize + kButtonGap;
    const int centerY = window.y + (kTitleBarHeight / 2) + 1;
    const int dx = x - centerX;
    const int dy = y - centerY;
    return (dx * dx) + (dy * dy) <= ((kButtonSize / 2) * (kButtonSize / 2));
}

bool pointer_on_resize_grip(const std::WindowInfo& window, int x, int y) {
    return point_in_rect(
        x,
        y,
        window.x + frame_width(window) - kBorder - kResizeGripSize,
        window.y + frame_height(window) - kBorder - kResizeGripSize,
        kResizeGripSize,
        kResizeGripSize
    );
}

bool pointer_on_titlebar(const std::WindowInfo& window, int x, int y) {
    return point_in_top_rounded_rect(x, y, window.x + kBorder, window.y + kBorder, window.width, kTitleBarHeight - kBorder,
                                     kWindowCornerRadius > kBorder ? (kWindowCornerRadius - kBorder) : 0);
}

void begin_move(DragState* drag, const std::WindowInfo& window, int pointerX, int pointerY) {
    drag->windowId = window.id;
    drag->moving = true;
    drag->resizing = false;
    drag->grabOffsetX = pointerX - window.x;
    drag->grabOffsetY = pointerY - window.y;
}

void begin_resize(DragState* drag, const std::WindowInfo& window, int pointerX, int pointerY) {
    drag->windowId = window.id;
    drag->moving = false;
    drag->resizing = true;
    drag->anchorX = pointerX;
    drag->anchorY = pointerY;
    drag->startWidth = window.width;
    drag->startHeight = window.height;
}

void clear_drag(DragState* drag) {
    drag->windowId = 0;
    drag->moving = false;
    drag->resizing = false;
}

bool handle_pointer_event(const std::Event& event, DragState* drag, std::uint16_t* buttons) {
    if (event.type != std::EventType::Pointer) {
        return false;
    }

    bool redraw = false;
    const std::uint16_t previousButtons = *buttons;
    *buttons = event.pointer.buttons;
    const bool leftWasDown = (previousButtons & 0x1u) != 0;
    const bool leftIsDown = (event.pointer.buttons & 0x1u) != 0;

    if (drag->moving && leftIsDown) {
        if (std::compositor_move_window(
                drag->windowId,
                event.pointer.x - drag->grabOffsetX,
                event.pointer.y - drag->grabOffsetY) != fail) {
            redraw = true;
        }
    } else if (drag->resizing && leftIsDown) {
        int newWidth = drag->startWidth + (event.pointer.x - drag->anchorX);
        int newHeight = drag->startHeight + (event.pointer.y - drag->anchorY);
        if (newWidth < 64) newWidth = 64;
        if (newHeight < 48) newHeight = 48;
        if (std::compositor_resize_window(drag->windowId, newWidth, newHeight) != fail) {
            redraw = true;
        }
    }

    if (!leftWasDown && leftIsDown) {
        std::memset(gWindowsScratch, 0, sizeof(gWindowsScratch));
        const std::uint64_t count = fetch_windows(gWindowsScratch, kMaxWindows);
        const std::WindowInfo* window = top_window_at(gWindowsScratch, count, event.pointer.x, event.pointer.y);
        if (window) {
            std::compositor_focus_window(window->id);
            redraw = true;

            if (pointer_on_close(*window, event.pointer.x, event.pointer.y)) {
                std::compositor_control_window(window->id, std::WindowControlAction::Close);
                clear_drag(drag);
                return true;
            }
            if (pointer_on_maximize(*window, event.pointer.x, event.pointer.y)) {
                const std::WindowControlAction action =
                    (window->state & std::WindowStateMaximized) != 0
                    ? std::WindowControlAction::Restore
                    : std::WindowControlAction::Maximize;
                std::compositor_control_window(window->id, action);
                clear_drag(drag);
                return true;
            }
            if (pointer_on_minimize(*window, event.pointer.x, event.pointer.y)) {
                std::compositor_control_window(window->id, std::WindowControlAction::Minimize);
                clear_drag(drag);
                return true;
            }
            if (pointer_on_resize_grip(*window, event.pointer.x, event.pointer.y)) {
                begin_resize(drag, *window, event.pointer.x, event.pointer.y);
                return true;
            }
            if (pointer_on_titlebar(*window, event.pointer.x, event.pointer.y)) {
                begin_move(drag, *window, event.pointer.x, event.pointer.y);
                return true;
            }
        }
    }

    if (leftWasDown && !leftIsDown) {
        clear_drag(drag);
    }

    return redraw;
}
}

int main() {
    const std::Handle queue = std::queue_create();
    if (queue == fail) {
        write_str("[graphics.compositor] queue_create failed\n");
        return 1;
    }

    if (std::service_register(std::services::graphics_compositor::NAME, queue) == fail) {
        write_str("[graphics.compositor] service_register failed\n");
        std::close(queue);
        return 1;
    }

    std::FBInfo info = {};
    if (std::fb_info(&info) == fail) {
        write_str("[graphics.compositor] fb_info failed\n");
        std::close(queue);
        return 1;
    }

    auto* pixels = static_cast<std::uint32_t*>(std::fb_map());
    if (pixels == reinterpret_cast<std::uint32_t*>(fail)) {
        pixels = reinterpret_cast<std::uint32_t*>(info.addr);
    }
    if (pixels == nullptr || pixels == reinterpret_cast<std::uint32_t*>(fail)) {
        write_str("[graphics.compositor] fb_map failed\n");
        std::close(queue);
        return 1;
    }

    if (!initialize_ui_font(&gUIFont)) {
        write_str("[graphics.compositor] ui font load failed, continuing without text\n");
    }

    const FramebufferView fb = {
        pixels,
        info.width,
        info.height,
        info.pitch
    };

    TripleBufferState buffers = {};
    if (!initialize_triple_buffers(&buffers, info.width, info.height)) {
        write_str("[graphics.compositor] triple buffer allocation failed\n");
        std::close(queue);
        return 1;
    }

    CursorState cursor = {};
    cursor.x = static_cast<int>(info.width / 2);
    cursor.y = static_cast<int>(info.height / 2);

    std::memset(gSurfaceCache, 0, sizeof(gSurfaceCache));
    DragState drag = {};
    std::uint16_t pointerButtons = 0;
    DesktopBackground background = {};
    load_first_available_background(&background, info.width, info.height);
    CursorImage cursorImage = {};
    if (!load_cursor_image(&cursorImage, kDefaultCursorPath)) {
        write_str("[graphics.compositor] cursor load failed\n");
    }

    RenderBuffer* sceneBuffer = &acquire_render_buffer(&buffers);
    redraw_scene(*sceneBuffer, gSurfaceCache, &background);
    present_to_framebuffer(*sceneBuffer, fb);
    present_cursor_move(*sceneBuffer, fb, cursor, cursor, cursorImage);

    write_str("[graphics.compositor] ready\n");

    auto handle_message = [&](const std::IPCMessage& message, bool* sceneDirty) {
        if ((message.flags & std::IPC_MESSAGE_EVENT) != 0) {
            std::Event event = {};
            if (decode_event_message(message, &event)) {
                if (event.type == std::EventType::Pointer) {
                    const CursorState previousCursor = cursor;
                    move_cursor(fb, &cursor, event.pointer.x, event.pointer.y);
                    if (handle_pointer_event(event, &drag, &pointerButtons)) {
                        *sceneDirty = true;
                    } else if (!*sceneDirty && (previousCursor.x != cursor.x || previousCursor.y != cursor.y)) {
                        present_cursor_move(*sceneBuffer, fb, previousCursor, cursor, cursorImage);
                    }
                } else if (event.type == std::EventType::Key && event.key.action == std::KeyEventAction::Press) {
                    const bool superPressed = (event.key.modifiers & std::KeyModifierSuper) != 0;
                    const char key = event.key.text[0] != '\0' ? event.key.text[0] : static_cast<char>(event.key.keycode);
                    if (superPressed && (key == 'f' || key == 'F')) {
                        if (!launch_file_browser()) {
                            write_str("[graphics.compositor] FAIL spawn file-browser.exe\n");
                        }
                    } else if (superPressed && (key == 'b' || key == 'B')) {
                        if (!launch_background_switcher()) {
                            write_str("[graphics.compositor] FAIL spawn background-switcher.exe\n");
                        }
                    } else if (superPressed && (key == 'c' || key == 'C')) {
                        if (!launch_cube()) {
                            write_str("[graphics.compositor] FAIL spawn cube.exe\n");
                        }
                    }
                }
            }
            return;
        }

        if ((message.flags & std::IPC_MESSAGE_REQUEST) == 0) {
            return;
        }

        std::services::MessageHeader header = {};
        if (!std::services::decode_message(message, &header)) {
            write_str("[graphics.compositor] invalid request payload\n");
            return;
        }

        if (header.opcode == static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::Hello)) {
            std::services::graphics_compositor::HelloReply reply = {};
            if (!build_hello_reply(message, &reply)) {
                write_str("[graphics.compositor] invalid hello payload\n");
                return;
            }

            if (std::queue_reply(queue, message.id, &reply, sizeof(reply)) == fail) {
                write_str("[graphics.compositor] queue_reply failed\n");
            }
            return;
        }

        if (header.opcode == static_cast<std::uint16_t>(std::services::graphics_compositor::Opcode::SetBackground)) {
            std::services::graphics_compositor::SetBackgroundReply reply = {};
            if (!build_background_reply(message, &reply, &background, info.width, info.height, sceneDirty)) {
                write_str("[graphics.compositor] invalid background payload\n");
                return;
            }

            if (std::queue_reply(queue, message.id, &reply, sizeof(reply)) == fail) {
                write_str("[graphics.compositor] queue_reply failed\n");
            }
        }
    };

    for (;;) {
        bool sceneDirty = pump_surface_updates(gSurfaceCache);

        if (!sceneDirty) {
            std::IPCMessage message = {};
            if (std::queue_receive(queue, &message, true) != fail) {
                handle_message(message, &sceneDirty);
            }
        }

        for (;;) {
            std::IPCMessage message = {};
            if (std::queue_receive(queue, &message, false) == fail) {
                break;
            }
            handle_message(message, &sceneDirty);
        }

        if (pump_surface_updates(gSurfaceCache)) {
            sceneDirty = true;
        }

        if (sceneDirty) {
            sceneBuffer = &acquire_render_buffer(&buffers);
            redraw_scene(*sceneBuffer, gSurfaceCache, &background);
            present_to_framebuffer(*sceneBuffer, fb);
            present_cursor_move(*sceneBuffer, fb, cursor, cursor, cursorImage);
        }
    }

    destroy_cursor_image(&cursorImage);
    delete[] background.pixels;
    std::close(queue);
    return 1;
}
