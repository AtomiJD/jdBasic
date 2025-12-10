#include "AppConfig.hpp" // Check for JD_IMGUI definition

#ifdef JD_IMGUI

#include "NeReLaBasic.hpp"
#include "Error.hpp"
#include "Commands.hpp"
#include "Graphics.hpp"
#include "imgui.h"
#include <vector>
#include <string>

// Helper macro
#define CHECK_GUI_INIT(vm) if (!vm.graphics_system.is_initialized) { Error::set(15, vm.runtime_current_line, "GUI commands require an active SCREEN."); return {}; }

// GUI.FLAG(name$) -> integer value of the flag
BasicValue gui_flag(NeReLaBasic & vm, const std::vector<BasicValue>&args) {
    if (args.size() != 1) return 0.0;
    std::string name = to_upper(to_string(args[0]));

    if (name == "MENUBAR") return (double)ImGuiWindowFlags_MenuBar;
    if (name == "NO_RESIZE") return (double)ImGuiWindowFlags_NoResize;
    if (name == "NO_TITLEBAR") return (double)ImGuiWindowFlags_NoTitleBar;
    if (name == "NO_MOVE") return (double)ImGuiWindowFlags_NoMove;
    if (name == "NO_SCROLLBAR") return (double)ImGuiWindowFlags_NoScrollbar;
    if (name == "NO_COLLAPSE") return (double)ImGuiWindowFlags_NoCollapse;
    if (name == "ALWAYS_AUTO_RESIZE") return (double)ImGuiWindowFlags_AlwaysAutoResize;
    if (name == "NO_SAVED_SETTINGS") return (double)ImGuiWindowFlags_NoSavedSettings;

    return 0.0;
}

// GUI.COL(name$) -> integer value of ImGuiCol_ enum
BasicValue gui_col(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return 0.0;
    std::string name = to_upper(to_string(args[0]));

    if (name == "TEXT") return (double)ImGuiCol_Text;
    if (name == "WINDOWBG") return (double)ImGuiCol_WindowBg;
    if (name == "BUTTON") return (double)ImGuiCol_Button;
    if (name == "BUTTONHOVERED") return (double)ImGuiCol_ButtonHovered;
    if (name == "BUTTONACTIVE") return (double)ImGuiCol_ButtonActive;
    if (name == "HEADER") return (double)ImGuiCol_Header;
    if (name == "HEADERHOVERED") return (double)ImGuiCol_HeaderHovered;
    if (name == "HEADERACTIVE") return (double)ImGuiCol_HeaderActive;
    if (name == "FRAMEBG") return (double)ImGuiCol_FrameBg;
    if (name == "FRAMEBGHOVERED") return (double)ImGuiCol_FrameBgHovered;
    if (name == "FRAMEBGACTIVE") return (double)ImGuiCol_FrameBgActive;
    if (name == "TITLEBG") return (double)ImGuiCol_TitleBg;
    if (name == "TITLEBGACTIVE") return (double)ImGuiCol_TitleBgActive;
    if (name == "CHECKMARK") return (double)ImGuiCol_CheckMark;
    if (name == "SLIDERGRAB") return (double)ImGuiCol_SliderGrab;
    if (name == "SLIDERGRABACTIVE") return (double)ImGuiCol_SliderGrabActive;

    return 0.0;
}

// GUI.PUSH_STYLE_COLOR(idx, color_array)
// idx: The ID from GUI.COL
// color_array: [r, g, b, a] (0-255)
BasicValue gui_push_style_color(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) return false;

    int idx = (int)to_double(args[0]);

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) return false;
    auto arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!arr_ptr || arr_ptr->data.size() < 3) return false;

    float r = (float)to_double(arr_ptr->data[0]) / 255.0f;
    float g = (float)to_double(arr_ptr->data[1]) / 255.0f;
    float b = (float)to_double(arr_ptr->data[2]) / 255.0f;
    float a = (arr_ptr->data.size() > 3) ? (float)to_double(arr_ptr->data[3]) / 255.0f : 1.0f;

    ImGui::PushStyleColor(idx, ImVec4(r, g, b, a));
    return false;
}

