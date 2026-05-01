#include <cstdio.hpp>
#include <cstring.hpp>
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

namespace {
constexpr std::uint64_t fail = static_cast<std::uint64_t>(-1);
constexpr std::uint32_t kSurfaceWidth = 900;
constexpr std::uint32_t kSurfaceHeight = 620;
constexpr std::uint32_t kAnimationTickMs = 120;
constexpr char kBackgroundDirectory[] = "/bin/backgrounds";
constexpr char kDefaultBackground[] = "/bin/backgrounds/instantos_2.png";

constexpr std::uint32_t kColorBackgroundTop = 0x00141b2dU;
constexpr std::uint32_t kColorBackgroundBottom = 0x001e3048U;
constexpr std::uint32_t kColorPanel = 0x0026394dU;
constexpr std::uint32_t kColorAccent = 0x00e39d3fU;
constexpr std::uint32_t kColorAccentSoft = 0x004d86b8U;
constexpr std::uint32_t kColorTileA = 0x00344f71U;
constexpr std::uint32_t kColorTileB = 0x00273c59U;

struct DesktopState {
    std::uint32_t* wallpaper;
    bool wallpaperReady;
    bool focused;
    std::uint32_t activitySeed;
    char currentBackground[192];
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

bool launch_file_browser() {
    return std::spawn("/bin/file-browser.exe") != fail;
}

bool launch_background_switcher() {
    return std::spawn("/bin/background-switcher.exe") != fail;
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
    if (endX > static_cast<int>(surfaceWidth)) endX = static_cast<int>(surfaceWidth);
    if (endY > static_cast<int>(surfaceHeight)) endY = static_cast<int>(surfaceHeight);

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

void scale_image_to_wallpaper(
    std::uint32_t* wallpaper,
    int imageWidth,
    int imageHeight,
    const unsigned char* imagePixels
) {
    if (!wallpaper || !imagePixels || imageWidth <= 0 || imageHeight <= 0) {
        return;
    }

    draw_gradient(wallpaper);

    std::uint32_t drawWidth = kSurfaceWidth;
    std::uint32_t drawHeight = static_cast<std::uint32_t>((static_cast<std::uint64_t>(imageHeight) * kSurfaceWidth) / imageWidth);
    if (drawHeight > kSurfaceHeight) {
        drawHeight = kSurfaceHeight;
        drawWidth = static_cast<std::uint32_t>((static_cast<std::uint64_t>(imageWidth) * kSurfaceHeight) / imageHeight);
    }

    if (drawWidth == 0 || drawHeight == 0) {
        return;
    }

    const int offsetX = static_cast<int>((kSurfaceWidth - drawWidth) / 2U);
    const int offsetY = static_cast<int>((kSurfaceHeight - drawHeight) / 2U);

    for (std::uint32_t y = 0; y < drawHeight; ++y) {
        const int srcY = static_cast<int>((static_cast<std::uint64_t>(y) * imageHeight) / drawHeight);
        for (std::uint32_t x = 0; x < drawWidth; ++x) {
            const int srcX = static_cast<int>((static_cast<std::uint64_t>(x) * imageWidth) / drawWidth);
            const unsigned char* src = imagePixels + ((srcY * imageWidth + srcX) * 4);
            const std::uint32_t srcColor = pack_rgb(src[0], src[1], src[2]);
            const std::uint8_t alpha = src[3];
            std::uint32_t& dst = wallpaper[(offsetY + static_cast<int>(y)) * kSurfaceWidth + offsetX + static_cast<int>(x)];
            dst = alpha == 255 ? srcColor : blend_rgb(dst, srcColor, alpha);
        }
    }
}

bool load_background(DesktopState* state, const char* path) {
    if (!state || !path || path[0] == '\0') {
        return false;
    }

    if (!state->wallpaper) {
        state->wallpaper = new (std::nothrow) std::uint32_t[kSurfaceWidth * kSurfaceHeight];
        if (!state->wallpaper) {
            return false;
        }
    }

    std::Stat stat = {};
    if (std::stat(path, &stat) == fail || stat.st_size == 0) {
        draw_gradient(state->wallpaper);
        state->wallpaperReady = false;
        return false;
    }

    const std::Handle file = std::open(path, O_RDONLY);
    if (file == fail) {
        draw_gradient(state->wallpaper);
        state->wallpaperReady = false;
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
            draw_gradient(state->wallpaper);
            state->wallpaperReady = false;
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
        draw_gradient(state->wallpaper);
        state->wallpaperReady = false;
        return false;
    }

    scale_image_to_wallpaper(state->wallpaper, imageWidth, imageHeight, imagePixels);
    stbi_image_free(imagePixels);

    std::strncpy(state->currentBackground, path, sizeof(state->currentBackground) - 1);
    state->currentBackground[sizeof(state->currentBackground) - 1] = '\0';
    state->wallpaperReady = true;
    return true;
}

bool load_first_available_background(DesktopState* state) {
    if (load_background(state, kDefaultBackground)) {
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
        if (load_background(state, path)) {
            return true;
        }
    }

    return false;
}

void draw_desktop(std::uint32_t* pixels, const DesktopState& state) {
    if (state.wallpaper && state.wallpaperReady) {
        std::memcpy(pixels, state.wallpaper, sizeof(std::uint32_t) * kSurfaceWidth * kSurfaceHeight);
    } else {
        draw_gradient(pixels);
    }

    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 0, 0, kSurfaceWidth, 56, kColorPanel);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 0, static_cast<int>(kSurfaceHeight) - 82, kSurfaceWidth, 82, kColorPanel);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 28, 88, 364, 212, kColorTileA);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 420, 88, 452, 132, kColorTileB);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 420, 244, 214, 260, kColorTileA);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 658, 244, 214, 260, kColorTileB);

    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 28, 332, 364, 172, kColorAccentSoft);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 54, 358, 312, 18, kColorPanel);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 54, 392, 224, 18, kColorPanel);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 54, 426, 280, 18, kColorPanel);

    const int indicatorWidth = 92 + static_cast<int>(state.activitySeed % 220U);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, 54, 460, indicatorWidth, 22, kColorAccent);

    for (int i = 0; i < 5; ++i) {
        const int offset = 26 + (i * 72);
        fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, offset, static_cast<int>(kSurfaceHeight) - 58, 48, 34, kColorTileA);
    }

    fill_rect(
        pixels,
        kSurfaceWidth,
        kSurfaceHeight,
        static_cast<int>(kSurfaceWidth) - 168,
        static_cast<int>(kSurfaceHeight) - 62,
        132,
        36,
        state.focused ? kColorAccent : kColorTileB
    );

    fill_rect(
        pixels,
        kSurfaceWidth,
        kSurfaceHeight,
        0,
        0,
        kSurfaceWidth,
        6,
        state.focused ? kColorAccent : kColorTileB
    );

    const int pulseX = 440 + static_cast<int>((state.activitySeed * 13U) % 168U);
    const int pulseY = 274 + static_cast<int>((state.activitySeed * 7U) % 180U);
    fill_rect(pixels, kSurfaceWidth, kSurfaceHeight, pulseX, pulseY, 26, 26, kColorAccent);
}

