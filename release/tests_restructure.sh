#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════
#  tests_restructure.sh — generated from release/tests_inventory.md
#
#  HOW TO USE
#  ----------
#  1. Review every `# git mv ...` line. Un-comment (delete leading `# `)
#     the moves you want to execute.
#  2. Run from repo root:   bash release/tests_restructure.sh
#  3. The "auto-sweep" block at the end gathers everything still sitting
#     at tests/ top level into tests/_scratch/. It is OFF by default —
#     set ENABLE_SCRATCH_SWEEP=1 below to enable.
#  4. After the script, **update `.claude/skills/jdbgate/SKILL.md`** if
#     you moved the gate tests, then run the gate to verify everything
#     still loads. IMPORT helpers (TEST_OUTER, native_test_modx, ...)
#     must travel WITH their consumers — jdBasic's IMPORT searches the
#     calling script's directory.
#
#  HELPER → CONSUMER LINKS (DO NOT SEPARATE)
#  ----------------------------------------
#    TEST_OUTER         ← comprehensive_test          (→ tests/gate/)
#    test_inner         ← TEST_OUTER                  (→ tests/gate/)
#    native_test_modx   ← native_test                 (→ tests/gate/)
#    static_test_mod    ← native_test                 (→ tests/gate/)
#    storage            ← native_test                 (→ tests/gate/)
#    askmod             ← test_cross_module_ask       (→ tests/modules/)
#    test_follow_mod    ← test_follow                 (→ tests/modules/)
#    test_gold_mod      ← test_gold_map_call, ...     (→ tests/modules/)
#    test_mod_writeback_mod ← test_mod_writeback      (→ tests/modules/)
#    test_xmod_array_mod    ← test_xmod_array         (→ tests/modules/)
#    test_xmod_funcarg_mod  ← test_xmod_funcarg       (→ tests/modules/)
#    test_imp_mod       ← comprehensive_test (?)      (→ tests/gate/ ?)
#    provider           ← (depends on consumer)
# ════════════════════════════════════════════════════════════════════

set -e

cd "$(dirname "$0")/.."

ENABLE_SCRATCH_SWEEP=1

# ──────────────────────────────────────────────────────────────────
#  Target directories
# ──────────────────────────────────────────────────────────────────
mkdir -p tests/gate
mkdir -p tests/tui
mkdir -p tests/strict
mkdir -p tests/udt
mkdir -p tests/native
mkdir -p tests/channels
mkdir -p tests/rag
mkdir -p tests/ai
mkdir -p tests/ffi
mkdir -p tests/http
mkdir -p tests/async
mkdir -p tests/eval
mkdir -p tests/crash
mkdir -p tests/modules
mkdir -p tests/regression
mkdir -p tests/tutorial
mkdir -p tests/demos
mkdir -p tests/_scratch

# ════════════════════════════════════════════════════════════════════
# === GATE (4 suites + their IMPORT helpers — must travel together) =
# ════════════════════════════════════════════════════════════════════
git mv tests/comprehensive_test.jdb tests/gate/comprehensive_test.jdb
git mv tests/native_test.jdb        tests/gate/native_test.jdb
git mv tests/test_apl_complete.jdb  tests/gate/test_apl_complete.jdb
git mv tests/test_apl_pipelines.jdb tests/gate/test_apl_pipelines.jdb
git mv tests/TEST_OUTER.jdb         tests/gate/TEST_OUTER.jdb
git mv tests/test_inner.jdb         tests/gate/test_inner.jdb
git mv tests/native_test_modx.jdb   tests/gate/native_test_modx.jdb
git mv tests/static_test_mod.jdb    tests/gate/static_test_mod.jdb
git mv tests/storage.jdb            tests/gate/storage.jdb
git mv tests/test_imp_mod.jdb       tests/gate/test_imp_mod.jdb