// GUI.POP_STYLE_COLOR([count])
BasicValue gui_pop_style_color(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    int count = 1;
    if (args.size() > 0) {
        count = (int)to_double(args[0]);
    }
    ImGui::PopStyleColor(count);
    return false;
}

// GUI.BEGIN(title$, [x, y, w, h], [p_open], [flags]) -> new_p_open_state (boolean)
// Signatures:
// 1. (Title)
// 2. (Title, p_open)
// 3. (Title, p_open, flags)
// 4. (Title, x, y, w, h)
// 5. (Title, x, y, w, h, p_open)
// 6. (Title, x, y, w, h, p_open, flags)
BasicValue gui_begin(NeReLaBasic & vm, const std::vector<BasicValue>&args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string title = to_string(args[0]);
    bool has_pos_size = (args.size() >= 4); // x,y,w,h provided

    // Determine where optional args start
    size_t open_arg_index = has_pos_size ? 5 : 1;
    size_t flag_arg_index = has_pos_size ? 6 : 2;

    if (has_pos_size && args.size() >= 5) {
        float x = (float)to_double(args[1]);
        float y = (float)to_double(args[2]);
        float w = (float)to_double(args[3]);
        float h = (float)to_double(args[4]);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
    }

    bool is_open = true;
    bool* p_open = nullptr;
    ImGuiWindowFlags flags = 0;

    // Check for p_open argument
    if (args.size() > open_arg_index) {
        is_open = to_bool(args[open_arg_index]);
        p_open = &is_open;
    }

    // Check for flags argument
    if (args.size() > flag_arg_index) {
        flags = (ImGuiWindowFlags)to_double(args[flag_arg_index]);
    }

    ImGui::Begin(title.c_str(), p_open, flags);

    // If p_open was passed, we must return its new state (so BASIC can update the variable)
    if (p_open) {
        return is_open;
    }
    else {
        return true; // Just return true to allow "IF GUI.BEGIN(...) THEN"
    }
}

// GUI.END
BasicValue gui_end(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::End();
    return false;
}

// GUI.TEXT(text$)
BasicValue gui_text(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;
    ImGui::Text("%s", to_string(args[0]).c_str());
    return false;
}

// GUI.BUTTON(label$, [w, h]) -> Boolean
BasicValue gui_button(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string label = to_string(args[0]);
    ImVec2 size(0, 0);

    if (args.size() >= 3) {
        size.x = (float)to_double(args[1]);
        size.y = (float)to_double(args[2]);
    }

    return ImGui::Button(label.c_str(), size);
}

// GUI.INPUT(label$, current_value$) -> new_value$
BasicValue gui_input_text(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 2) return std::string("");

    std::string label = to_string(args[0]);
    std::string current_val = to_string(args[1]);

    char buffer[256];
    // Safe copy
    if (current_val.length() >= 256) current_val = current_val.substr(0, 255);
    
#if defined(_MSC_VER)
    strcpy_s(buffer, sizeof(buffer), current_val.c_str());
#else
    strncpy(buffer, current_val.c_str(), sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0'; // Ensure null termination on non-Windows
#endif

    if (ImGui::InputText(label.c_str(), buffer, sizeof(buffer))) {
        return std::string(buffer);
    }

    return current_val;
}

// GUI.SLIDER(label$, value, min, max) -> new_value
BasicValue gui_slider(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 4) return 0.0;

    std::string label = to_string(args[0]);
    float val = (float)to_double(args[1]);
    float v_min = (float)to_double(args[2]);
    float v_max = (float)to_double(args[3]);

    if (ImGui::SliderFloat(label.c_str(), &val, v_min, v_max)) {
        return (double)val;
    }
    return (double)val;
}

// GUI.CHECKBOX(label$, boolean_state) -> new_boolean_state
BasicValue gui_checkbox(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 2) return false;

    std::string label = to_string(args[0]);
    bool active = to_bool(args[1]);

    if (ImGui::Checkbox(label.c_str(), &active)) {
        return active;
    }
    return active;
}

// GUI.SAME_LINE
BasicValue gui_sameline(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::SameLine();
    return false;
}