bool handle_shell_request(std::Handle queue, const std::IPCMessage& message, DesktopState* state, bool* redraw) {
    if (!state || !redraw || (message.flags & std::IPC_MESSAGE_REQUEST) == 0) {
        return false;
    }

    std::services::MessageHeader header = {};
    if (!std::services::decode_message(message, &header)) {
        return false;
    }

    if (header.version != std::services::desktop_shell::VERSION) {
        std::services::desktop_shell::SetBackgroundReply reply = {};
        reply.header.version = std::services::desktop_shell::VERSION;
        reply.header.opcode = header.opcode;
        reply.status = std::services::STATUS_BAD_VERSION;
        return std::queue_reply(queue, message.id, &reply, sizeof(reply)) != fail;
    }

    if (header.opcode == static_cast<std::uint16_t>(std::services::desktop_shell::Opcode::Hello)) {
        std::services::desktop_shell::HelloReply reply = {};
        reply.header.version = std::services::desktop_shell::VERSION;
        reply.header.opcode = static_cast<std::uint16_t>(std::services::desktop_shell::Opcode::Hello);
        reply.status = std::services::STATUS_OK;
        std::strncpy(reply.service_name, std::services::desktop_shell::NAME, sizeof(reply.service_name) - 1);
        reply.service_name[sizeof(reply.service_name) - 1] = '\0';
        return std::queue_reply(queue, message.id, &reply, sizeof(reply)) != fail;
    }

    if (header.opcode == static_cast<std::uint16_t>(std::services::desktop_shell::Opcode::SetBackground)) {
        std::services::desktop_shell::SetBackgroundRequest request = {};
        std::services::desktop_shell::SetBackgroundReply reply = {};
        reply.header.version = std::services::desktop_shell::VERSION;
        reply.header.opcode = static_cast<std::uint16_t>(std::services::desktop_shell::Opcode::SetBackground);
        reply.status = std::services::STATUS_BAD_PAYLOAD;

        if (std::services::decode_message(message, &request) &&
            request.path[0] != '\0' &&
            load_background(state, request.path)) {
            reply.status = std::services::STATUS_OK;
            state->activitySeed += 43U;
            *redraw = true;
        }

        return std::queue_reply(queue, message.id, &reply, sizeof(reply)) != fail;
    }

    std::services::desktop_shell::SetBackgroundReply reply = {};
    reply.header.version = std::services::desktop_shell::VERSION;
    reply.header.opcode = header.opcode;
    reply.status = std::services::STATUS_BAD_OPCODE;
    return std::queue_reply(queue, message.id, &reply, sizeof(reply)) != fail;
}

}

