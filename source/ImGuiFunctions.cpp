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

// GUI.BEGIN(title$, [x, y, w, h], [p_open]) -> new_p_open_state (boolean)
// If p_open is NOT provided, no close button is shown.
// If p_open IS provided, the function returns the new state of p_open.
BasicValue gui_begin(NeReLaBasic & vm, const std::vector<BasicValue>&args) {
    CHECK_GUI_INIT(vm);
    if (args.empty()) return false;

    std::string title = to_string(args[0]);
    bool has_pos_size = (args.size() >= 5);
    bool has_close_flag = (args.size() == 2 || args.size() == 6); // Either (title, open) or (title, x,y,w,h, open)

    if (has_pos_size) {
        float x = (float)to_double(args[1]);
        float y = (float)to_double(args[2]);
        float w = (float)to_double(args[3]);
        float h = (float)to_double(args[4]);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    }

    bool is_open = true;
    bool* p_open = nullptr;

    // Check if the last argument is the boolean 'open' state
    if (has_close_flag) {
        // The last argument is the open state
        is_open = to_bool(args.back());
        p_open = &is_open;
    }

    // We don't use the return value of Begin() for the BASIC return value here,
    // because we need to return the state of the CLOSE BUTTON (p_open).
    // The user must still call GUI.END() regardless (ImGui rule: Begin/End must match).

    // However, to be safe and allow "IF GUI.BEGIN(...) THEN ... ENDIF", we need to know if it's collapsed.
    // But since we can only return ONE value, and pass-by-reference isn't simple here,
    // we prioritize returning the OPEN STATE if a close button was requested.

    bool expanded = ImGui::Begin(title.c_str(), p_open);

    if (has_close_flag) {
        // Return the potentially modified open state (false if X was clicked)
        return is_open;
    }
    else {
        // Legacy behavior: return true if expanded, false if collapsed
        return expanded;
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

    reg("GUI.BEGIN", -1, gui_begin); ;
    reg_proc("GUI.END", 0, gui_end);
    reg_proc("GUI.TEXT", 1, gui_text);
    reg_proc("GUI.SAME_LINE", 0, gui_sameline);
    reg_proc("GUI.SEPARATOR", 0, gui_separator);

    reg("GUI.BUTTON", -1, gui_button);
    reg("GUI.INPUT", 2, gui_input_text);
    reg("GUI.SLIDER", 4, gui_slider);
    reg("GUI.CHECKBOX", 2, gui_checkbox);
}

#endif // JD_IMGUI