// GUI.SEPARATOR
BasicValue gui_separator(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::Separator();
    return false;
}

// GUI.RADIO(label$, current_value, this_button_value) -> new_value
// Returns 'this_button_value' if selected, otherwise returns 'current_value'.
BasicValue gui_radio(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 3) return 0.0;

    std::string label = to_string(args[0]);
    double current_val = to_double(args[1]);
    double button_val = to_double(args[2]);

    // ImGui::RadioButton returns true if active
    if (ImGui::RadioButton(label.c_str(), current_val == button_val)) {
        return button_val;
    }
    return current_val;
}

// GUI.COMBO(label$, current_index, items_array) -> new_index
BasicValue gui_combo(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 3) return 0.0;

    std::string label = to_string(args[0]);
    int current_item = (int)to_double(args[1]);

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[2])) return (double)current_item;
    auto items_ptr = std::get<std::shared_ptr<Array>>(args[2]);
    if (!items_ptr) return (double)current_item;

    // Convert BASIC array to vector of strings for ImGui
    std::vector<std::string> item_strings;
    std::vector<const char*> item_ptrs;
    for (const auto& val : items_ptr->data) {
        item_strings.push_back(to_string(val));
    }
    for (const auto& s : item_strings) item_ptrs.push_back(s.c_str());

    if (ImGui::Combo(label.c_str(), &current_item, item_ptrs.data(), (int)item_ptrs.size())) {
        return (double)current_item;
    }
    return (double)current_item;
}

// GUI.LISTBOX(label$, current_index, items_array, [height_in_items]) -> new_index
BasicValue gui_listbox(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() < 3) return 0.0;

    std::string label = to_string(args[0]);
    int current_item = (int)to_double(args[1]);
    int height_in_items = (args.size() > 3) ? (int)to_double(args[3]) : 4;

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[2])) return (double)current_item;
    auto items_ptr = std::get<std::shared_ptr<Array>>(args[2]);
    if (!items_ptr) return (double)current_item;

    std::vector<std::string> item_strings;
    std::vector<const char*> item_ptrs;
    for (const auto& val : items_ptr->data) {
        item_strings.push_back(to_string(val));
    }
    for (const auto& s : item_strings) item_ptrs.push_back(s.c_str());

    if (ImGui::ListBox(label.c_str(), &current_item, item_ptrs.data(), (int)item_ptrs.size(), height_in_items)) {
        return (double)current_item;
    }
    return (double)current_item;
}

// GUI.PROGRESS(fraction, [overlay_text$])
BasicValue gui_progressbar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    float fraction = (float)to_double(args[0]);
    const char* overlay = NULL;
    std::string overlay_str;

    if (args.size() > 1) {
        overlay_str = to_string(args[1]);
        overlay = overlay_str.c_str();
    }

    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), overlay);
    return false;
}

// GUI.DUMMY(width, height)
BasicValue gui_dummy(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 2) return false;
    ImGui::Dummy(ImVec2((float)to_double(args[0]), (float)to_double(args[1])));
    return false;
}

// GUI.COLOR(label$, color_array) -> boolean (true if changed)
// color_array must be [r, g, b] or [r, g, b, a] in range 0-255.
BasicValue gui_color(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 2) return false;

    std::string label = to_string(args[0]);
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) return false;
    auto arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!arr_ptr || arr_ptr->data.size() < 3) return false;

    // Convert BASIC 0-255 to ImGui 0.0-1.0
    float col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    col[0] = (float)to_double(arr_ptr->data[0]) / 255.0f;
    col[1] = (float)to_double(arr_ptr->data[1]) / 255.0f;
    col[2] = (float)to_double(arr_ptr->data[2]) / 255.0f;

    bool has_alpha = (arr_ptr->data.size() >= 4);
    if (has_alpha) {
        col[3] = (float)to_double(arr_ptr->data[3]) / 255.0f;
    }

    int flags = has_alpha ? ImGuiColorEditFlags_None : ImGuiColorEditFlags_NoAlpha;

    if (ImGui::ColorEdit4(label.c_str(), col, flags)) {
        // Write back to BASIC array
        arr_ptr->data[0] = (double)(col[0] * 255.0f);
        arr_ptr->data[1] = (double)(col[1] * 255.0f);
        arr_ptr->data[2] = (double)(col[2] * 255.0f);
        if (has_alpha) {
            arr_ptr->data[3] = (double)(col[3] * 255.0f);
        }
        return true;
    }
    return false;
}