# ════════════════════════════════════════════════════════════════════
# === TUI (phases + smoke, all self-contained) ======================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_tui_smoke.jdb   tests/tui/test_tui_smoke.jdb
git mv tests/test_tui_phase_a.jdb tests/tui/test_tui_phase_a.jdb
git mv tests/test_tui_phase_b.jdb tests/tui/test_tui_phase_b.jdb
git mv tests/test_tui_phase_c.jdb tests/tui/test_tui_phase_c.jdb
git mv tests/test_tui_phase_d.jdb tests/tui/test_tui_phase_d.jdb
git mv tests/test_tui_phase_e.jdb tests/tui/test_tui_phase_e.jdb
git mv tests/test_tui_phase_f.jdb tests/tui/test_tui_phase_f.jdb
git mv tests/test_tui_phase_g.jdb tests/tui/test_tui_phase_g.jdb

# ════════════════════════════════════════════════════════════════════
# === STRICT (compiler enforcement) =================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_strict_dim.jdb           tests/strict/test_strict_dim.jdb
git mv tests/test_strict_dim2.jdb          tests/strict/test_strict_dim2.jdb
git mv tests/test_strict_explicit_bad.jdb  tests/strict/test_strict_explicit_bad.jdb
git mv tests/test_strict_explicit_good.jdb tests/strict/test_strict_explicit_good.jdb
git mv tests/test_strict_type_bad.jdb      tests/strict/test_strict_type_bad.jdb
git mv tests/test_strict_type_good.jdb     tests/strict/test_strict_type_good.jdb

# ════════════════════════════════════════════════════════════════════
# === UDT (lifecycle: INIT / DISPOSE / arrays) ======================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_udt_init_basic.jdb         tests/udt/test_udt_init_basic.jdb
git mv tests/test_udt_init_default.jdb       tests/udt/test_udt_init_default.jdb
git mv tests/test_udt_init_no_args.jdb       tests/udt/test_udt_init_no_args.jdb
git mv tests/test_udt_dispose_local.jdb      tests/udt/test_udt_dispose_local.jdb
git mv tests/test_udt_dispose_reassign.jdb   tests/udt/test_udt_dispose_reassign.jdb
git mv tests/test_udt_array_dispose.jdb      tests/udt/test_udt_array_dispose.jdb
git mv tests/test_udt_array_vector_init.jdb  tests/udt/test_udt_array_vector_init.jdb
git mv tests/test_udt_scalar_field.jdb       tests/udt/test_udt_scalar_field.jdb
git mv tests/test_udt_arr.jdb                tests/udt/test_udt_arr.jdb

# ════════════════════════════════════════════════════════════════════
# === NATIVE codegen regressions ====================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_native_format_date.jdb       tests/native/test_native_format_date.jdb
git mv tests/test_native_gaps.jdb              tests/native/test_native_gaps.jdb
git mv tests/test_native_line_matrix.jdb       tests/native/test_native_line_matrix.jdb
git mv tests/test_native_mixed_array.jdb       tests/native/test_native_mixed_array.jdb
git mv tests/test_native_os_args.jdb           tests/native/test_native_os_args.jdb
git mv tests/test_native_os_exec.jdb           tests/native/test_native_os_exec.jdb
git mv tests/test_native_rotchain.jdb          tests/native/test_native_rotchain.jdb
git mv tests/test_native_str_nested.jdb        tests/native/test_native_str_nested.jdb
git mv tests/test_native_transpose_drop.jdb    tests/native/test_native_transpose_drop.jdb
git mv tests/test_native_v_only.jdb            tests/native/test_native_v_only.jdb
git mv tests/test_native_wire_full.jdb         tests/native/test_native_wire_full.jdb
git mv tests/test_native_wire_i64cx.jdb        tests/native/test_native_wire_i64cx.jdb
git mv tests/test_native_wire_lines.jdb        tests/native/test_native_wire_lines.jdb
git mv tests/test_native_wire_loop.jdb         tests/native/test_native_wire_loop.jdb
git mv tests/test_native_wire_pipeline.jdb     tests/native/test_native_wire_pipeline.jdb
git mv tests/test_native_wire_with_sub.jdb     tests/native/test_native_wire_with_sub.jdb
git mv tests/test_native_mul.jdb               tests/native/test_native_mul.jdb
git mv tests/test_native_outer.jdb             tests/native/test_native_outer.jdb
git mv tests/test_native_outer_zero.jdb        tests/native/test_native_outer_zero.jdb
git mv tests/test_native_pow.jdb               tests/native/test_native_pow.jdb
git mv tests/test_native_take_2d.jdb           tests/native/test_native_take_2d.jdb
git mv tests/test_cls_native.jdb               tests/native/test_cls_native.jdb
git mv tests/test_map_exists_native.jdb        tests/native/test_map_exists_native.jdb
git mv tests/test_split_index_native.jdb      tests/native/test_split_index_native.jdb

