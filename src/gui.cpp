#ifdef IMGUI

#include "gui.h"
#include "vm.h"
#include "errors.h"
#include "sprites.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <cfloat>

// ── ImGui state ────────────────────────────────────────────────

static bool g_imgui_init = false;
static SDL_Renderer* g_renderer = nullptr;

// ── Begin/End nesting counters ────────────────────────────────
// ImGui crashes hard if you call TableNextRow / TableNextColumn / EndTable
// outside an active table, or pass mismatched Begin*/End* pairs. The
// natives below use these counters so the user's BASIC code can be sloppy
// without taking down the interpreter - calls outside the proper scope
// are silently ignored, and `End*` only fires when there's something to
// end. Reset to 0 at the start of every new frame so a previous frame
// that bailed out mid-loop doesn't leak counters.
static int g_window_depth = 0;       // Begin / End  (always paired, end always)
static int g_child_depth = 0;        // BeginChild / EndChild (always paired)
static int g_main_menu_bar_depth = 0; // BeginMainMenuBar / EndMainMenuBar
static int g_table_depth = 0;
static int g_tab_bar_depth = 0;
static int g_tab_item_depth = 0;
static int g_menu_bar_depth = 0;
static int g_menu_depth = 0;
static int g_popup_depth = 0;

static void gui_reset_begin_end_counters() {
    g_window_depth = 0;
    g_child_depth = 0;
    g_main_menu_bar_depth = 0;
    g_table_depth = 0;
    g_tab_bar_depth = 0;
    g_tab_item_depth = 0;
    g_menu_bar_depth = 0;
    g_menu_depth = 0;
    g_popup_depth = 0;
}

// Walk back any unclosed Begin* scopes from a sloppy frame so the ImGui
// stack ends balanced. Called right before gui_render() - without this,
// a user who forgets `END_TABLE` would crash the next frame because ImGui
// asserts on a non-empty Begin/End stack at NewFrame time. Order matters:
// the most-nested scope first.
static void gui_balance_begin_end_stack() {
    // Inner-most: table cells / tab items
    while (g_tab_item_depth > 0)    { ImGui::EndTabItem();    g_tab_item_depth--; }
    while (g_table_depth > 0)       { ImGui::EndTable();      g_table_depth--; }
    while (g_tab_bar_depth > 0)     { ImGui::EndTabBar();     g_tab_bar_depth--; }
    while (g_menu_depth > 0)        { ImGui::EndMenu();       g_menu_depth--; }
    while (g_menu_bar_depth > 0)    { ImGui::EndMenuBar();    g_menu_bar_depth--; }
    while (g_popup_depth > 0)       { ImGui::EndPopup();      g_popup_depth--; }
    while (g_main_menu_bar_depth > 0) { ImGui::EndMainMenuBar(); g_main_menu_bar_depth--; }
    while (g_child_depth > 0)       { ImGui::EndChild();      g_child_depth--; }
    while (g_window_depth > 0)      { ImGui::End();           g_window_depth--; }
}

// ── Init / Shutdown / Frame ────────────────────────────────────