// GUI.THEME(theme_name$) "DARK", "LIGHT", "CLASSIC"
BasicValue gui_theme(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;
    std::string theme = to_upper(to_string(args[0]));
    if (theme == "DARK") ImGui::StyleColorsDark();
    else if (theme == "LIGHT") ImGui::StyleColorsLight();
    else if (theme == "CLASSIC") ImGui::StyleColorsClassic();
    return false;
}

// GUI.TOOLTIP(text$)
BasicValue gui_tooltip(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;
    ImGui::SetItemTooltip("%s", to_string(args[0]).c_str());
    return false;
}

// GUI.HELPMARKER(text$) -> renders (?) and shows tooltip on hover
BasicValue gui_helpmarker(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", to_string(args[0]).c_str());
    }
    return false;
}

// GUI.INPUT_INT(label$, val) -> new_val
BasicValue gui_input_int(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 2) return 0.0;
    std::string label = to_string(args[0]);
    int val = (int)to_double(args[1]);
    if (ImGui::InputInt(label.c_str(), &val)) {
        return (double)val;
    }
    return (double)val;
}

// GUI.INPUT_DOUBLE(label$, val) -> new_val
BasicValue gui_input_double(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() != 2) return 0.0;
    std::string label = to_string(args[0]);
    double val = to_double(args[1]);
    if (ImGui::InputDouble(label.c_str(), &val)) {
        return val;
    }
    return val;
}

// GUI.SEPARATOR_TEXT(text$)
BasicValue gui_separator_text(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;
    ImGui::SeparatorText(to_string(args[0]).c_str());
    return false;
}

// GUI.SHOW_FONT_ATLAS
BasicValue gui_show_font_atlas(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::ShowFontSelector("Font Atlas");
    return false;
}

// GUI.PLOT_LINES(label$, array_values, [overlay_text], [scale_min], [scale_max])
BasicValue gui_plot_lines(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() < 2) return false;
    std::string label = to_string(args[0]);

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) return false;
    auto arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!arr_ptr) return false;

    std::vector<float> values;
    for (const auto& v : arr_ptr->data) values.push_back((float)to_double(v));

    const char* overlay = (args.size() > 2) ? to_string(args[2]).c_str() : NULL;
    float min_scale = (args.size() > 3) ? (float)to_double(args[3]) : FLT_MAX;
    float max_scale = (args.size() > 4) ? (float)to_double(args[4]) : FLT_MAX;

    ImGui::PlotLines(label.c_str(), values.data(), (int)values.size(), 0, overlay, min_scale, max_scale, ImVec2(0, 80.0f));
    return false;
}

// GUI.PLOT_HISTOGRAM(label$, array_values, [overlay_text], [scale_min], [scale_max])
BasicValue gui_plot_histogram(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.size() < 2) return false;
    std::string label = to_string(args[0]);

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) return false;
    auto arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!arr_ptr) return false;

    std::vector<float> values;
    for (const auto& v : arr_ptr->data) values.push_back((float)to_double(v));

    const char* overlay = (args.size() > 2) ? to_string(args[2]).c_str() : NULL;
    float min_scale = (args.size() > 3) ? (float)to_double(args[3]) : FLT_MAX;
    float max_scale = (args.size() > 4) ? (float)to_double(args[4]) : FLT_MAX;

    ImGui::PlotHistogram(label.c_str(), values.data(), (int)values.size(), 0, overlay, min_scale, max_scale, ImVec2(0, 80.0f));
    return false;
}

// GUI.COLLAPSING_HEADER(label$, [visible_bool]) -> is_open
BasicValue gui_collapsing_header(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;
    std::string label = to_string(args[0]);
    bool visible = true;
    if (args.size() > 1) visible = to_bool(args[1]); // Not strictly used by CollapsingHeader in standard API for close button, but for default open maybe?
    // Actually standard CollapsingHeader doesn't take a bool* for closing, just flags.
    // We'll stick to simple usage.
    return ImGui::CollapsingHeader(label.c_str());
}

