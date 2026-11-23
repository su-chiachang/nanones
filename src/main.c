/* nes-emu: SDL3 front end.
 *
 *   nes-emu game.nes                      play (Esc quits)
 *   nes-emu game.nes --headless N out.ppm [keys]
 *                                         run N frames without a window, dump the
 *                                         last frame; keys = "frame:hexmask,..."
 *
 * Keys: arrows = D-pad, Z = B, X = A, Enter = Start, Right Shift = Select.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCALE 3

/* keys spec: "frame:hexmask,frame:hexmask,..." — the mask stays held from that
 * frame on (BTN_A=01 B=02 Select=04 Start=08 Up=10 Down=20 Left=40 Right=80). */
static uint8_t scripted_buttons(const char *spec, int frame) {
    uint8_t held = 0;
    while (spec && *spec) {
        char *end;
        long f = strtol(spec, &end, 10);
        if (*end != ':') break;
        long mask = strtol(end + 1, &end, 16);
        if (f <= frame) held = (uint8_t)mask;
        spec = (*end == ',') ? end + 1 : NULL;
    }
    return held;
}

static int run_headless(Nes *nes, int frames, const char *out, const char *keys) {
    for (int i = 0; i < frames; i++) {
        nes->buttons = scripted_buttons(keys, i);
        if (!nes_run_frame(nes)) {
            fprintf(stderr, "CPU jammed at $%04X\n", nes->cpu.pc);
            return 1;
        }
    }
    FILE *f = fopen(out, "wb");
    if (!f) { perror(out); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", PPU_W, PPU_H);
    for (int i = 0; i < PPU_W * PPU_H; i++) {
        uint32_t px = nes->ppu.fb[i];
        fputc((px >> 16) & 0xFF, f);
        fputc((px >> 8) & 0xFF, f);
        fputc(px & 0xFF, f);
    }
    fclose(f);
    return 0;
}

static uint8_t read_buttons(void) {
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X]) b |= BTN_A;
    if (k[SDL_SCANCODE_Z]) b |= BTN_B;
    if (k[SDL_SCANCODE_RSHIFT]) b |= BTN_SELECT;
    if (k[SDL_SCANCODE_RETURN]) b |= BTN_START;
    if (k[SDL_SCANCODE_UP]) b |= BTN_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= BTN_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= BTN_RIGHT;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s game.nes [--headless frames out.ppm]\n", argv[0]);
        return 2;
    }

    static Nes nes;
    if (nes_load(&nes, argv[1]) != 0) return 1;

    if (argc >= 5 && strcmp(argv[2], "--headless") == 0) {
        int rc = run_headless(&nes, atoi(argv[3]), argv[4], argc > 5 ? argv[5] : NULL);
        nes_free(&nes);
        return rc;
    }

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("nes-emu", PPU_W * SCALE, PPU_H * SCALE, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, PPU_W, PPU_H) : NULL;
    if (!tex) {
        fprintf(stderr, "SDL error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderVSync(ren, 1);

    SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, 48000};
    SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else fprintf(stderr, "audio disabled: %s\n", SDL_GetError());

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
        }
        nes.buttons = read_buttons();
        if (!nes_run_frame(&nes)) {
            fprintf(stderr, "CPU jammed at $%04X\n", nes.cpu.pc);
            break;
        }
        if (audio) {
            static float samples[APU_RING];
            size_t n = apu_read_samples(&nes.apu, samples, APU_RING);
            /* keep the queue short so sound does not lag behind the picture */
            if (SDL_GetAudioStreamQueued(audio) < 48000 / 10 * (int)sizeof(float))
                SDL_PutAudioStreamData(audio, samples, (int)(n * sizeof(float)));
        }
        SDL_UpdateTexture(tex, NULL, nes.ppu.fb, PPU_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    nes_free(&nes);
    return 0;
}