# ════════════════════════════════════════════════════════════════════
# === CHANNELS + FILE-STREAMING =====================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_channels.jdb            tests/channels/test_channels.jdb
git mv tests/test_channels_concurrent.jdb tests/channels/test_channels_concurrent.jdb
git mv tests/test_foreach_chan.jdb        tests/channels/test_foreach_chan.jdb
git mv tests/test_file_streaming.jdb      tests/channels/test_file_streaming.jdb
git mv tests/test_file_tail.jdb           tests/channels/test_file_tail.jdb

# ════════════════════════════════════════════════════════════════════
# === RAG + HNSW + PDF ==============================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_rag.jdb              tests/rag/test_rag.jdb
git mv tests/test_rag_dense_persist.jdb tests/rag/test_rag_dense_persist.jdb
git mv tests/test_rag_dir.jdb          tests/rag/test_rag_dir.jdb
git mv tests/test_rag_multi_query.jdb  tests/rag/test_rag_multi_query.jdb
git mv tests/test_rag_save_load.jdb    tests/rag/test_rag_save_load.jdb
git mv tests/test_rag_sources.jdb      tests/rag/test_rag_sources.jdb
git mv tests/test_hnsw.jdb             tests/rag/test_hnsw.jdb
git mv tests/test_hnsw_speed.jdb       tests/rag/test_hnsw_speed.jdb
git mv tests/test_pdf_compressed.jdb   tests/rag/test_pdf_compressed.jdb
git mv tests/test_pdf_extract.jdb      tests/rag/test_pdf_extract.jdb
git mv tests/make_test_pdf.jdb         tests/rag/make_test_pdf.jdb

# ════════════════════════════════════════════════════════════════════
# === AI (classifier, embed, tools, json mode) ======================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_classifier.jdb      tests/ai/test_classifier.jdb
git mv tests/test_embed_api.jdb       tests/ai/test_embed_api.jdb
git mv tests/test_tools.jdb           tests/ai/test_tools.jdb
git mv tests/test_json_mode.jdb       tests/ai/test_json_mode.jdb
git mv tests/test_sound_precision.jdb tests/ai/test_sound_precision.jdb

# ════════════════════════════════════════════════════════════════════
# === FFI (DECLARE FUNC + COM) ======================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_ffi_smoke.jdb        tests/ffi/test_ffi_smoke.jdb
git mv tests/test_ffi_native_diag.jdb  tests/ffi/test_ffi_native_diag.jdb
git mv tests/test_ffi_return.jdb       tests/ffi/test_ffi_return.jdb
git mv tests/test_ffi_string_param.jdb tests/ffi/test_ffi_string_param.jdb
git mv tests/test_com.jdb              tests/ffi/test_com.jdb
git mv tests/test_com2.jdb             tests/ffi/test_com2.jdb

# ════════════════════════════════════════════════════════════════════
# === HTTP ==========================================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_http.jdb       tests/http/test_http.jdb
git mv tests/test_http_async.jdb tests/http/test_http_async.jdb
git mv tests/test_http_gd.jdb    tests/http/test_http_gd.jdb

# ════════════════════════════════════════════════════════════════════
# === ASYNC + THREADING =============================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_async.jdb  tests/async/test_async.jdb
git mv tests/test_async2.jdb tests/async/test_async2.jdb