// GUI.TREE_NODE(label$) -> is_open. MUST CALL GUI.TREE_POP if true!
BasicValue gui_tree_node(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;
    return ImGui::TreeNode(to_string(args[0]).c_str());
}

// GUI.TREE_POP
BasicValue gui_tree_pop(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::TreePop();
    return false;
}

// GUI.OPEN_POPUP(str_id$)
BasicValue gui_open_popup(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) return false;
    ImGui::OpenPopup(to_string(args[0]).c_str());
    return false;
}

// GUI.BEGIN_POPUP(str_id$) -> is_open. Must call GUI.END_POPUP
BasicValue gui_begin_popup(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;
    return ImGui::BeginPopup(to_string(args[0]).c_str());
}

// GUI.BEGIN_POPUP_MODAL(name$, [p_open]) -> is_open
BasicValue gui_begin_popup_modal(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;
    std::string name = to_string(args[0]);
    bool* p_open = nullptr;
    bool is_open = true; // Placeholder if passed
    if (args.size() > 1) {
        is_open = to_bool(args[1]);
        p_open = &is_open;
    }

    if (ImGui::BeginPopupModal(name.c_str(), p_open)) {
        // We can't return the modified p_open easily AND the begin state. 
        // We prioritize the begin state. The user has to handle close button logic via buttons usually.
        return true;
    }
    return false;
}

// GUI.END_POPUP
BasicValue gui_end_popup(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::EndPopup();
    return false;
}

// GUI.CLOSE_CURRENT_POPUP
BasicValue gui_close_current_popup(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::CloseCurrentPopup();
    return false;
}

// GUI.BEGIN_MENU_BAR (Window-local menu bar)
BasicValue gui_begin_menu_bar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    return ImGui::BeginMenuBar();
}

// GUI.BEGIN_MAIN_MENU_BAR (Full-screen top menu bar)
BasicValue gui_begin_main_menu_bar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    return ImGui::BeginMainMenuBar();
}

// GUI.END_MENU_BAR (Used for both)
BasicValue gui_end_menu_bar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // ImGui::EndMenuBar() is used for window-local menu bars.
    // ImGui::EndMainMenuBar() is used for the main menu bar.
    // However, ImGui::EndMenuBar() works for both if the context is correct in newer versions?
    // Actually, EndMainMenuBar is just a wrapper for EndMenuBar + End.
    // Let's check typical usage.
    // If user called BeginMainMenuBar, they should call EndMainMenuBar.
    // We can infer which one based on internal stack? No.
    // Safest to just expose GUI.END_MAIN_MENU_BAR separately or use context.

    // Actually, ImGui::BeginMainMenuBar() internally calls Begin() with flags.
    // So ImGui::EndMainMenuBar() calls End().
    // ImGui::BeginMenuBar() is for inside a window.

    // To simplify: We will have GUI.END_MENU_BAR map to ImGui::EndMenuBar().
    // And GUI.END_MAIN_MENU_BAR map to ImGui::EndMainMenuBar().
    ImGui::EndMenuBar();
    return false;
}

BasicValue gui_end_main_menu_bar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::EndMainMenuBar();
    return false;
}

// GUI.BEGIN_MENU(label$, [enabled]) -> is_open
BasicValue gui_begin_menu(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) return false;
    bool enabled = (args.size() > 1) ? to_bool(args[1]) : true;
    return ImGui::BeginMenu(to_string(args[0]).c_str(), enabled);
}

// GUI.END_MENU
BasicValue gui_end_menu(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::EndMenu();
    return false;
}

