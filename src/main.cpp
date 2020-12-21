#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <thread>

#include <common/cbuf.hpp>
#include <common/types.hpp>

#include <core/emulator.hpp>
#include <core/spu.hpp>
#include <core/joypad/joypad.hpp>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <spdlog/spdlog.h>

static const std::map<std::size_t, Core::Key> KeyMap = {
    {0, Core::Key::Triangle},
    {1, Core::Key::Circle},
    {2, Core::Key::Cross},
    {3, Core::Key::Square},
    {4, Core::Key::L2},
    {5, Core::Key::R2},
    {6, Core::Key::L1},
    {7, Core::Key::R1},
    {8, Core::Key::Select},
    {9, Core::Key::Start}
};

static SDL_AudioDeviceID audio_device;
static Cbuf<s16, 8192> *audio_fifo;

static std::atomic<bool> running;

void AudioCallback(void *userdata, u8 *stream, int length)
{
    (void)userdata;

    s16 *buffer = reinterpret_cast<s16 *>(stream);

    length /= 2;
    length = std::min(static_cast<int>(audio_fifo->Size()), length);

    const std::size_t read = audio_fifo->Dequeue(buffer, length);

    if (read == 0) {
        std::memset(stream, 0, length);
    } else if (static_cast<int>(read) < length) {
        std::memset(&buffer[read], buffer[read - 1], length - read);
    }
}

