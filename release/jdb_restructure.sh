#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════
#  jdb_restructure.sh — generated from release/jdb_inventory.md
#
#  HOW TO USE
#  ----------
#  1. Review every `# git mv ...` line. Un-comment (delete leading `# `)
#     the moves you want to execute.
#  2. Run from repo root:   bash release/jdb_restructure.sh
#  3. The "auto-sweep" block at the end gathers everything still sitting
#     at jdb/ top level into jdb/_scratch/. It is OFF by default — set
#     ENABLE_SCRATCH_SWEEP=1 below to enable, or run manually later.
#  4. After the script, run the pre-commit gate to verify nothing
#     broke. IMPORT path resolution is the main risk — jdBasic searches
#     the script's own directory first, so a module imported by a file
#     that you moved into a different subdir will silently fail to load.
#
#  CAVEATS
#  -------
#  * `git mv` preserves history (file shows as renamed in `git log
#     --follow`); plain `mv` would not.
#  * Moves are local until you commit + push.
#  * Filenames are case-sensitive in git's index even on Windows.
# ════════════════════════════════════════════════════════════════════

set -e

# Run from repo root regardless of caller's cwd.
cd "$(dirname "$0")/.."

# Toggle to 1 to enable the final auto-sweep of whatever survived.
ENABLE_SCRATCH_SWEEP=1

# ──────────────────────────────────────────────────────────────────
#  Create all target directories up-front so individual `git mv`
#  lines below don't need to mkdir each time.
# ──────────────────────────────────────────────────────────────────
mkdir -p jdb/modules
mkdir -p jdb/demos/games
mkdir -p jdb/demos/graphics
mkdir -p jdb/demos/gl
mkdir -p jdb/demos/ai
mkdir -p jdb/demos/gui
mkdir -p jdb/demos/sound
mkdir -p jdb/demos/apl
mkdir -p jdb/demos/tui
mkdir -p jdb/demos/web
mkdir -p jdb/demos/bridges
mkdir -p jdb/demos/async
mkdir -p jdb/demos/turtle
mkdir -p jdb/demos/sprites
mkdir -p jdb/demos/workflow
mkdir -p jdb/demos/tensor
mkdir -p jdb/tutorials
mkdir -p jdb/emu
mkdir -p jdb/_scratch