# ════════════════════════════════════════════════════════════════════
# === EVAL / EXECUTE regressions ====================================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_eval.jdb              tests/eval/test_eval.jdb
git mv tests/test_eval_nested.jdb       tests/eval/test_eval_nested.jdb
git mv tests/test_execute_in_try.jdb    tests/eval/test_execute_in_try.jdb
git mv tests/test_execute_no_try.jdb    tests/eval/test_execute_no_try.jdb
git mv tests/test_eval_after.jdb        tests/eval/test_eval_after.jdb
git mv tests/test_eval_then_func.jdb    tests/eval/test_eval_then_func.jdb
git mv tests/test_eval_top.jdb          tests/eval/test_eval_top.jdb
git mv tests/test_eval_twice.jdb        tests/eval/test_eval_twice.jdb
git mv tests/test_eval_min.jdb          tests/eval/test_eval_min.jdb
git mv tests/test_eval_min2.jdb         tests/eval/test_eval_min2.jdb
git mv tests/test_eval_min3.jdb         tests/eval/test_eval_min3.jdb
git mv tests/test_eval_seq.jdb          tests/eval/test_eval_seq.jdb
git mv tests/test_eval_seq2.jdb         tests/eval/test_eval_seq2.jdb
git mv tests/test_eval_seq3.jdb         tests/eval/test_eval_seq3.jdb

# ════════════════════════════════════════════════════════════════════
# === CRASH RESILIENCE ==============================================
# ════════════════════════════════════════════════════════════════════
 git mv tests/crash_test.jdb tests/crash/crash_test.jdb

# ════════════════════════════════════════════════════════════════════
# === MODULES / Cross-module + globals (helpers go WITH consumers) ==
# ════════════════════════════════════════════════════════════════════
git mv tests/test_xmod_array.jdb         tests/modules/test_xmod_array.jdb
git mv tests/test_xmod_array_mod.jdb     tests/modules/test_xmod_array_mod.jdb
git mv tests/test_xmod_funcarg.jdb       tests/modules/test_xmod_funcarg.jdb
git mv tests/test_xmod_funcarg_mod.jdb   tests/modules/test_xmod_funcarg_mod.jdb
git mv tests/test_mod_writeback.jdb      tests/modules/test_mod_writeback.jdb
git mv tests/test_mod_writeback_mod.jdb  tests/modules/test_mod_writeback_mod.jdb
git mv tests/test_gold_map_call.jdb      tests/modules/test_gold_map_call.jdb
git mv tests/test_gold_multi_caller.jdb  tests/modules/test_gold_multi_caller.jdb
git mv tests/test_gold_mod.jdb           tests/modules/test_gold_mod.jdb
git mv tests/test_follow.jdb             tests/modules/test_follow.jdb
git mv tests/test_follow_mod.jdb         tests/modules/test_follow_mod.jdb
git mv tests/test_cross_module_ask.jdb   tests/modules/test_cross_module_ask.jdb
git mv tests/askmod.jdb                  tests/modules/askmod.jdb
git mv tests/test_module_funcref.jdb     tests/modules/test_module_funcref.jdb
git mv tests/test_member_name.jdb        tests/modules/test_member_name.jdb
git mv tests/test_member_name2.jdb       tests/modules/test_member_name2.jdb
git mv tests/test_member_name3.jdb       tests/modules/test_member_name3.jdb
git mv tests/test_sub_globals.jdb        tests/modules/test_sub_globals.jdb
git mv tests/provider.jdb                tests/modules/provider.jdb

# ════════════════════════════════════════════════════════════════════
# === REGRESSION (bug-repro with descriptive headers) ===============
# ════════════════════════════════════════════════════════════════════
# --- Arrays ---
git mv tests/test_array_row_alias.jdb       tests/regression/test_array_row_alias.jdb
git mv tests/test_array_row_alias2.jdb      tests/regression/test_array_row_alias2.jdb
git mv tests/test_alias_isolated.jdb        tests/regression/test_alias_isolated.jdb
git mv tests/test_string_array_push.jdb     tests/regression/test_string_array_push.jdb
git mv tests/test_str_array.jdb             tests/regression/test_str_array.jdb
git mv tests/test_multi_idx.jdb             tests/regression/test_multi_idx.jdb
git mv tests/test_slice.jdb                 tests/regression/test_slice.jdb
git mv tests/test_reshape.jdb               tests/regression/test_reshape.jdb
git mv tests/test_arr_idx_method.jdb        tests/regression/test_arr_idx_method.jdb
git mv tests/test_arr_idx_method_deep.jdb   tests/regression/test_arr_idx_method_deep.jdb
git mv tests/test_2d.jdb                    tests/regression/test_2d.jdb

