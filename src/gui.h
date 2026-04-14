#pragma once

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

class VM;

void register_gui_builtins(VM& vm);
void gui_init(SDL_Window* window, SDL_Renderer* renderer, float scale = 1.0f);
void gui_shutdown();
void gui_new_frame();
void gui_render(SDL_Renderer* renderer);
bool gui_process_event(SDL_Event* ev);