void RunCoreThread(std::shared_ptr<Core::Emulator> e)
{
    int frames = 0;

    float mean = 0.0f;
    float m2 = 0.0f;

    auto last = std::chrono::high_resolution_clock::now();

    while (running) {
        e->RunFrame();

        auto current = std::chrono::high_resolution_clock::now();
        const float delta = std::chrono::duration_cast<std::chrono::microseconds>(current - last).count();
        last = current;

        frames++;

        const float dt = delta - mean;
        mean += dt / frames;

        const float dt2 = delta - mean;
        m2 += dt * dt2;

        const float std = std::sqrt(m2 / (frames - 1)) / 1000.0f;

        spdlog::trace("frame drawn in {:.03f} ms", delta / 1000.0f);
        spdlog::trace("mean {:.03f} ms, std. {:.03f} ms", mean / 1000.0f, std);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    spdlog::set_pattern("[%T:%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::debug);

    std::ifstream config_file("config.json");

    if (!config_file.is_open()) {
        spdlog::error("unable to open config.json");
        return 1;
    }

    json config;
    config_file >> config;

    if (!config.contains("bios")) {
        spdlog::error("bios key does not exist in config.json\n");
        return 1;
    }

    if (!config.contains("disc")) {
        spdlog::error("disc key does not exist in config.json\n");
        return 1;
    }

    const auto bios = config["bios"];
    const auto disc = config["disc"];

    bool enable_audio = false;

    if (config.contains("enable_audio")) {
        enable_audio = config["enable_audio"].get<bool>();
    }

    if (config.contains("log_level")) {
        const auto log_level = config["log_level"];

        if (log_level == "trace") {
            spdlog::set_level(spdlog::level::trace);
        } else if (log_level == "debug") {
            spdlog::set_level(spdlog::level::debug);
        } else if (log_level == "info") {
            spdlog::set_level(spdlog::level::info);
        } else if (log_level == "warn") {
            spdlog::set_level(spdlog::level::warn);
        } else if (log_level == "err") {
            spdlog::set_level(spdlog::level::err);
        } else if (log_level == "critical") {
            spdlog::set_level(spdlog::level::critical);
        } else if (log_level == "off") {
            spdlog::set_level(spdlog::level::off);
        } else {
            spdlog::warn("unknown log level option \"{}\"", log_level);
        }
    }

    auto e = std::make_shared<Core::Emulator>(bios, disc, enable_audio);
    e->Reset();

    audio_fifo = e->m_spu->SoundFifo();

    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
        spdlog::error("unable to init SDL2: {}", SDL_GetError());
        return 1;
    }

    SDL_Joystick *joystick = nullptr;

    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);
        spdlog::debug("using joystick 0: {}", SDL_JoystickNameForIndex(0));
    } else {
        spdlog::warn("no joysticks connected");
    }

    SDL_Window *window = SDL_CreateWindow(
        "btpsx",
         SDL_WINDOWPOS_UNDEFINED,
         SDL_WINDOWPOS_UNDEFINED,
         1024,
         512,
         SDL_WINDOW_SHOWN
    );

    if (window == nullptr) {
        spdlog::error("unable to create window: {}", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (renderer == nullptr) {
        spdlog::error("unable to create renderer: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR1555,
        SDL_TEXTUREACCESS_STREAMING,
        1024,
        512
    );

    if (texture == nullptr) {
        spdlog::error("unable to create texture: {}", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return 1;
    }

    if (enable_audio) {
        SDL_AudioSpec want, have;
        SDL_zero(want);

        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 2048;
        want.callback = AudioCallback;

        audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);

        if (audio_device <= 0) {
            spdlog::error("unable to open audio device: {}", SDL_GetError());
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            return 1;
        }

        SDL_PauseAudioDevice(audio_device, 0);
    }

    running = true;
    std::thread core_thread(RunCoreThread, e);

    SDL_Event event;

    while (running) { 
        if (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_JOYBUTTONDOWN:
                if (KeyMap.count(event.jbutton.button) != 0) {
                    e->m_joypad->SetKeystate(KeyMap.at(event.jbutton.button), true);
                }

                break;
            case SDL_JOYBUTTONUP:
                if (KeyMap.count(event.jbutton.button) != 0) {
                    e->m_joypad->SetKeystate(KeyMap.at(event.jbutton.button), false);
                }

                break;
            case SDL_JOYHATMOTION:
                switch (event.jhat.value) {
                case SDL_HAT_CENTERED:
                    e->m_joypad->SetKeystate(Core::Key::Up, false);
                    e->m_joypad->SetKeystate(Core::Key::Down, false);
                    e->m_joypad->SetKeystate(Core::Key::Left, false);
                    e->m_joypad->SetKeystate(Core::Key::Right, false);
                    break;
                case SDL_HAT_UP:
                    e->m_joypad->SetKeystate(Core::Key::Up, true);
                    e->m_joypad->SetKeystate(Core::Key::Down, false);
                    e->m_joypad->SetKeystate(Core::Key::Left, false);
                    e->m_joypad->SetKeystate(Core::Key::Right, false);
                    break;
                case SDL_HAT_DOWN:
                    e->m_joypad->SetKeystate(Core::Key::Up, false);
                    e->m_joypad->SetKeystate(Core::Key::Down, true);
                    e->m_joypad->SetKeystate(Core::Key::Left, false);
                    e->m_joypad->SetKeystate(Core::Key::Right, false);
                    break;
                case SDL_HAT_LEFT:
                    e->m_joypad->SetKeystate(Core::Key::Up, false);
                    e->m_joypad->SetKeystate(Core::Key::Down, false);
                    e->m_joypad->SetKeystate(Core::Key::Left, true);
                    e->m_joypad->SetKeystate(Core::Key::Right, false);
                    break;
                case SDL_HAT_RIGHT:
                    e->m_joypad->SetKeystate(Core::Key::Up, false);
                    e->m_joypad->SetKeystate(Core::Key::Down, false);
                    e->m_joypad->SetKeystate(Core::Key::Left, false);
                    e->m_joypad->SetKeystate(Core::Key::Right, true);
                    break;
                }

                spdlog::debug("dpad {}", event.jhat.hat);
                break;
            case SDL_DROPFILE:
                spdlog::debug("loading {}...", event.drop.file);
                e->LoadExe(event.drop.file);
                SDL_free(event.drop.file);
                break;
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        void *pixels;
        int pitch;

        SDL_LockTexture(texture, nullptr, &pixels, &pitch);

        e->m_swapchain.WithConsumer([=](u8 *buffer) {
            u8 *p = reinterpret_cast<u8 *>(pixels);

            for (std::size_t y = 0; y < 512; ++y) {
                std::memcpy(p, buffer, 2 * 1024);
                buffer += 2 * 1024;
                p += pitch;
            }
        });

        SDL_UnlockTexture(texture);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    core_thread.join();

    if (SDL_JoystickGetAttached(joystick)) {
        SDL_JoystickClose(joystick);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    if (enable_audio) {
        SDL_CloseAudioDevice(audio_device);
    }

    SDL_Quit();
    return 0;
}