# --- Strings / Formatting ---
git mv tests/test_frmv.jdb           tests/regression/test_frmv.jdb
git mv tests/test_format_vmh.jdb     tests/regression/test_format_vmh.jdb
git mv tests/test_dollar_coerce.jdb  tests/regression/test_dollar_coerce.jdb
git mv tests/test_tag7_to_str.jdb    tests/regression/test_tag7_to_str.jdb
git mv tests/test_codec.jdb          tests/regression/test_codec.jdb
git mv tests/test_map_str_concat.jdb tests/regression/test_map_str_concat.jdb
git mv tests/test_regex.jdb          tests/regression/test_regex.jdb
git mv tests/test_print.jdb          tests/regression/test_print.jdb
git mv tests/test_print_types.jdb    tests/regression/test_print_types.jdb
git mv tests/test_utf8.jdb           tests/regression/test_utf8.jdb
git mv tests/test_txt_cp1252.jdb     tests/regression/test_txt_cp1252.jdb

# --- Loops / Control flow ---
git mv tests/test_for.jdb            tests/regression/test_for.jdb
git mv tests/test_for_step_neg.jdb   tests/regression/test_for_step_neg.jdb
git mv tests/test_for_step_neg2.jdb  tests/regression/test_for_step_neg2.jdb
git mv tests/test_for_step_neg3.jdb  tests/regression/test_for_step_neg3.jdb
git mv tests/test_for_step_neg4.jdb  tests/regression/test_for_step_neg4.jdb
git mv tests/test_oneliner.jdb       tests/regression/test_oneliner.jdb
git mv tests/test_multi_case.jdb     tests/regression/test_multi_case.jdb
git mv tests/test_shadow_param.jdb   tests/regression/test_shadow_param.jdb
git mv tests/test_noparens.jdb       tests/regression/test_noparens.jdb

# --- Math / Vector / Sin ---
git mv tests/test_math.jdb           tests/regression/test_math.jdb
git mv tests/test_sin_arr.jdb        tests/regression/test_sin_arr.jdb
git mv tests/test_vec_ops.jdb        tests/regression/test_vec_ops.jdb
git mv tests/test_bitops_vector.jdb  tests/regression/test_bitops_vector.jdb
git mv tests/test_bnot.jdb           tests/regression/test_bnot.jdb
git mv tests/test_shift_infix.jdb    tests/regression/test_shift_infix.jdb
git mv tests/test_conversions.jdb    tests/regression/test_conversions.jdb
git mv tests/test_new_array_ops.jdb  tests/regression/test_new_array_ops.jdb
git mv tests/test_pipe_tag.jdb       tests/regression/test_pipe_tag.jdb

# --- Types / DIM / Casting ---
git mv tests/test_destruct.jdb        tests/regression/test_destruct.jdb
git mv tests/test_enum.jdb            tests/regression/test_enum.jdb
git mv tests/test_date_dim_bug.jdb    tests/regression/test_date_dim_bug.jdb
git mv tests/test_date_dim_works.jdb  tests/regression/test_date_dim_works.jdb
git mv tests/test_datetime.jdb        tests/regression/test_datetime.jdb

# --- Functions / Higher-order ---
git mv tests/test_func_array_param.jdb     tests/regression/test_func_array_param.jdb
git mv tests/test_func_local_array.jdb     tests/regression/test_func_local_array.jdb
git mv tests/test_funcref.jdb              tests/regression/test_funcref.jdb
git mv tests/test_functional.jdb           tests/regression/test_functional.jdb
git mv tests/test_lambda.jdb               tests/regression/test_lambda.jdb
git mv tests/test_b_fixes.jdb              tests/regression/test_b_fixes.jdb

# --- Maps / JSON ---
git mv tests/test_map.jdb           tests/regression/test_map.jdb
git mv tests/test_map_values.jdb    tests/regression/test_map_values.jdb
git mv tests/test_json_lookup.jdb   tests/regression/test_json_lookup.jdb
git mv tests/test_json_push.jdb     tests/regression/test_json_push.jdb

# --- Sprites / Graphics ---
git mv tests/test_sprite_create_save_load.jdb tests/regression/test_sprite_create_save_load.jdb

# --- Errors / Exceptions ---
git mv tests/test_errors.jdb         tests/regression/test_errors.jdb
git mv tests/test_try.jdb            tests/regression/test_try.jdb
git mv tests/test_try_no_execute.jdb tests/regression/test_try_no_execute.jdb