// GUI.MENU_ITEM(label$, [shortcut$], [selected], [enabled]) -> activated
BasicValue gui_menu_item(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) return false;
    std::string label = to_string(args[0]);
    const char* shortcut = (args.size() > 1) ? to_string(args[1]).c_str() : NULL;
    bool selected = (args.size() > 2) ? to_bool(args[2]) : false;
    bool enabled = (args.size() > 3) ? to_bool(args[3]) : true;
    return ImGui::MenuItem(label.c_str(), shortcut, selected, enabled);
}
// GUI.BEGIN_CHILD(id$, [width, height], [border_bool], [flags])
BasicValue gui_begin_child(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string str_id = to_string(args[0]);
    float w = 0.0f;
    float h = 0.0f;
    if (args.size() > 1) w = (float)to_double(args[1]);
    if (args.size() > 2) h = (float)to_double(args[2]);

    // ImGuiChildFlags handling
    ImGuiChildFlags child_flags = ImGuiChildFlags_None;
    if (args.size() > 3 && to_bool(args[3])) {
        child_flags |= ImGuiChildFlags_Borders;
    }

    ImGuiWindowFlags window_flags = 0;
    if (args.size() > 4) window_flags = (ImGuiWindowFlags)to_double(args[4]);

    return ImGui::BeginChild(str_id.c_str(), ImVec2(w, h), child_flags, window_flags);
}

// GUI.END_CHILD
BasicValue gui_end_child(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::EndChild();
    return false;
}

// GUI.SELECTABLE(label$, [selected_bool], [flags], [width, height]) -> selected
BasicValue gui_selectable(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string label = to_string(args[0]);
    bool selected = (args.size() > 1) ? to_bool(args[1]) : false;
    ImGuiSelectableFlags flags = (args.size() > 2) ? (ImGuiSelectableFlags)to_double(args[2]) : 0;

    float w = 0.0f;
    float h = 0.0f;
    if (args.size() > 3) w = (float)to_double(args[3]);
    if (args.size() > 4) h = (float)to_double(args[4]);

    // Use a special span flag to make the whole row clickable if desired, mimicking table row selection
    flags |= ImGuiSelectableFlags_SpanAllColumns;

    if (ImGui::Selectable(label.c_str(), selected, flags, ImVec2(w, h))) {
        return true;
    }
    return false;
}

// GUI.PUSH_ID(int_or_string)
BasicValue gui_push_id(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) return false;

    if (std::holds_alternative<std::string>(args[0])) {
        ImGui::PushID(std::get<std::string>(args[0]).c_str());
    }
    else {
        ImGui::PushID((int)to_double(args[0]));
    }
    return false;
}

// GUI.POP_ID
BasicValue gui_pop_id(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::PopID();
    return false;
}

BasicValue gui_begin_tab_bar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string str_id = to_string(args[0]);
    ImGuiTabBarFlags flags = 0;
    if (args.size() > 1) {
        // You might want to expose tab bar flags similarly to window flags if needed
        // For now, let's assume it takes an integer if provided
        flags = (ImGuiTabBarFlags)to_double(args[1]);
    }

    return ImGui::BeginTabBar(str_id.c_str(), flags);
}

// GUI.END_TAB_BAR
BasicValue gui_end_tab_bar(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::EndTabBar();
    return false;
}

// GUI.BEGIN_TAB_ITEM(label$, [p_open], [flags]) -> boolean (is_selected)
BasicValue gui_begin_tab_item(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string label = to_string(args[0]);
    bool* p_open = nullptr;
    bool is_open = true; // Placeholder
    ImGuiTabItemFlags flags = 0;

    // Check for p_open argument (2nd arg)
    if (args.size() > 1) {
        // p_open isnt used yet!
    }

    if (args.size() > 2) {
        flags = (ImGuiTabItemFlags)to_double(args[2]);
    }

    // For now, simplest binding:
    return ImGui::BeginTabItem(label.c_str(), nullptr, flags);
}

// GUI.END_TAB_ITEM
BasicValue gui_end_tab_item(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    ImGui::EndTabItem();
    return false;
}


