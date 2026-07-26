#pragma once
//
// Central color palette for the FTXUI REPL (and the Editor once Phase 3
// lands). Change the values here to retheme the whole TUI in one spot -
// the rest of the code refers to slots by NAME, not by hard-coded
// Color::* constants.
//
// Slot naming follows intent, not hue:
//   accent / accent_bg   - primary brand colour, used for headers,
//                          prompts and the active-tab marker
//   fg_primary           - main readable text (output lines, etc.)
//   fg_dim / fg_muted    - secondary text (placeholders, hints,
//                          inactive tabs)
//   input_text           - the input row text colour
//   var_value / func_sig - side-panel data accents
//   error                - error lines in the output box
//
// Default palette matches the jdBasic.org website (cool-cyan brand).
// A second palette is sketched below for the eventual "warm" theme
// switch, kept commented out as a reference.

#include <ftxui/screen/color.hpp>

namespace jdb_theme {

inline const ftxui::Color accent       = ftxui::Color::Cyan3;
inline const ftxui::Color accent_bg    = ftxui::Color::DarkBlue;

inline const ftxui::Color fg_primary   = ftxui::Color::White;
inline const ftxui::Color fg_dim       = ftxui::Color::GrayLight;
inline const ftxui::Color fg_muted     = ftxui::Color::GrayDark;

inline const ftxui::Color input_text   = ftxui::Color::GreenLight;

inline const ftxui::Color var_value    = ftxui::Color::Yellow3;
inline const ftxui::Color func_sig     = ftxui::Color::Magenta;

inline const ftxui::Color error        = ftxui::Color::RedLight;

// Reference for a warm-toned scheme:
// inline const ftxui::Color accent     = ftxui::Color::Orange1;
// inline const ftxui::Color accent_bg  = ftxui::Color::DarkRed;
// inline const ftxui::Color input_text = ftxui::Color::Yellow3;

} // namespace jdb_theme