# --- REPL / VM / Workspace ---
git mv tests/test_dap_repl.jdb        tests/regression/test_dap_repl.jdb
git mv tests/test_dump.jdb            tests/regression/test_dump.jdb
git mv tests/test_dump_run.jdb        tests/regression/test_dump_run.jdb
git mv tests/test_help.jdb            tests/regression/test_help.jdb
git mv tests/test_resume.jdb          tests/regression/test_resume.jdb
git mv tests/test_resume2.jdb         tests/regression/test_resume2.jdb
git mv tests/test_resume_sim.jdb      tests/regression/test_resume_sim.jdb
git mv tests/test_stop.jdb            tests/regression/test_stop.jdb
git mv tests/test_codegen_hint_leak.jdb tests/regression/test_codegen_hint_leak.jdb
git mv tests/mcp_resume_chain.jdb     tests/regression/mcp_resume_chain.jdb

# --- Reactive (REACT) ---
git mv tests/test_react.jdb        tests/regression/test_react.jdb
git mv tests/test_react_cache.jdb  tests/regression/test_react_cache.jdb
git mv tests/test_react_udt.jdb    tests/regression/test_react_udt.jdb
git mv tests/test_react_udt2.jdb   tests/regression/test_react_udt2.jdb

# --- File I/O / OS ---
git mv tests/test_fileio.jdb     tests/regression/test_fileio.jdb
git mv tests/test_fs.jdb         tests/regression/test_fs.jdb
git mv tests/test_binread.jdb    tests/regression/test_binread.jdb
git mv tests/test_pwd_cd.jdb     tests/regression/test_pwd_cd.jdb
git mv tests/test_os.jdb         tests/regression/test_os.jdb
git mv tests/test_getos.jdb      tests/regression/test_getos.jdb
git mv tests/test_ip.jdb         tests/regression/test_ip.jdb
git mv tests/test_env_neg.jdb    tests/regression/test_env_neg.jdb

# --- Misc / Smoke ---
git mv tests/test_input.jdb          tests/regression/test_input.jdb
git mv tests/test_props.jdb          tests/regression/test_props.jdb
git mv tests/test_semi.jdb           tests/regression/test_semi.jdb
git mv tests/test_operators.jdb      tests/regression/test_operators.jdb
git mv tests/test_output_capture.jdb tests/regression/test_output_capture.jdb
git mv tests/test_tutorial_samples.jdb tests/regression/test_tutorial_samples.jdb
git mv tests/test_playbuffer.jdb     tests/regression/test_playbuffer.jdb
git mv tests/test_apl.jdb            tests/regression/test_apl.jdb
git mv tests/test_apl2.jdb           tests/regression/test_apl2.jdb
git mv tests/test_newfuncs.jdb       tests/regression/test_newfuncs.jdb
git mv tests/test_cmds.jdb           tests/regression/test_cmds.jdb

# ════════════════════════════════════════════════════════════════════
# === TUTORIAL (simple language-exercise programs) ==================
# ════════════════════════════════════════════════════════════════════
git mv tests/test_strings.jdb tests/tutorial/test_strings.jdb
git mv tests/test_letters.jdb tests/tutorial/test_letters.jdb
git mv tests/test.jdb         tests/tutorial/test.jdb
git mv tests/test_switch.jdb  tests/tutorial/test_switch.jdb
git mv tests/test_type.jdb    tests/tutorial/test_type.jdb
git mv tests/test_vec.jdb     tests/tutorial/test_vec.jdb
git mv tests/test_vec2.jdb    tests/tutorial/test_vec2.jdb
git mv tests/test_cvdate.jdb  tests/tutorial/test_cvdate.jdb

# ════════════════════════════════════════════════════════════════════
# === DEMOS (actually demos, masquerading as tests) =================
# ════════════════════════════════════════════════════════════════════
git mv tests/dupfinder.jdb     tests/demos/dupfinder.jdb
git mv tests/demo_group_a.jdb  tests/demos/demo_group_a.jdb
git mv tests/demo_group_b.jdb  tests/demos/demo_group_b.jdb
git mv tests/demo_group_c.jdb  tests/demos/demo_group_c.jdb
git mv tests/demo_group_d.jdb  tests/demos/demo_group_d.jdb