# ════════════════════════════════════════════════════════════════════
# === MODULES (reusable libraries — IMPORT targets, never delete) ===
# ════════════════════════════════════════════════════════════════════
git mv jdb/MATH.jdb           jdb/modules/MATH.jdb
git mv jdb/MLAB.jdb           jdb/modules/MLAB.jdb
git mv jdb/PLOTTER.jdb        jdb/modules/PLOTTER.jdb
git mv jdb/SQ.jdb             jdb/modules/SQ.jdb
git mv jdb/claude_live.jdb    jdb/modules/claude_live.jdb
git mv jdb/cpu6502.jdb        jdb/modules/cpu6502.jdb
git mv jdb/apple2.jdb         jdb/modules/apple2.jdb
git mv jdb/sqlite.jdb         jdb/modules/sqlite.jdb
git mv jdb/plot_lib.jdb       jdb/modules/plot_lib.jdb
git mv jdb/text_viz.jdb       jdb/modules/text_viz.jdb
git mv jdb/sprite_core.jdb    jdb/modules/sprite_core.jdb
git mv jdb/modglob.jdb        jdb/modules/modglob.jdb
git mv jdb/modwrap.jdb        jdb/modules/modwrap.jdb
git mv jdb/sys_paths.jdb      jdb/modules/sys_paths.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / GAMES =================================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/space_shooter.jdb  jdb/demos/games/space_shooter.jdb
git mv jdb/snake_game.jdb     jdb/demos/games/snake_game.jdb
git mv jdb/tetris_game.jdb    jdb/demos/games/tetris_game.jdb
git mv jdb/chess_engine.jdb   jdb/demos/games/chess_engine.jdb
git mv jdb/minesweeper.jdb    jdb/demos/games/minesweeper.jdb
git mv jdb/brainf__k.jdb      jdb/demos/games/brainf__k.jdb
git mv jdb/car_race.jdb       jdb/demos/games/car_race.jdb
git mv jdb/vibe_game.jdb      jdb/demos/games/vibe_game.jdb
git mv jdb/parallax_demo.jdb  jdb/demos/games/parallax_demo.jdb
git mv jdb/rotate_demo.jdb    jdb/demos/games/rotate_demo.jdb
git mv jdb/mine_joy.jdb       jdb/demos/games/mine_joy.jdb
git mv jdb/raytracer.jdb      jdb/demos/games/raytracer.jdb
git mv jdb/wf_cube.jdb        jdb/demos/games/wf_cube.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / GRAPHICS (fractals, plots, math viz) ==================
# ════════════════════════════════════════════════════════════════════
git mv jdb/mandel_core.jdb     jdb/demos/graphics/mandel_core.jdb
git mv jdb/mandel_vec.jdb      jdb/demos/graphics/mandel_vec.jdb
git mv jdb/mandel_zoom.jdb     jdb/demos/graphics/mandel_zoom.jdb
git mv jdb/mandel_live.jdb     jdb/demos/graphics/mandel_live.jdb
git mv jdb/mandel_eigen.jdb    jdb/demos/graphics/mandel_eigen.jdb
git mv jdb/barnsley_fern.jdb   jdb/demos/graphics/barnsley_fern.jdb
git mv jdb/nbody_galaxy.jdb    jdb/demos/graphics/nbody_galaxy.jdb
git mv jdb/orbit_core.jdb      jdb/demos/graphics/orbit_core.jdb
git mv jdb/orbit_sim.jdb       jdb/demos/graphics/orbit_sim.jdb
git mv jdb/orbit_vec.jdb       jdb/demos/graphics/orbit_vec.jdb
git mv jdb/orbit_visual.jdb    jdb/demos/graphics/orbit_visual.jdb
git mv jdb/universe.jdb        jdb/demos/graphics/universe.jdb
git mv jdb/universe_naive.jdb  jdb/demos/graphics/universe_naive.jdb
git mv jdb/boids_apl.jdb       jdb/demos/graphics/boids_apl.jdb
git mv jdb/circles.jdb         jdb/demos/graphics/circles.jdb
git mv jdb/sin_vec.jdb         jdb/demos/graphics/sin_vec.jdb
git mv jdb/sin_visual.jdb      jdb/demos/graphics/sin_visual.jdb
git mv jdb/sine_wave_3d.jdb               jdb/demos/graphics/sine_wave_3d.jdb
git mv jdb/sine_wave_3d_wire.jdb          jdb/demos/graphics/sine_wave_3d_wire.jdb
git mv jdb/sine_wave_3d_wire_rot.jdb      jdb/demos/graphics/sine_wave_3d_wire_rot.jdb
git mv jdb/sine_wave_3d_wire_rot_debug.jdb jdb/demos/graphics/sine_wave_3d_wire_rot_debug.jdb
git mv jdb/pi.jdb              jdb/demos/graphics/pi.jdb
git mv jdb/matrix_rain.jdb     jdb/demos/graphics/matrix_rain.jdb
git mv jdb/fft_viz.jdb         jdb/demos/graphics/fft_viz.jdb
git mv jdb/num_eigen.jdb       jdb/demos/graphics/num_eigen.jdb
git mv jdb/graph_v2.jdb        jdb/demos/graphics/graph_v2.jdb
git mv jdb/vec_plot.jdb        jdb/demos/graphics/vec_plot.jdb
git mv jdb/plot_viz.jdb        jdb/demos/graphics/plot_viz.jdb
git mv jdb/sim_meta.jdb        jdb/demos/graphics/sim_meta.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / OPENGL ================================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/gl_p1_demo.jdb        jdb/demos/gl/gl_p1_demo.jdb
git mv jdb/gl_p2_triangle.jdb    jdb/demos/gl/gl_p2_triangle.jdb
git mv jdb/gl_p3_cube.jdb        jdb/demos/gl/gl_p3_cube.jdb
git mv jdb/gl_p4_texcube.jdb     jdb/demos/gl/gl_p4_texcube.jdb
git mv jdb/gl_keyboard_smoke.jdb jdb/demos/gl/gl_keyboard_smoke.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / AI / LLM / CLASSIFIER =================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/ai_demo.jdb            jdb/demos/ai/ai_demo.jdb
git mv jdb/ai_chat_demo.jdb       jdb/demos/ai/ai_chat_demo.jdb
git mv jdb/classifier_demo.jdb    jdb/demos/ai/classifier_demo.jdb
git mv jdb/train_classifier.jdb   jdb/demos/ai/train_classifier.jdb
git mv jdb/mini_llm.jdb           jdb/demos/ai/mini_llm.jdb
git mv jdb/mini_onnx.jdb          jdb/demos/ai/mini_onnx.jdb
git mv jdb/mini_rag.jdb           jdb/demos/ai/mini_rag.jdb
git mv jdb/rag_demo.jdb           jdb/demos/ai/rag_demo.jdb
git mv jdb/gpt_client.jdb         jdb/demos/ai/gpt_client.jdb
git mv jdb/gpt_mini.jdb           jdb/demos/ai/gpt_mini.jdb
git mv jdb/agent_task.jdb         jdb/demos/ai/agent_task.jdb
git mv jdb/worker_node.jdb        jdb/demos/ai/worker_node.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / GUI (ImGui) ===========================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/gui_v1.jdb        jdb/demos/gui/gui_v1.jdb
git mv jdb/gui_v2.jdb        jdb/demos/gui/gui_v2.jdb
git mv jdb/gui_full.jdb      jdb/demos/gui/gui_full.jdb
git mv jdb/gui_test.jdb      jdb/demos/gui/gui_test.jdb
git mv jdb/gui_theme.jdb     jdb/demos/gui/gui_theme.jdb
git mv jdb/gui_input.jdb     jdb/demos/gui/gui_input.jdb
git mv jdb/gui_mouse.jdb     jdb/demos/gui/gui_mouse.jdb
git mv jdb/gui_sql.jdb       jdb/demos/gui/gui_sql.jdb
git mv jdb/app_master.jdb    jdb/demos/gui/app_master.jdb
git mv jdb/spreadsheet.jdb   jdb/demos/gui/spreadsheet.jdb
git mv jdb/piano_ui.jdb      jdb/demos/gui/piano_ui.jdb
git mv jdb/mini_graphics.jdb jdb/demos/gui/mini_graphics.jdb
git mv jdb/script_edit.jdb   jdb/demos/gui/script_edit.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / SOUND / MUSIC =========================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/sq_core.jdb     jdb/demos/sound/sq_core.jdb
git mv jdb/sq_fluent.jdb   jdb/demos/sound/sq_fluent.jdb
git mv jdb/sq_part2.jdb    jdb/demos/sound/sq_part2.jdb
git mv jdb/sq_part22.jdb   jdb/demos/sound/sq_part22.jdb
git mv jdb/sq_part23.jdb   jdb/demos/sound/sq_part23.jdb
git mv jdb/sq_part3.jdb    jdb/demos/sound/sq_part3.jdb
git mv jdb/sq_part4.jdb    jdb/demos/sound/sq_part4.jdb
git mv jdb/sq_part5.jdb    jdb/demos/sound/sq_part5.jdb
git mv jdb/sq_part6.jdb    jdb/demos/sound/sq_part6.jdb
git mv jdb/sq_part7.jdb    jdb/demos/sound/sq_part7.jdb
git mv jdb/sq_part8.jdb    jdb/demos/sound/sq_part8.jdb
git mv jdb/sq_reactive.jdb jdb/demos/sound/sq_reactive.jdb
git mv jdb/synth_apl.jdb   jdb/demos/sound/synth_apl.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / APL VECTORIZED ========================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/life_demo.jdb     jdb/demos/apl/life_demo.jdb
git mv jdb/gol_doc.jdb       jdb/demos/apl/gol_doc.jdb
git mv jdb/gol_graph.jdb     jdb/demos/apl/gol_graph.jdb
git mv jdb/gol_logic.jdb     jdb/demos/apl/gol_logic.jdb
git mv jdb/prime_sieve.jdb   jdb/demos/apl/prime_sieve.jdb
git mv jdb/prime_gen.jdb     jdb/demos/apl/prime_gen.jdb
git mv jdb/iota_vec.jdb      jdb/demos/apl/iota_vec.jdb
git mv jdb/outer_prod.jdb    jdb/demos/apl/outer_prod.jdb
git mv jdb/matrix_vec.jdb    jdb/demos/apl/matrix_vec.jdb
git mv jdb/matrix_inv.jdb    jdb/demos/apl/matrix_inv.jdb
git mv jdb/reduce_ops.jdb    jdb/demos/apl/reduce_ops.jdb
git mv jdb/fib_reduce.jdb    jdb/demos/apl/fib_reduce.jdb
git mv jdb/pipe_v1.jdb       jdb/demos/apl/pipe_v1.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / TUI (terminal UI) =====================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/tui_demo.jdb       jdb/demos/tui/tui_demo.jdb
git mv jdb/sys_monitor.jdb    jdb/demos/tui/sys_monitor.jdb
git mv jdb/mouse_basics.jdb   jdb/demos/tui/mouse_basics.jdb
git mv jdb/input_joy.jdb      jdb/demos/tui/input_joy.jdb
git mv jdb/clip_tool.jdb      jdb/demos/tui/clip_tool.jdb
git mv jdb/md_browser.jdb     jdb/demos/tui/md_browser.jdb
git mv jdb/md_explorer.jdb    jdb/demos/tui/md_explorer.jdb
git mv jdb/md_render_v1.jdb   jdb/demos/tui/md_render_v1.jdb
git mv jdb/md_render_v2.jdb   jdb/demos/tui/md_render_v2.jdb
git mv jdb/cowsay.jdb         jdb/demos/tui/cowsay.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / WEB / NETWORK =========================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/http_get.jdb       jdb/demos/web/http_get.jdb
git mv jdb/web_server.jdb     jdb/demos/web/web_server.jdb
git mv jdb/weather.jdb        jdb/demos/web/weather.jdb
git mv jdb/stock_plot.jdb     jdb/demos/web/stock_plot.jdb
git mv jdb/fake_ticker.jdb    jdb/demos/web/fake_ticker.jdb
git mv jdb/wflib.jdb          jdb/demos/web/wflib.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / BRIDGES (FFI / COM / DLL / SQL) =======================
# ════════════════════════════════════════════════════════════════════
git mv jdb/dll_demo.jdb        jdb/demos/bridges/dll_demo.jdb
git mv jdb/dll_magic.jdb       jdb/demos/bridges/dll_magic.jdb
git mv jdb/sqlite_demo.jdb     jdb/demos/bridges/sqlite_demo.jdb
git mv jdb/sqlite_core.jdb     jdb/demos/bridges/sqlite_core.jdb
git mv jdb/sql_insert.jdb      jdb/demos/bridges/sql_insert.jdb
git mv jdb/sql_logging.jdb     jdb/demos/bridges/sql_logging.jdb
git mv jdb/serial_com.jdb      jdb/demos/bridges/serial_com.jdb
git mv jdb/excel_com.jdb       jdb/demos/bridges/excel_com.jdb
git mv jdb/word_auto.jdb       jdb/demos/bridges/word_auto.jdb
git mv jdb/access_reader.jdb   jdb/demos/bridges/access_reader.jdb
git mv jdb/access_sql.jdb      jdb/demos/bridges/access_sql.jdb
git mv jdb/access_v1.jdb       jdb/demos/bridges/access_v1.jdb
git mv jdb/acc_module.jdb      jdb/demos/bridges/acc_module.jdb
git mv jdb/outlook_summary.jdb jdb/demos/bridges/outlook_summary.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / ASYNC + THREADING =====================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/async_await.jdb jdb/demos/async/async_await.jdb
git mv jdb/thread_v1.jdb   jdb/demos/async/thread_v1.jdb
git mv jdb/simple_sim.jdb  jdb/demos/async/simple_sim.jdb
git mv jdb/task_queue.jdb  jdb/demos/async/task_queue.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / TURTLE GRAPHICS =======================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/turtle_start.jdb  jdb/demos/turtle/turtle_start.jdb
git mv jdb/turti.jdb         jdb/demos/turtle/turti.jdb
git mv jdb/turtle_fib.jdb    jdb/demos/turtle/turtle_fib.jdb
git mv jdb/turtle_dragon.jdb jdb/demos/turtle/turtle_dragon.jdb
git mv jdb/turtle_koch.jdb   jdb/demos/turtle/turtle_koch.jdb
git mv jdb/turtle_tree.jdb   jdb/demos/turtle/turtle_tree.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / SPRITES + TILEMAP =====================================
# ════════════════════════════════════════════════════════════════════
git mv jdb/sprite_create_demo.jdb jdb/demos/sprites/sprite_create_demo.jdb
git mv jdb/sprite_demo.jdb        jdb/demos/sprites/sprite_demo.jdb
git mv jdb/sprite_gen.jdb         jdb/demos/sprites/sprite_gen.jdb
git mv jdb/sprite_test.jdb        jdb/demos/sprites/sprite_test.jdb
git mv jdb/sprite_walk.jdb        jdb/demos/sprites/sprite_walk.jdb
git mv jdb/tilemap_demo.jdb       jdb/demos/sprites/tilemap_demo.jdb
git mv jdb/invader_soa.jdb        jdb/demos/sprites/invader_soa.jdb
git mv jdb/invader_v1.jdb         jdb/demos/sprites/invader_v1.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / WORKFLOW (n8n-style + reactive + regex) ===============
# ════════════════════════════════════════════════════════════════════
git mv jdb/workflow_pipe.jdb jdb/demos/workflow/workflow_pipe.jdb
git mv jdb/workflow_run.jdb  jdb/demos/workflow/workflow_run.jdb
git mv jdb/workflow_v3.jdb   jdb/demos/workflow/workflow_v3.jdb
git mv jdb/node_regex.jdb    jdb/demos/workflow/node_regex.jdb
git mv jdb/lala.jdb          jdb/demos/workflow/lala.jdb
git mv jdb/re_basics.jdb     jdb/demos/workflow/re_basics.jdb
git mv jdb/re_library.jdb    jdb/demos/workflow/re_library.jdb
git mv jdb/event_bus.jdb     jdb/demos/workflow/event_bus.jdb
git mv jdb/eval_expr.jdb     jdb/demos/workflow/eval_expr.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS / TENSOR (TF / ML primitives) ===========================
# ════════════════════════════════════════════════════════════════════
git mv jdb/tensor_v1.jdb    jdb/demos/tensor/tensor_v1.jdb
git mv jdb/tensor_train.jdb jdb/demos/tensor/tensor_train.jdb
git mv jdb/tensor_bench.jdb jdb/demos/tensor/tensor_bench.jdb
git mv jdb/nl_start.jdb     jdb/demos/tensor/nl_start.jdb
git mv jdb/nl_part2.jdb     jdb/demos/tensor/nl_part2.jdb
git mv jdb/nl_part3.jdb     jdb/demos/tensor/nl_part3.jdb
git mv jdb/nl_part4.jdb     jdb/demos/tensor/nl_part4.jdb