int main() {
    write_str("[desktop-shell] connecting to graphics.compositor\n");
    const std::Handle compositor = connect_service(std::services::graphics_compositor::NAME);
    if (compositor == fail) {
        write_str("[desktop-shell] FAIL service_connect graphics.compositor\n");
        return 1;
    }
    write_str("[desktop-shell] SUCCESS service_connect graphics.compositor\n");

    const std::Handle surface = std::surface_create(kSurfaceWidth, kSurfaceHeight, std::services::surfaces::FORMAT_BGRA8);
    if (surface == fail) {
        write_str("[desktop-shell] FAIL surface_create\n");
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS surface_create\n");

    auto* pixels = static_cast<std::uint32_t*>(std::shared_map(surface));
    if (pixels == reinterpret_cast<std::uint32_t*>(fail) || pixels == nullptr) {
        write_str("[desktop-shell] FAIL shared_map(surface)\n");
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS shared_map(surface)\n");

    const std::Handle window = std::compositor_create_window(compositor, kSurfaceWidth, kSurfaceHeight, 0);
    if (window == fail) {
        write_str("[desktop-shell] FAIL compositor_create_window\n");
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS compositor_create_window\n");

    if (std::window_set_title(window, "Desktop Shell") == fail) {
        write_str("[desktop-shell] FAIL window_set_title\n");
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS window_set_title\n");

    if (std::window_attach_surface(window, surface) == fail) {
        write_str("[desktop-shell] FAIL window_attach_surface\n");
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS window_attach_surface\n");

    const std::Handle events = std::window_event_queue(window);
    if (events == fail) {
        write_str("[desktop-shell] FAIL window_event_queue\n");
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS window_event_queue\n");

    const std::Handle serviceQueue = std::queue_create();
    if (serviceQueue == fail || std::service_register(std::services::desktop_shell::NAME, serviceQueue) == fail) {
        write_str("[desktop-shell] FAIL desktop.shell service_register\n");
        if (serviceQueue != fail) {
            std::close(serviceQueue);
        }
        std::close(events);
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS desktop.shell service_register\n");

    DesktopState state = {};
    state.activitySeed = 1;
    load_first_available_background(&state);

    draw_desktop(pixels, state);
    if (std::surface_commit(surface, 0, 0, kSurfaceWidth, kSurfaceHeight) == fail) {
        write_str("[desktop-shell] FAIL initial surface_commit\n");
        std::close(serviceQueue);
        std::close(events);
        std::close(window);
        std::close(surface);
        std::close(compositor);
        return 1;
    }
    write_str("[desktop-shell] SUCCESS initial surface_commit\n");
    write_str("[desktop-shell] ready\n");

    for (;;) {
        bool redraw = false;

        for (;;) {
            std::Event event = {};
            if (std::event_poll(events, &event) == fail) {
                break;
            }

            if (event.type == std::EventType::Window) {
                if (event.window.action == std::WindowEventAction::FocusGained) {
                    state.focused = true;
                    redraw = true;
                } else if (event.window.action == std::WindowEventAction::FocusLost) {
                    state.focused = false;
                    redraw = true;
                } else if (event.window.action == std::WindowEventAction::CloseRequested) {
                    delete[] state.wallpaper;
                    std::close(serviceQueue);
                    std::close(events);
                    std::close(window);
                    std::close(surface);
                    std::close(compositor);
                    return 0;
                }
            } else if (event.type == std::EventType::Key && event.key.action == std::KeyEventAction::Press) {
                const bool superPressed = (event.key.modifiers & std::KeyModifierSuper) != 0;
                const char key = event.key.text[0] != '\0' ? event.key.text[0] : static_cast<char>(event.key.keycode);
                if (superPressed && (key == 'f' || key == 'F')) {
                    if (!launch_file_browser()) {
                        write_str("[desktop-shell] FAIL spawn file-browser.exe\n");
                    }
                    state.activitySeed += 31U;
                    redraw = true;
                } else if (superPressed && (key == 'b' || key == 'B')) {
                    if (!launch_background_switcher()) {
                        write_str("[desktop-shell] FAIL spawn background-switcher.exe\n");
                    }
                    state.activitySeed += 37U;
                    redraw = true;
                } else {
                    state.activitySeed += 9U;
                    redraw = true;
                }
            }
        }

        for (;;) {
            std::IPCMessage message = {};
            if (std::queue_receive(serviceQueue, &message, false) == fail) {
                break;
            }

            if (!handle_shell_request(serviceQueue, message, &state, &redraw)) {
                write_str("[desktop-shell] FAIL service request handling\n");
            }
        }

        state.activitySeed += state.focused ? 5U : 2U;
        redraw = true;

        if (redraw) {
            draw_desktop(pixels, state);
            if (std::surface_commit(surface, 0, 0, kSurfaceWidth, kSurfaceHeight) == fail) {
                write_str("[desktop-shell] FAIL surface_commit update\n");
                break;
            }
        }

        std::sleep(kAnimationTickMs);
    }

    delete[] state.wallpaper;
    std::close(serviceQueue);
    std::close(events);
    std::close(window);
    std::close(surface);
    std::close(compositor);
    return 0;
}
