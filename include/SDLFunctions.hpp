#pragma once
#ifdef SDL3
#include "NeReLaBasic.hpp"

BasicValue builtin_screen(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_screenflip(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_screenwidth(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_screenheight(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_toggle_fullscreen(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_drawcolor(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_pset(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_line(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_rect(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_circle(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_ellipse(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_rounded_rect(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_circle_sector(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_setfont(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_text(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_plotraw(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_forward(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_backward(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_left(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_right(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_penup(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_pendown(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_setpos(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_setheading(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_home(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_draw(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_clear(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_turtle_set_color(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sound_init(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sound_voice(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sound_play(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sound_release(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sound_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sfx_load(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sfx_play(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_music_play(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_music_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_mousex(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_mousey(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_mouseb(NeReLaBasic& vm, const std::vector<BasicValue>& args);
#if !defined(__EMSCRIPTEN__) // SDL3 is still experimental no LOADTEXTURE support
BasicValue builtin_sprite_load(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_load_aseprite(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_create(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_set_animation(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_set_flip(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_move(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_set_velocity(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_delete(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_update(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_draw_all(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_get_x(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_get_y(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_collision(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_add_to_group(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_collision_group(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_create_group(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sprite_collision_groups(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_map_load(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_map_draw_layer(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_map_get_objects(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_map_collides(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_map_get_tile_id(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_map_draw_debug(NeReLaBasic& vm, const std::vector<BasicValue>& args);
#endif

void register_sdl_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate);

#endif