# ════════════════════════════════════════════════════════════════════
# === TUTORIALS (language-basics snippets for newcomers) ============
# ════════════════════════════════════════════════════════════════════
git mv jdb/array_basics.jdb        jdb/tutorials/array_basics.jdb
git mv jdb/map_basics.jdb          jdb/tutorials/map_basics.jdb
git mv jdb/map_ext.jdb             jdb/tutorials/map_ext.jdb
git mv jdb/map_filter.jdb          jdb/tutorials/map_filter.jdb
git mv jdb/map_keys.jdb            jdb/tutorials/map_keys.jdb
git mv jdb/str_basics.jdb          jdb/tutorials/str_basics.jdb
git mv jdb/str_format.jdb          jdb/tutorials/str_format.jdb
git mv jdb/str_ops.jdb             jdb/tutorials/str_ops.jdb
git mv jdb/if_blocks.jdb           jdb/tutorials/if_blocks.jdb
git mv jdb/loop_control.jdb        jdb/tutorials/loop_control.jdb
git mv jdb/lambda_capture.jdb      jdb/tutorials/lambda_capture.jdb
git mv jdb/lambda_ops.jdb          jdb/tutorials/lambda_ops.jdb
git mv jdb/func_factory.jdb        jdb/tutorials/func_factory.jdb
git mv jdb/func_recurse.jdb        jdb/tutorials/func_recurse.jdb
git mv jdb/destructure.jdb         jdb/tutorials/destructure.jdb
git mv jdb/enum_types.jdb          jdb/tutorials/enum_types.jdb
git mv jdb/try_catch.jdb           jdb/tutorials/try_catch.jdb
git mv jdb/error_throw.jdb         jdb/tutorials/error_throw.jdb
git mv jdb/type_cast.jdb           jdb/tutorials/type_cast.jdb
git mv jdb/custom_ops.jdb          jdb/tutorials/custom_ops.jdb
git mv jdb/logic_vec.jdb           jdb/tutorials/logic_vec.jdb
git mv jdb/int64_ops.jdb           jdb/tutorials/int64_ops.jdb
git mv jdb/factorial.jdb           jdb/tutorials/factorial.jdb
git mv jdb/locale_test.jdb         jdb/tutorials/locale_test.jdb
git mv jdb/binary_io.jdb           jdb/tutorials/binary_io.jdb
git mv jdb/file_reader.jdb         jdb/tutorials/file_reader.jdb
git mv jdb/file_write.jdb          jdb/tutorials/file_write.jdb
git mv jdb/dir_matrix.jdb          jdb/tutorials/dir_matrix.jdb
git mv jdb/csv_stats.jdb           jdb/tutorials/csv_stats.jdb
git mv jdb/world_clock.jdb         jdb/tutorials/world_clock.jdb
git mv jdb/os_detect.jdb           jdb/tutorials/os_detect.jdb
git mv jdb/codec_tools.jdb         jdb/tutorials/codec_tools.jdb
git mv jdb/sha256_gen.jdb          jdb/tutorials/sha256_gen.jdb
git mv jdb/date_vec.jdb            jdb/tutorials/date_vec.jdb
git mv jdb/num_theory.jdb          jdb/tutorials/num_theory.jdb
git mv jdb/num_integral.jdb        jdb/tutorials/num_integral.jdb
git mv jdb/eqn_solver.jdb          jdb/tutorials/eqn_solver.jdb
git mv jdb/fem_1d.jdb              jdb/tutorials/fem_1d.jdb
git mv jdb/mlab_test.jdb           jdb/tutorials/mlab_test.jdb
git mv jdb/mlab_bench.jdb          jdb/tutorials/mlab_bench.jdb
git mv jdb/mlab_stats.jdb          jdb/tutorials/mlab_stats.jdb
git mv jdb/proc_gen.jdb            jdb/tutorials/proc_gen.jdb
git mv jdb/sys_logger.jdb          jdb/tutorials/sys_logger.jdb
git mv jdb/wavi.jdb                jdb/tutorials/wavi.jdb
git mv jdb/movie_data.jdb          jdb/tutorials/movie_data.jdb

