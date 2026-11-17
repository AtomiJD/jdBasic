/* WASM stubs for SDL3_mixer, which has no Emscripten port yet, and for
 * SDL3_image when build_wasm.sh finds no libs/SDL3_image to compile.
 * No-op bodies so the GFX / audio builtins link: image loads fail with an
 * error that names the missing library, audio calls stay silent.
 * Compiled only by build_wasm.sh, which defines JDB_WASM_SDL3_IMAGE when it
 * links the real SDL3_image. */
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL_mixer.h>

#ifndef JDB_WASM_SDL3_IMAGE
#define NO_IMAGE_MSG "SDL3_image is not built into this WASM runtime"
SDL_Surface *IMG_Load(const char *file) { (void)file; SDL_SetError(NO_IMAGE_MSG); return NULL; }
SDL_Texture *IMG_LoadTexture(SDL_Renderer *r, const char *file) { (void)r; (void)file; SDL_SetError(NO_IMAGE_MSG); return NULL; }
bool IMG_SavePNG(SDL_Surface *s, const char *file) { (void)s; (void)file; return SDL_SetError(NO_IMAGE_MSG); }
bool IMG_SaveJPG(SDL_Surface *s, const char *file, int quality) { (void)s; (void)file; (void)quality; return SDL_SetError(NO_IMAGE_MSG); }
#endif

bool MIX_Init(void) { return false; }
void MIX_Quit(void) {}
MIX_Mixer *MIX_CreateMixerDevice(SDL_AudioDeviceID d, const SDL_AudioSpec *s) { (void)d; (void)s; return NULL; }
void MIX_DestroyMixer(MIX_Mixer *m) { (void)m; }
MIX_Audio *MIX_LoadAudio(MIX_Mixer *m, const char *p, bool pd) { (void)m; (void)p; (void)pd; return NULL; }
void MIX_DestroyAudio(MIX_Audio *a) { (void)a; }
MIX_Track *MIX_CreateTrack(MIX_Mixer *m) { (void)m; return NULL; }
void MIX_DestroyTrack(MIX_Track *t) { (void)t; }
bool MIX_SetTrackAudio(MIX_Track *t, MIX_Audio *a) { (void)t; (void)a; return false; }
MIX_Audio *MIX_GetTrackAudio(MIX_Track *t) { (void)t; return NULL; }
bool MIX_PlayTrack(MIX_Track *t, SDL_PropertiesID o) { (void)t; (void)o; return false; }
bool MIX_StopTrack(MIX_Track *t, Sint64 f) { (void)t; (void)f; return false; }
bool MIX_PauseTrack(MIX_Track *t) { (void)t; return false; }
bool MIX_ResumeTrack(MIX_Track *t) { (void)t; return false; }
bool MIX_PauseAllTracks(MIX_Mixer *m) { (void)m; return false; }
bool MIX_ResumeAllTracks(MIX_Mixer *m) { (void)m; return false; }
bool MIX_SetTrackGain(MIX_Track *t, float g) { (void)t; (void)g; return false; }
bool MIX_SetMixerGain(MIX_Mixer *m, float g) { (void)m; (void)g; return false; }
bool MIX_SetTrackLoops(MIX_Track *t, int n) { (void)t; (void)n; return false; }
