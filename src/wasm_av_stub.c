/* WASM stubs for SDL3_image and SDL3_mixer, which have no Emscripten port yet.
 * No-op bodies so the GFX / audio builtins link and degrade gracefully in the
 * browser: image loads return NULL (no texture), audio calls stay silent.
 * Compiled only by build_wasm.sh; replaced by real emcc-built libs later. */
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL_mixer.h>

SDL_Surface *IMG_Load(const char *file) { (void)file; return NULL; }
SDL_Texture *IMG_LoadTexture(SDL_Renderer *r, const char *file) { (void)r; (void)file; return NULL; }
bool IMG_SavePNG(SDL_Surface *s, const char *file) { (void)s; (void)file; return false; }

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