# ════════════════════════════════════════════════════════════════════
# === EMU (Apple II + 6502 emulator suite) ==========================
# ════════════════════════════════════════════════════════════════════
git mv jdb/emu_run.jdb                jdb/emu/emu_run.jdb
git mv jdb/boot_probe.jdb             jdb/emu/boot_probe.jdb
git mv jdb/bench_cpu_speed.jdb        jdb/emu/bench_cpu_speed.jdb
git mv jdb/test_apple2.jdb            jdb/emu/test_apple2.jdb
git mv jdb/test_cpu6502.jdb           jdb/emu/test_cpu6502.jdb
git mv jdb/test_demo_prog.jdb         jdb/emu/test_demo_prog.jdb
git mv jdb/test_glyph_cache.jdb       jdb/emu/test_glyph_cache.jdb
git mv jdb/test_pc_hook.jdb           jdb/emu/test_pc_hook.jdb
git mv jdb/test_switch_multicase.jdb  jdb/emu/test_switch_multicase.jdb
git mv jdb/test_modglob.jdb           jdb/emu/test_modglob.jdb
git mv jdb/test_modwrap.jdb           jdb/emu/test_modwrap.jdb
git mv jdb/test_return_call_strarg.jdb jdb/emu/test_return_call_strarg.jdb
git mv jdb/test_binread.jdb           jdb/emu/test_binread.jdb
git mv jdb/test_resume.jdb            jdb/emu/test_resume.jdb
git mv jdb/test.jdb                   jdb/emu/test.jdb