void gui_init(SDL_Window* window, SDL_Renderer* renderer, float scale) {
    if (g_imgui_init) return;
    // Reset begin/end counters in case a previous run left them set
    // (e.g. crashed mid-frame, then user re-ran the script).
    gui_reset_begin_end_counters();
    g_renderer = renderer;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Scale ImGui to match the window scaling factor
    if (scale > 1.0f) {
        ImGui::GetStyle().ScaleAllSizes(scale);
        io.FontGlobalScale = scale;
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    // Start the first frame so GUI.* calls work immediately
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();
    g_imgui_init = true;
}

void gui_shutdown() {
    if (!g_imgui_init) return;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_imgui_init = false;
}

static int g_logical_w = 0, g_logical_h = 0;

void gui_new_frame() {
    if (!g_imgui_init) return;

    // Restore logical presentation for BASIC drawing commands
    if (g_renderer && g_logical_w > 0) {
        SDL_SetRenderLogicalPresentation(g_renderer, g_logical_w, g_logical_h,
            SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }

    // Defensive: if a previous frame bailed out (exception, user error)
    // mid-table or mid-tab-bar, the depth counters could leak across the
    // frame boundary. Reset them so the new frame starts clean.
    gui_reset_begin_end_counters();

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();
}

void gui_render(SDL_Renderer* renderer) {
    if (!g_imgui_init) return;

    // Disable logical presentation so ImGui renders in raw window coordinates.
    // This avoids all mouse/coordinate mismatch issues with scaling.
    if (renderer) {
        int w, h;
        SDL_GetRenderLogicalPresentation(renderer, &w, &h, nullptr);
        if (w > 0 && h > 0) {
            g_logical_w = w;
            g_logical_h = h;
            SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                SDL_LOGICAL_PRESENTATION_DISABLED);
        }
    }

    // Auto-close any unclosed Begin scopes from sloppy user code so the
    // ImGui stack ends balanced before Render().
    gui_balance_begin_end_stack();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

bool gui_process_event(SDL_Event* ev) {
    if (!g_imgui_init) return false;
    // No coordinate transformation needed - ImGui works in native window coords
    return ImGui_ImplSDL3_ProcessEvent(ev);
}

// ── Helpers ────────────────────────────────────────────────────

static void ensure_imgui(const char* fn) {
    if (!g_imgui_init)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string(fn) + ": ImGui not initialized (call SCREEN first)");
}

// Persistent string buffers for InputText (keyed by label)
static std::unordered_map<std::string, std::string> g_input_buffers;
static constexpr size_t INPUT_BUF_SIZE = 4096;

// ── Flag/Color name lookup ─────────────────────────────────────

static int lookup_window_flag(const std::string& name) {
    static const std::unordered_map<std::string, int> flags = {
        {"NONE", 0},
        {"NOTITLEBAR", ImGuiWindowFlags_NoTitleBar},
        {"NORESIZE", ImGuiWindowFlags_NoResize},
        {"NOMOVE", ImGuiWindowFlags_NoMove},
        {"NOSCROLLBAR", ImGuiWindowFlags_NoScrollbar},
        {"NOSCROLLWITHMOUSE", ImGuiWindowFlags_NoScrollWithMouse},
        {"NOCOLLAPSE", ImGuiWindowFlags_NoCollapse},
        {"ALWAYSAUTORESIZE", ImGuiWindowFlags_AlwaysAutoResize},
        {"NOBACKGROUND", ImGuiWindowFlags_NoBackground},
        {"NOSAVEDSETTINGS", ImGuiWindowFlags_NoSavedSettings},
        {"NOMOUSEINPUTS", ImGuiWindowFlags_NoMouseInputs},
        {"MENUBAR", ImGuiWindowFlags_MenuBar},
        {"HORIZONTALSCROLLBAR", ImGuiWindowFlags_HorizontalScrollbar},
        {"NOFOCUSONAPPEARING", ImGuiWindowFlags_NoFocusOnAppearing},
        {"NOBRINGTOFRONTONFOCUS", ImGuiWindowFlags_NoBringToFrontOnFocus},
        {"ALWAYSVERTICALSCROLLBAR", ImGuiWindowFlags_AlwaysVerticalScrollbar},
        {"ALWAYSHORIZONTALSCROLLBAR", ImGuiWindowFlags_AlwaysHorizontalScrollbar},
        {"NONAVINPUTS", ImGuiWindowFlags_NoNavInputs},
        {"NONAVFOCUS", ImGuiWindowFlags_NoNavFocus},
        {"UNSAVEDDOCUMENT", ImGuiWindowFlags_UnsavedDocument},
        {"NONAV", ImGuiWindowFlags_NoNav},
        {"NODECORATION", ImGuiWindowFlags_NoDecoration},
        {"NOINPUTS", ImGuiWindowFlags_NoInputs},
    };
    auto it = flags.find(name);
    return (it != flags.end()) ? it->second : 0;
}

static int lookup_col(const std::string& name) {
    static const std::unordered_map<std::string, int> cols = {
        {"TEXT", ImGuiCol_Text},
        {"TEXTDISABLED", ImGuiCol_TextDisabled},
        {"WINDOWBG", ImGuiCol_WindowBg},
        {"CHILDBG", ImGuiCol_ChildBg},
        {"POPUPBG", ImGuiCol_PopupBg},
        {"BORDER", ImGuiCol_Border},
        {"BORDERSHADOW", ImGuiCol_BorderShadow},
        {"FRAMEBG", ImGuiCol_FrameBg},
        {"FRAMEBGHOVERED", ImGuiCol_FrameBgHovered},
        {"FRAMEBGACTIVE", ImGuiCol_FrameBgActive},
        {"TITLEBG", ImGuiCol_TitleBg},
        {"TITLEBGACTIVE", ImGuiCol_TitleBgActive},
        {"TITLEBGCOLLAPSED", ImGuiCol_TitleBgCollapsed},
        {"MENUBARBG", ImGuiCol_MenuBarBg},
        {"SCROLLBARBG", ImGuiCol_ScrollbarBg},
        {"SCROLLBARGRAB", ImGuiCol_ScrollbarGrab},
        {"SCROLLBARGRABHOVERED", ImGuiCol_ScrollbarGrabHovered},
        {"SCROLLBARGRABACTIVE", ImGuiCol_ScrollbarGrabActive},
        {"CHECKMARK", ImGuiCol_CheckMark},
        {"SLIDERGRAB", ImGuiCol_SliderGrab},
        {"SLIDERGRABACTIVE", ImGuiCol_SliderGrabActive},
        {"BUTTON", ImGuiCol_Button},
        {"BUTTONHOVERED", ImGuiCol_ButtonHovered},
        {"BUTTONACTIVE", ImGuiCol_ButtonActive},
        {"HEADER", ImGuiCol_Header},
        {"HEADERHOVERED", ImGuiCol_HeaderHovered},
        {"HEADERACTIVE", ImGuiCol_HeaderActive},
        {"SEPARATOR", ImGuiCol_Separator},
        {"SEPARATORHOVERED", ImGuiCol_SeparatorHovered},
        {"SEPARATORACTIVE", ImGuiCol_SeparatorActive},
        {"RESIZEGRIP", ImGuiCol_ResizeGrip},
        {"RESIZEGRIPHOVERED", ImGuiCol_ResizeGripHovered},
        {"RESIZEGRIPACTIVE", ImGuiCol_ResizeGripActive},
        {"TAB", ImGuiCol_Tab},
        {"TABHOVERED", ImGuiCol_TabHovered},
        {"TABSELECTED", ImGuiCol_TabSelected},
        {"TABDIMMED", ImGuiCol_TabDimmed},
        {"TABDIMMEDSELECTED", ImGuiCol_TabDimmedSelected},
        {"PLOTLINES", ImGuiCol_PlotLines},
        {"PLOTLINESHOVERED", ImGuiCol_PlotLinesHovered},
        {"PLOTHISTOGRAM", ImGuiCol_PlotHistogram},
        {"PLOTHISTOGRAMHOVERED", ImGuiCol_PlotHistogramHovered},
        {"TABLEHEADERBG", ImGuiCol_TableHeaderBg},
        {"TABLEBORDERSTRONG", ImGuiCol_TableBorderStrong},
        {"TABLEBORDERLIGHT", ImGuiCol_TableBorderLight},
        {"TABLEROWBG", ImGuiCol_TableRowBg},
        {"TABLEROWBGALT", ImGuiCol_TableRowBgAlt},
        {"TEXTSELECTEDBG", ImGuiCol_TextSelectedBg},
        {"DRAGDROPTARGET", ImGuiCol_DragDropTarget},
        {"NAVCURSOR", ImGuiCol_NavCursor},
        {"NAVWINDOWINGHIGHLIGHT", ImGuiCol_NavWindowingHighlight},
    };
    auto it = cols.find(name);
    return (it != cols.end()) ? it->second : -1;
}

// ── Register all GUI builtins ──────────────────────────────────

void register_gui_builtins(VM& vm) {

    // ── GUI.BEGIN(title$, [x, y, w, h], [p_open], [flags]) -> bool ──

    vm.register_native("GUI.BEGIN", 1, 8, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN");
        std::string title = args[0].as_string()->data;
        size_t idx = 1;

        // Optional position and size: x, y, w, h
        if (args.size() >= 5 && args[1].type != ValueType::BOOLEAN) {
            float x = (float)args[1].to_double();
            float y = (float)args[2].to_double();
            float w = (float)args[3].to_double();
            float h = (float)args[4].to_double();
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
            idx = 5;
        }

        // Optional p_open (bool)
        bool use_p_open = false;
        bool p_open_val = true;
        if (idx < args.size() && args[idx].type == ValueType::BOOLEAN) {
            use_p_open = true;
            p_open_val = args[idx].to_bool();
            idx++;
        }

        // Optional flags (int)
        ImGuiWindowFlags flags = 0;
        if (idx < args.size()) {
            flags = (ImGuiWindowFlags)args[idx].to_int();
        }

        bool result;
        if (use_p_open) {
            result = ImGui::Begin(title.c_str(), &p_open_val, flags);
        } else {
            result = ImGui::Begin(title.c_str(), nullptr, flags);
        }
        // Window: ImGui requires End() *regardless* of Begin()'s return.
        // Track depth so a stray END before any BEGIN doesn't blow up.
        g_window_depth++;
        return Value::make_bool(result);
    });

    vm.register_native("GUI.END", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END");
        if (g_window_depth > 0) {
            ImGui::End();
            g_window_depth--;
        }
        return Value::make_none();
    });

    // ── GUI.BEGIN_CHILD(id$, [width, height], [border], [flags]) ──

    vm.register_native("GUI.BEGIN_CHILD", 1, 5, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_CHILD");
        std::string id = args[0].as_string()->data;
        float w = (args.size() >= 2) ? (float)args[1].to_double() : 0.0f;
        float h = (args.size() >= 3) ? (float)args[2].to_double() : 0.0f;
        ImGuiChildFlags child_flags = 0;
        if (args.size() >= 4 && args[3].to_bool()) {
            child_flags |= ImGuiChildFlags_Borders;
        }
        ImGuiWindowFlags window_flags = 0;
        if (args.size() >= 5) {
            window_flags = (ImGuiWindowFlags)args[4].to_int();
        }
        bool r = ImGui::BeginChild(id.c_str(), ImVec2(w, h), child_flags, window_flags);
        // Child windows: same rule as Begin/End - EndChild() must be called
        // regardless of BeginChild's return value (since ImGui 1.81).
        g_child_depth++;
        return Value::make_bool(r);
    });

    vm.register_native("GUI.END_CHILD", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_CHILD");
        if (g_child_depth > 0) {
            ImGui::EndChild();
            g_child_depth--;
        }
        return Value::make_none();
    });

    // ── GUI.COLLAPSING_HEADER(label$, [visible_bool]) -> bool ──

    vm.register_native("GUI.COLLAPSING_HEADER", 1, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.COLLAPSING_HEADER");
        std::string label = args[0].as_string()->data;
        if (args.size() >= 2) {
            bool visible = args[1].to_bool();
            bool r = ImGui::CollapsingHeader(label.c_str(), &visible);
            return Value::make_bool(r);
        }
        return Value::make_bool(ImGui::CollapsingHeader(label.c_str()));
    });

    // ── GUI.TREE_NODE(label$) -> bool ──

    vm.register_native("GUI.TREE_NODE", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TREE_NODE");
        std::string label = args[0].as_string()->data;
        return Value::make_bool(ImGui::TreeNode(label.c_str()));
    });

    vm.register_native("GUI.TREE_POP", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.TREE_POP");
        ImGui::TreePop();
        return Value::make_none();
    });

    // ── Layout ──────────────────────────────────────────────────

    vm.register_native("GUI.SAME_LINE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.SAME_LINE");
        ImGui::SameLine();
        return Value::make_none();
    });

    vm.register_native("GUI.SEPARATOR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.SEPARATOR");
        ImGui::Separator();
        return Value::make_none();
    });

    vm.register_native("GUI.SEPARATOR_TEXT", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.SEPARATOR_TEXT");
        std::string text = args[0].as_string()->data;
        ImGui::SeparatorText(text.c_str());
        return Value::make_none();
    });

    vm.register_native("GUI.DUMMY", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.DUMMY");
        float w = (float)args[0].to_double();
        float h = (float)args[1].to_double();
        ImGui::Dummy(ImVec2(w, h));
        return Value::make_none();
    });

    // ── Layout-Abfragen für dynamische Größen ──────────────────
    // Verfügbarer Platz im aktuellen Window/Child (nach bisherigen Widgets):

    vm.register_native("GUI.AVAIL_WIDTH", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.AVAIL_WIDTH");
        return Value::make_f64(ImGui::GetContentRegionAvail().x);
    });

    vm.register_native("GUI.AVAIL_HEIGHT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.AVAIL_HEIGHT");
        return Value::make_f64(ImGui::GetContentRegionAvail().y);
    });

    // Vollständige Größe des aktuellen Windows (inklusive Title/Padding):

    vm.register_native("GUI.WINDOW_WIDTH", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.WINDOW_WIDTH");
        return Value::make_f64(ImGui::GetWindowWidth());
    });

    vm.register_native("GUI.WINDOW_HEIGHT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.WINDOW_HEIGHT");
        return Value::make_f64(ImGui::GetWindowHeight());
    });

    // ── Basic Widgets ───────────────────────────────────────────

    vm.register_native("GUI.TEXT", 1, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TEXT");
        std::string text = args[0].as_string()->data;
        bool wrap = (args.size() >= 2) && args[1].to_bool();
        if (wrap) {
            // Wrap-Position = rechter Rand des aktuellen Content-Bereichs
            ImGui::PushTextWrapPos(0.0f); // 0 = bis zum rechten Rand des Windows/Child
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextUnformatted(text.c_str());
        }
        return Value::make_none();
    });

    // Convenience: immer mit Zeilenumbruch
    vm.register_native("GUI.TEXT_WRAPPED", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TEXT_WRAPPED");
        std::string text = args[0].as_string()->data;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        return Value::make_none();
    });

    vm.register_native("GUI.BUTTON", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BUTTON");
        std::string label = args[0].as_string()->data;
        float w = (args.size() >= 2) ? (float)args[1].to_double() : 0.0f;
        float h = (args.size() >= 3) ? (float)args[2].to_double() : 0.0f;
        return Value::make_bool(ImGui::Button(label.c_str(), ImVec2(w, h)));
    });

    vm.register_native("GUI.CHECKBOX", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.CHECKBOX");
        std::string label = args[0].as_string()->data;
        bool checked = args[1].to_bool();
        ImGui::Checkbox(label.c_str(), &checked);
        return Value::make_bool(checked);
    });

    vm.register_native("GUI.RADIO", 3, 3, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.RADIO");
        std::string label = args[0].as_string()->data;
        int current = (int)args[1].to_int();
        int button_val = (int)args[2].to_int();
        if (ImGui::RadioButton(label.c_str(), current == button_val)) {
            current = button_val;
        }
        return Value::make_i64(current);
    });

    vm.register_native("GUI.SLIDER", 4, 4, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.SLIDER");
        std::string label = args[0].as_string()->data;
        float v = (float)args[1].to_double();
        float vmin = (float)args[2].to_double();
        float vmax = (float)args[3].to_double();
        ImGui::SliderFloat(label.c_str(), &v, vmin, vmax);
        return Value::make_f64((double)v);
    });

    vm.register_native("GUI.PROGRESS", 1, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.PROGRESS");
        float fraction = (float)args[0].to_double();
        const char* overlay = nullptr;
        std::string overlay_str;
        if (args.size() >= 2 && args[1].type == ValueType::STRING) {
            overlay_str = args[1].as_string()->data;
            overlay = overlay_str.c_str();
        }
        ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), overlay);
        return Value::make_none();
    });

    // GUI.IMAGE(sprite_id, [w], [h]) -> bool
    // Draws a SPRITE.LOAD'd texture inside the current ImGui window. Defaults
    // to the texture's native pixel size. Returns FALSE for an unknown id.
    vm.register_native("GUI.IMAGE", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.IMAGE");
        int tw = 0, th = 0;
        SDL_Texture* tex = sprite_texture((int)args[0].to_int(), &tw, &th);
        if (!tex) return Value::make_bool(false);
        float w = (args.size() >= 2) ? (float)args[1].to_double() : (float)tw;
        float h = (args.size() >= 3) ? (float)args[2].to_double() : (float)th;
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, h));
        return Value::make_bool(true);
    });

    // GUI.COLOR(label$, color_array) -> bool
    // color_array is [r,g,b,a] with 0-255 values, modified in place

    vm.register_native("GUI.COLOR", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.COLOR");
        std::string label = args[0].as_string()->data;
        auto* arr = args[1].as_array();
        if (arr->elements.size() < 4)
            throw jdError(ErrCode::RUNTIME_ERROR, "GUI.COLOR: color array must have 4 elements [r,g,b,a]");

        float col[4] = {
            (float)(arr->elements[0].to_double() / 255.0),
            (float)(arr->elements[1].to_double() / 255.0),
            (float)(arr->elements[2].to_double() / 255.0),
            (float)(arr->elements[3].to_double() / 255.0)
        };

        bool changed = ImGui::ColorEdit4(label.c_str(), col);
        if (changed) {
            arr->elements[0] = Value::make_f64(col[0] * 255.0);
            arr->elements[1] = Value::make_f64(col[1] * 255.0);
            arr->elements[2] = Value::make_f64(col[2] * 255.0);
            arr->elements[3] = Value::make_f64(col[3] * 255.0);
        }
        return Value::make_bool(changed);
    });

    vm.register_native("GUI.HELPMARKER", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.HELPMARKER");
        std::string text = args[0].as_string()->data;
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        return Value::make_none();
    });

    vm.register_native("GUI.TOOLTIP", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TOOLTIP");
        std::string text = args[0].as_string()->data;
        ImGui::SetTooltip("%s", text.c_str());
        return Value::make_none();
    });

    // ── Input Widgets ───────────────────────────────────────────

    vm.register_native("GUI.INPUT", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.INPUT");
        std::string label = args[0].as_string()->data;
        std::string current = args[1].as_string()->data;

        // Use persistent buffer keyed by label
        auto& buf = g_input_buffers[label];
        buf = current;
        buf.resize(INPUT_BUF_SIZE, '\0');

        // `##`-prefixed labels are hidden - fill the cell so the field
        // doesn't collapse to 0px. Visible labels keep ImGui's default.
        bool hidden_label = (label.size() >= 2 && label[0] == '#' && label[1] == '#');
        if (hidden_label) {
            float avail_w = ImGui::GetContentRegionAvail().x;
            if (avail_w > 1.0f) ImGui::SetNextItemWidth(avail_w);
        }
        if (ImGui::InputText(label.c_str(), buf.data(), INPUT_BUF_SIZE,
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            // Enter pressed - return current content
        }
        // Always return current content (may have been edited)
        return Value::make_string(std::string(buf.c_str()));
    });

    vm.register_native("GUI.INPUT_INT", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.INPUT_INT");
        std::string label = args[0].as_string()->data;
        int v = (int)args[1].to_int();
        // `##` label: drop step buttons and fill the cell; visible label
        // keeps the default input + buttons + label-on-right layout.
        bool hidden_label = (label.size() >= 2 && label[0] == '#' && label[1] == '#');
        if (hidden_label) {
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > 1.0f) ImGui::SetNextItemWidth(avail);
            ImGui::InputInt(label.c_str(), &v, 0, 0);
        } else {
            ImGui::InputInt(label.c_str(), &v);
        }
        return Value::make_i64(v);
    });

    vm.register_native("GUI.INPUT_DOUBLE", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.INPUT_DOUBLE");
        std::string label = args[0].as_string()->data;
        double v = args[1].to_double();
        bool hidden_label = (label.size() >= 2 && label[0] == '#' && label[1] == '#');
        if (hidden_label) {
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > 1.0f) ImGui::SetNextItemWidth(avail);
            ImGui::InputDouble(label.c_str(), &v, 0.0, 0.0);
        } else {
            ImGui::InputDouble(label.c_str(), &v);
        }
        return Value::make_f64(v);
    });

    vm.register_native("GUI.COMBO", 3, 3, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.COMBO");
        std::string label = args[0].as_string()->data;
        int current = (int)args[1].to_int();
        auto* items_arr = args[2].as_array();

        // Build items string list
        std::vector<std::string> item_strs;
        std::vector<const char*> item_ptrs;
        for (auto& e : items_arr->elements) {
            item_strs.push_back(e.to_string());
        }
        for (auto& s : item_strs) {
            item_ptrs.push_back(s.c_str());
        }

        // Use BeginCombo/EndCombo for full control
        std::string preview = (current >= 0 && current < (int)item_strs.size())
            ? item_strs[current] : "";

        if (ImGui::BeginCombo(label.c_str(), preview.c_str())) {
            for (int i = 0; i < (int)item_strs.size(); i++) {
                bool selected = (i == current);
                if (ImGui::Selectable(item_strs[i].c_str(), selected)) {
                    current = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return Value::make_i64(current);
    });

    vm.register_native("GUI.LISTBOX", 3, 4, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.LISTBOX");
        std::string label = args[0].as_string()->data;
        int current = (int)args[1].to_int();
        auto* items_arr = args[2].as_array();
        int height = (args.size() >= 4) ? (int)args[3].to_int() : -1;

        std::vector<std::string> item_strs;
        std::vector<const char*> item_ptrs;
        for (auto& e : items_arr->elements) {
            item_strs.push_back(e.to_string());
        }
        for (auto& s : item_strs) {
            item_ptrs.push_back(s.c_str());
        }

        ImGui::ListBox(label.c_str(), &current, item_ptrs.data(),
                       (int)item_ptrs.size(), height);
        return Value::make_i64(current);
    });

    vm.register_native("GUI.SELECTABLE", 1, 5, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.SELECTABLE");
        std::string label = args[0].as_string()->data;
        bool selected = (args.size() >= 2) ? args[1].to_bool() : false;
        ImGuiSelectableFlags flags = (args.size() >= 3) ? (ImGuiSelectableFlags)args[2].to_int() : 0;
        float w = (args.size() >= 4) ? (float)args[3].to_double() : 0.0f;
        float h = (args.size() >= 5) ? (float)args[4].to_double() : 0.0f;
        bool clicked = ImGui::Selectable(label.c_str(), selected, flags, ImVec2(w, h));
        return Value::make_bool(clicked);
    });

    // ── Menus ───────────────────────────────────────────────────

    vm.register_native("GUI.BEGIN_MAIN_MENU_BAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.BEGIN_MAIN_MENU_BAR");
        bool ok = ImGui::BeginMainMenuBar();
        if (ok) g_main_menu_bar_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_MAIN_MENU_BAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_MAIN_MENU_BAR");
        if (g_main_menu_bar_depth > 0) {
            ImGui::EndMainMenuBar();
            g_main_menu_bar_depth--;
        }
        return Value::make_none();
    });

    vm.register_native("GUI.BEGIN_MENU_BAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.BEGIN_MENU_BAR");
        bool ok = ImGui::BeginMenuBar();
        if (ok) g_menu_bar_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_MENU_BAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_MENU_BAR");
        if (g_menu_bar_depth > 0) {
            ImGui::EndMenuBar();
            g_menu_bar_depth--;
        }
        return Value::make_none();
    });

    vm.register_native("GUI.BEGIN_MENU", 1, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_MENU");
        std::string label = args[0].as_string()->data;
        bool enabled = (args.size() >= 2) ? args[1].to_bool() : true;
        bool ok = ImGui::BeginMenu(label.c_str(), enabled);
        if (ok) g_menu_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_MENU", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_MENU");
        if (g_menu_depth > 0) {
            ImGui::EndMenu();
            g_menu_depth--;
        }
        return Value::make_none();
    });

    vm.register_native("GUI.MENU_ITEM", 1, 4, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.MENU_ITEM");
        std::string label = args[0].as_string()->data;
        const char* shortcut = nullptr;
        std::string shortcut_str;
        if (args.size() >= 2 && args[1].type == ValueType::STRING) {
            shortcut_str = args[1].as_string()->data;
            shortcut = shortcut_str.c_str();
        }
        bool selected = (args.size() >= 3) ? args[2].to_bool() : false;
        bool enabled = (args.size() >= 4) ? args[3].to_bool() : true;
        return Value::make_bool(ImGui::MenuItem(label.c_str(), shortcut, selected, enabled));
    });

    // ── Popups & Modals ─────────────────────────────────────────

    vm.register_native("GUI.OPEN_POPUP", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.OPEN_POPUP");
        std::string str_id = args[0].as_string()->data;
        ImGui::OpenPopup(str_id.c_str());
        return Value::make_none();
    });

    vm.register_native("GUI.BEGIN_POPUP", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_POPUP");
        std::string str_id = args[0].as_string()->data;
        bool ok = ImGui::BeginPopup(str_id.c_str());
        if (ok) g_popup_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.BEGIN_POPUP_MODAL", 1, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_POPUP_MODAL");
        std::string name = args[0].as_string()->data;
        bool ok;
        if (args.size() >= 2) {
            bool p_open = args[1].to_bool();
            ok = ImGui::BeginPopupModal(name.c_str(), &p_open);
        } else {
            ok = ImGui::BeginPopupModal(name.c_str());
        }
        if (ok) g_popup_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_POPUP", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_POPUP");
        if (g_popup_depth > 0) {
            ImGui::EndPopup();
            g_popup_depth--;
        }
        return Value::make_none();
    });

    vm.register_native("GUI.CLOSE_CURRENT_POPUP", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.CLOSE_CURRENT_POPUP");
        if (g_popup_depth == 0) return Value::make_none();
        ImGui::CloseCurrentPopup();
        return Value::make_none();
    });

    // ── Tabs ────────────────────────────────────────────────────

    vm.register_native("GUI.BEGIN_TAB_BAR", 1, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_TAB_BAR");
        std::string str_id = args[0].as_string()->data;
        ImGuiTabBarFlags flags = (args.size() >= 2) ? (ImGuiTabBarFlags)args[1].to_int() : 0;
        bool ok = ImGui::BeginTabBar(str_id.c_str(), flags);
        if (ok) g_tab_bar_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_TAB_BAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_TAB_BAR");
        if (g_tab_bar_depth > 0) {
            ImGui::EndTabBar();
            g_tab_bar_depth--;
        }
        return Value::make_none();
    });

    vm.register_native("GUI.BEGIN_TAB_ITEM", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_TAB_ITEM");
        if (g_tab_bar_depth == 0) return Value::make_bool(false);
        std::string label = args[0].as_string()->data;
        bool* p_open_ptr = nullptr;
        bool p_open_val = true;
        if (args.size() >= 2 && args[1].type == ValueType::BOOLEAN) {
            p_open_val = args[1].to_bool();
            p_open_ptr = &p_open_val;
        }
        ImGuiTabItemFlags flags = (args.size() >= 3) ? (ImGuiTabItemFlags)args[2].to_int() : 0;
        bool ok = ImGui::BeginTabItem(label.c_str(), p_open_ptr, flags);
        if (ok) g_tab_item_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_TAB_ITEM", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_TAB_ITEM");
        if (g_tab_item_depth > 0) {
            ImGui::EndTabItem();
            g_tab_item_depth--;
        }
        return Value::make_none();
    });

    // ── Plots ───────────────────────────────────────────────────

    vm.register_native("GUI.PLOT_LINES", 2, 5, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.PLOT_LINES");
        std::string label = args[0].as_string()->data;
        auto* arr = args[1].as_array();

        std::vector<float> values;
        values.reserve(arr->elements.size());
        for (auto& e : arr->elements) {
            values.push_back((float)e.to_double());
        }

        const char* overlay = nullptr;
        std::string overlay_str;
        if (args.size() >= 3 && args[2].type == ValueType::STRING) {
            overlay_str = args[2].as_string()->data;
            overlay = overlay_str.c_str();
        }
        float scale_min = (args.size() >= 4) ? (float)args[3].to_double() : FLT_MAX;
        float scale_max = (args.size() >= 5) ? (float)args[4].to_double() : FLT_MAX;

        ImGui::PlotLines(label.c_str(), values.data(), (int)values.size(),
                         0, overlay, scale_min, scale_max, ImVec2(0, 80));
        return Value::make_none();
    });

    vm.register_native("GUI.PLOT_HISTOGRAM", 2, 5, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.PLOT_HISTOGRAM");
        std::string label = args[0].as_string()->data;
        auto* arr = args[1].as_array();

        std::vector<float> values;
        values.reserve(arr->elements.size());
        for (auto& e : arr->elements) {
            values.push_back((float)e.to_double());
        }

        const char* overlay = nullptr;
        std::string overlay_str;
        if (args.size() >= 3 && args[2].type == ValueType::STRING) {
            overlay_str = args[2].as_string()->data;
            overlay = overlay_str.c_str();
        }
        float scale_min = (args.size() >= 4) ? (float)args[3].to_double() : FLT_MAX;
        float scale_max = (args.size() >= 5) ? (float)args[4].to_double() : FLT_MAX;

        ImGui::PlotHistogram(label.c_str(), values.data(), (int)values.size(),
                             0, overlay, scale_min, scale_max, ImVec2(0, 80));
        return Value::make_none();
    });

    // ── Tables ──────────────────────────────────────────────────

    vm.register_native("GUI.BEGIN_TABLE", 2, 5, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.BEGIN_TABLE");
        std::string id = args[0].as_string()->data;
        int columns = (int)args[1].to_int();
        ImGuiTableFlags flags = (args.size() >= 3) ? (ImGuiTableFlags)args[2].to_int() : 0;
        float outer_w = (args.size() >= 4) ? (float)args[3].to_double() : 0.0f;
        float outer_h = (args.size() >= 5) ? (float)args[4].to_double() : 0.0f;
        bool ok = ImGui::BeginTable(id.c_str(), columns, flags, ImVec2(outer_w, outer_h));
        if (ok) g_table_depth++;
        return Value::make_bool(ok);
    });

    vm.register_native("GUI.END_TABLE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.END_TABLE");
        if (g_table_depth > 0) {
            ImGui::EndTable();
            g_table_depth--;
        }
        return Value::make_none();
    });

    vm.register_native("GUI.TABLE_SETUP_COLUMN", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TABLE_SETUP_COLUMN");
        if (g_table_depth == 0) return Value::make_none();
        std::string label = args[0].as_string()->data;
        ImGuiTableColumnFlags flags = (args.size() >= 2) ? (ImGuiTableColumnFlags)args[1].to_int() : 0;
        float init_width = (args.size() >= 3) ? (float)args[2].to_double() : 0.0f;
        ImGui::TableSetupColumn(label.c_str(), flags, init_width);
        return Value::make_none();
    });

    vm.register_native("GUI.TABLE_HEADERS_ROW", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.TABLE_HEADERS_ROW");
        if (g_table_depth == 0) return Value::make_none();
        ImGui::TableHeadersRow();
        return Value::make_none();
    });

    vm.register_native("GUI.TABLE_NEXT_ROW", 0, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TABLE_NEXT_ROW");
        if (g_table_depth == 0) return Value::make_none();
        ImGuiTableRowFlags row_flags = (args.size() >= 1) ? (ImGuiTableRowFlags)args[0].to_int() : 0;
        float min_row_height = (args.size() >= 2) ? (float)args[1].to_double() : 0.0f;
        ImGui::TableNextRow(row_flags, min_row_height);
        return Value::make_none();
    });

    vm.register_native("GUI.TABLE_SET_COLUMN_INDEX", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.TABLE_SET_COLUMN_INDEX");
        if (g_table_depth == 0) return Value::make_bool(false);
        int index = (int)args[0].to_int();
        return Value::make_bool(ImGui::TableSetColumnIndex(index));
    });

    vm.register_native("GUI.TABLE_NEXT_COLUMN", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.TABLE_NEXT_COLUMN");
        if (g_table_depth == 0) return Value::make_bool(false);
        return Value::make_bool(ImGui::TableNextColumn());
    });

    // ── Utilities & Styling ─────────────────────────────────────

    vm.register_native("GUI.THEME", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.THEME");
        std::string theme = args[0].as_string()->data;
        // Uppercase for comparison
        std::string upper = theme;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper == "DARK") ImGui::StyleColorsDark();
        else if (upper == "LIGHT") ImGui::StyleColorsLight();
        else if (upper == "CLASSIC") ImGui::StyleColorsClassic();
        else throw jdError(ErrCode::RUNTIME_ERROR,
            "GUI.THEME: unknown theme '" + theme + "' (use DARK, LIGHT, or CLASSIC)");
        return Value::make_none();
    });

    vm.register_native("GUI.FLAG", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        return Value::make_i64(lookup_window_flag(upper));
    });

    vm.register_native("GUI.COL", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        int col = lookup_col(upper);
        if (col < 0)
            throw jdError(ErrCode::RUNTIME_ERROR,
                "GUI.COL: unknown color '" + name + "'");
        return Value::make_i64(col);
    });

    vm.register_native("GUI.PUSH_STYLE_COLOR", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.PUSH_STYLE_COLOR");
        int idx = (int)args[0].to_int();
        auto* arr = args[1].as_array();
        if (!arr || arr->elements.size() < 4)
            throw jdError(ErrCode::RUNTIME_ERROR,
                "GUI.PUSH_STYLE_COLOR: color must be [r,g,b,a] array");
        float r = (float)arr->elements[0].to_double() / 255.0f;
        float g = (float)arr->elements[1].to_double() / 255.0f;
        float b = (float)arr->elements[2].to_double() / 255.0f;
        float a = (float)arr->elements[3].to_double() / 255.0f;
        ImGui::PushStyleColor(idx, ImVec4(r, g, b, a));
        return Value::make_none();
    });

    vm.register_native("GUI.POP_STYLE_COLOR", 0, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.POP_STYLE_COLOR");
        int count = args.empty() ? 1 : (int)args[0].to_int();
        ImGui::PopStyleColor(count);
        return Value::make_none();
    });

    vm.register_native("GUI.PUSH_ID", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.PUSH_ID");
        if (args[0].type == ValueType::STRING) {
            ImGui::PushID(args[0].as_string()->data.c_str());
        } else {
            ImGui::PushID((int)args[0].to_int());
        }
        return Value::make_none();
    });

    vm.register_native("GUI.POP_ID", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.POP_ID");
        ImGui::PopID();
        return Value::make_none();
    });

    vm.register_native("GUI.SHOW_FONT_ATLAS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.SHOW_FONT_ATLAS");
        ImGuiIO& io = ImGui::GetIO();
        ImFontAtlas* atlas = io.Fonts;
        if (atlas && atlas->IsBuilt()) {
            ImGui::ShowFontSelector("Fonts##FontSelector");
        }
        return Value::make_none();
    });

    vm.register_native("GUI.ITEM_RECT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.ITEM_RECT");
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        Value r = Value::make_array();
        auto* arr = r.as_array();
        arr->elements.push_back(Value::make_f64(mn.x));
        arr->elements.push_back(Value::make_f64(mn.y));
        arr->elements.push_back(Value::make_f64(mx.x));
        arr->elements.push_back(Value::make_f64(mx.y));
        return r;
    });

    vm.register_native("GUI.SET_CURSOR_SCREEN_POS", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.SET_CURSOR_SCREEN_POS");
        float x = (float)args[0].to_double();
        float y = (float)args[1].to_double();
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        return Value::make_none();
    });

    vm.register_native("GUI.SET_NEXT_ITEM_WIDTH", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_imgui("GUI.SET_NEXT_ITEM_WIDTH");
        float w = (float)args[0].to_double();
        ImGui::SetNextItemWidth(w);
        return Value::make_none();
    });

    vm.register_native("GUI.SET_KEYBOARD_FOCUS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.SET_KEYBOARD_FOCUS");
        ImGui::SetKeyboardFocusHere();
        return Value::make_none();
    });

    vm.register_native("GUI.ITEM_DEACTIVATED_AFTER_EDIT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_imgui("GUI.ITEM_DEACTIVATED_AFTER_EDIT");
        return Value::make_bool(ImGui::IsItemDeactivatedAfterEdit());
    });
}

#else // !GFX

#include "gui.h"
#include "vm.h"

void gui_init(SDL_Window*, SDL_Renderer*, float) {}
void gui_shutdown() {}
void gui_new_frame() {}
void gui_render(SDL_Renderer*) {}
bool gui_process_event(SDL_Event*) { return false; }
void register_gui_builtins(VM&) {}

#endif // IMGUI