// Registration Function
void register_imgui_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
    auto reg = [&](std::string name, int arity, NeReLaBasic::NativeFunction fn) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = fn;
        table[name] = info;
        };

    auto reg_proc = [&](std::string name, int arity, NeReLaBasic::NativeFunction fn) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = fn;
        info.is_procedure = true;
        table[name] = info;
        };

    reg("GUI.BEGIN", -1, gui_begin);
    reg_proc("GUI.END", 0, gui_end);
    reg_proc("GUI.TEXT", 1, gui_text);
    reg_proc("GUI.SAME_LINE", 0, gui_sameline);
    reg_proc("GUI.SEPARATOR", 0, gui_separator);

    reg("GUI.BUTTON", -1, gui_button);
    reg("GUI.INPUT", 2, gui_input_text);
    reg("GUI.SLIDER", 4, gui_slider);
    reg("GUI.CHECKBOX", 2, gui_checkbox);

    // New Registrations
    reg("GUI.RADIO", 3, gui_radio);
    reg("GUI.COMBO", 3, gui_combo);
    reg("GUI.LISTBOX", -1, gui_listbox);
    reg_proc("GUI.PROGRESS", -1, gui_progressbar);
    reg_proc("GUI.DUMMY", 2, gui_dummy);
    reg("GUI.COLOR", 2, gui_color);

    // Latest Additions
    reg("GUI.FLAG", 1, gui_flag);

    reg("GUI.COL", 1, gui_col);
    reg_proc("GUI.PUSH_STYLE_COLOR", 2, gui_push_style_color);
    reg_proc("GUI.POP_STYLE_COLOR", -1, gui_pop_style_color);

    reg_proc("GUI.THEME", 1, gui_theme);
    reg_proc("GUI.TOOLTIP", 1, gui_tooltip);
    reg_proc("GUI.HELPMARKER", 1, gui_helpmarker);
    reg("GUI.INPUT_INT", 2, gui_input_int);
    reg("GUI.INPUT_DOUBLE", 2, gui_input_double);
    reg_proc("GUI.SEPARATOR_TEXT", 1, gui_separator_text);
    reg_proc("GUI.SHOW_FONT_ATLAS", 0, gui_show_font_atlas);
    reg_proc("GUI.PLOT_LINES", -1, gui_plot_lines);
    reg_proc("GUI.PLOT_HISTOGRAM", -1, gui_plot_histogram);
    reg("GUI.COLLAPSING_HEADER", -1, gui_collapsing_header);
    reg("GUI.TREE_NODE", 1, gui_tree_node);
    reg_proc("GUI.TREE_POP", 0, gui_tree_pop);
    reg_proc("GUI.OPEN_POPUP", 1, gui_open_popup);
    reg("GUI.BEGIN_POPUP", 1, gui_begin_popup);
    reg("GUI.BEGIN_POPUP_MODAL", -1, gui_begin_popup_modal);
    reg_proc("GUI.END_POPUP", 0, gui_end_popup);
    reg_proc("GUI.CLOSE_CURRENT_POPUP", 0, gui_close_current_popup);

    reg("GUI.BEGIN_MENU_BAR", 0, gui_begin_menu_bar); // Window-local
    reg("GUI.BEGIN_MAIN_MENU_BAR", 0, gui_begin_main_menu_bar); // Top of screen

    reg_proc("GUI.END_MENU_BAR", 0, gui_end_menu_bar);
    reg_proc("GUI.END_MAIN_MENU_BAR", 0, gui_end_main_menu_bar);

    reg("GUI.BEGIN_MENU", -1, gui_begin_menu);
    reg_proc("GUI.END_MENU", 0, gui_end_menu);
    reg("GUI.MENU_ITEM", -1, gui_menu_item);

    reg("GUI.BEGIN_CHILD", -1, gui_begin_child);
    reg_proc("GUI.END_CHILD", 0, gui_end_child);
    reg("GUI.SELECTABLE", -1, gui_selectable);
    reg_proc("GUI.PUSH_ID", 1, gui_push_id);
    reg_proc("GUI.POP_ID", 0, gui_pop_id);

    // Tab Bar helpers
    reg("GUI.BEGIN_TAB_BAR", -1, gui_begin_tab_bar);
    reg_proc("GUI.END_TAB_BAR", 0, gui_end_tab_bar);
    reg("GUI.BEGIN_TAB_ITEM", -1, gui_begin_tab_item);
    reg_proc("GUI.END_TAB_ITEM", 0, gui_end_tab_item);
}

#endif // JD_IMGUI