# ════════════════════════════════════════════════════════════════════
# === TOOLS / UTILITIES (keep at jdb/ top? or jdb/tools/?) ===========
# ════════════════════════════════════════════════════════════════════
git mv jdb/bundler.jdb         jdb/tools/bundler.jdb
git mv jdb/winpos_probe.jdb    jdb/tools/winpos_probe.jdb
git mv jdb/screeni.jdb         jdb/tools/screeni.jdb
git mv jdb/basiclogo.jdb       jdb/tools/basiclogo.jdb

# ════════════════════════════════════════════════════════════════════
# === DROP CANDIDATES (delete after confirming) =====================
# ════════════════════════════════════════════════════════════════════
# Lots of headerless tiny scratch files. Either DROP (uncomment `git rm`)
# or let the auto-sweep below carry them into jdb/_scratch/.
#
# git rm jdb/acct.jdb
# git rm jdb/ansi_styles.jdb
# git rm jdb/ascii.jdb
git rm jdb/bench_fib.jdb
git rm jdb/bench_mixed.jdb
git rm jdb/benchmark.jdb
# git rm jdb/bit_ops.jdb
# git rm jdb/branch_logic.jdb
# git rm jdb/code_one_line.jdb
# git rm jdb/entferner.jdb
# git rm jdb/error_info.jdb
# git rm jdb/exit_test.jdb
# git rm jdb/fib.jdb
# git rm jdb/fnt.jdb
# git rm jdb/fso_util.jdb
git rm jdb/komisch.jdb
# git rm jdb/lambda.jdb
# git rm jdb/logic_nor.jdb
# git rm jdb/loop_logic.jdb
# git rm jdb/loop_step.jdb
# git rm jdb/loop_vars.jdb
# git rm jdb/math_ext.jdb
# git rm jdb/math_test.jdb
# git rm jdb/matrix_let.jdb
# git rm jdb/matrix_view.jdb
# git rm jdb/nested_for.jdb
# git rm jdb/oneliner.jdb
# git rm jdb/pipe_v2.jdb
# git rm jdb/py_chart.jdb
# git rm jdb/race.jdb
# git rm jdb/random_arr.jdb
# git rm jdb/re_sort.jdb
# git rm jdb/self_ref.jdb
# git rm jdb/set_lookup.jdb
# git rm jdb/simple.jdb
git rm jdb/sine.jdb
git rm jdb/stoppi.jdb
git rm jdb/stoppi_loop.jdb
# git rm jdb/str_multi.jdb
# git rm jdb/switch_case.jdb
git rm jdb/test_resume.jdb
# git rm jdb/type_id.jdb
# git rm jdb/typi.jdb
git rm jdb/wstest.jdb
git rm jdb/wstest2.jdb
git rm jdb/lall.jdb