# ════════════════════════════════════════════════════════════════════
# === SCRATCH CANDIDATES (debug iteration variants, no headers) =====
# ════════════════════════════════════════════════════════════════════
# Move these explicitly (or let auto-sweep grab them at the end).
#
git mv tests/test_arr_idx.jdb           tests/_scratch/test_arr_idx.jdb
git mv tests/test_arr_lit_from_map.jdb  tests/_scratch/test_arr_lit_from_map.jdb
git mv tests/test_arr_lit_min.jdb       tests/_scratch/test_arr_lit_min.jdb
git mv tests/test_bare.jdb              tests/_scratch/test_bare.jdb
git mv tests/test_bare2.jdb             tests/_scratch/test_bare2.jdb
git mv tests/test_bio.jdb               tests/_scratch/test_bio.jdb
git mv tests/test_bio2.jdb              tests/_scratch/test_bio2.jdb
git mv tests/test_biorhythm.jdb         tests/_scratch/test_biorhythm.jdb
git mv tests/test_builtin_cmds.jdb      tests/_scratch/test_builtin_cmds.jdb
git mv tests/test_builtin_cmds2.jdb     tests/_scratch/test_builtin_cmds2.jdb
git mv tests/test_combo_full.jdb        tests/_scratch/test_combo_full.jdb
git mv tests/test_format_mixed.jdb      tests/_scratch/test_format_mixed.jdb
git mv tests/test_frmv_bug.jdb          tests/_scratch/test_frmv_bug.jdb
git mv tests/test_func.jdb              tests/_scratch/test_func.jdb
git mv tests/test_func2.jdb             tests/_scratch/test_func2.jdb
git mv tests/test_func3.jdb             tests/_scratch/test_func3.jdb
git mv tests/test_func_local_map.jdb    tests/_scratch/test_func_local_map.jdb
git mv tests/test_func_local_map_init.jdb tests/_scratch/test_func_local_map_init.jdb
git mv tests/test_json_exists.jdb       tests/_scratch/test_json_exists.jdb
git mv tests/test_json_push2.jdb        tests/_scratch/test_json_push2.jdb
git mv tests/test_oneliner2.jdb         tests/_scratch/test_oneliner2.jdb
git mv tests/test_reshape2.jdb          tests/_scratch/test_reshape2.jdb
git mv tests/test_simple_debug.jdb      tests/_scratch/test_simple_debug.jdb
git mv tests/rpg_test.jdb               tests/_scratch/rpg_test.jdb
git mv tests/vo_locked_repro.jdb        tests/_scratch/vo_locked_repro.jdb

# ════════════════════════════════════════════════════════════════════
# === AUTO-SWEEP ====================================================
#  Anything still sitting at tests/ top-level (any extension we care
#  about) gets pushed to tests/_scratch/. OFF by default; keeplist
#  protects files we never want swept.
# ════════════════════════════════════════════════════════════════════
if [ "$ENABLE_SCRATCH_SWEEP" = "1" ]; then
    keeplist_regex='^(README\.md|SuperVanilla\.ttf|.*\.props|.*\.cpp|.*\.py)$'
    shopt -s nullglob
    for f in tests/*.jdb tests/*.bin tests/*.json tests/*.md tests/*.pdf; do
        base="$(basename "$f")"
        if [[ "$base" =~ $keeplist_regex ]]; then
            echo "[keep]    $f"
            continue
        fi
        echo "[scratch] git mv $f tests/_scratch/$base"
        git mv "$f" "tests/_scratch/$base"
    done
    shopt -u nullglob
else
    echo
    echo "Auto-sweep is OFF. Set ENABLE_SCRATCH_SWEEP=1 at the top to enable."
    echo "Files still at tests/ top-level after your moves:"
    echo "    ls tests/*.jdb | wc -l"
fi

echo
echo "Done. Next steps:"
echo "  1. ls tests/  → verify the new layout"
echo "  2. If you moved the gate suites, update .claude/skills/jdbgate/SKILL.md"
echo "     to reference tests/gate/comprehensive_test.jdb etc."
echo "  3. Run the gate to verify IMPORT paths still resolve"
echo "  4. Commit + push when green"