# ════════════════════════════════════════════════════════════════════
# === RUNTIME / DATA JUNK currently tracked → gitignore + git rm ====
# ════════════════════════════════════════════════════════════════════
# git rm jdb/Orders.accdb
git rm jdb/Orders.laccdb
git rm jdb/savegame.bin
git rm jdb/test_np.bin
git rm jdb/test_out.bin

# ════════════════════════════════════════════════════════════════════
# === AUTO-SWEEP: anything left at jdb/ top-level → jdb/_scratch/ ===
# ════════════════════════════════════════════════════════════════════
# Set ENABLE_SCRATCH_SWEEP=1 at the top to enable. Files matching one
# of these names are KEEPLIST'd and never swept (README, etc.).
if [ "$ENABLE_SCRATCH_SWEEP" = "1" ]; then
    keeplist_regex='^(README\.md|REMOTE\.jdb|greet\.jdb|winpos\.json)$'
    shopt -s nullglob
    for f in jdb/*.jdb jdb/*.txt jdb/*.json jdb/*.md jdb/*.png jdb/*.csv; do
        base="$(basename "$f")"
        if [[ "$base" =~ $keeplist_regex ]]; then
            echo "[keep]    $f"
            continue
        fi
        echo "[scratch] git mv $f jdb/_scratch/$base"
        git mv "$f" "jdb/_scratch/$base"
    done
    shopt -u nullglob
else
    echo
    echo "Auto-sweep is OFF. Set ENABLE_SCRATCH_SWEEP=1 at the top to enable."
    echo "Inspect what would move with:"
    echo "    ls jdb/*.jdb | wc -l   # files still at top after your moves"
fi

echo
echo "Done. Next steps:"
echo "  1. ls jdb/  → verify the new layout"
echo "  2. Run the pre-commit gate to check IMPORT paths still resolve"
echo "  3. Commit + push when green"
