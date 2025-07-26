// whisper_integration.cpp
#include "whisper_integration.h"
#include "NeReLaBasic.hpp"
#include "Types.hpp"
#include "Error.hpp"

// This is the header from the whisper.cpp project.
// You would need to have this file and the corresponding library
// available in your build environment.
#include "whisper.h" 

// Include the main SDL header for audio capture
#include <SDL3/SDL.h>

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

// --- Global state for our Whisper integration ---
namespace {
    // Pointers to jdBasic's core functions, provided on module load.
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
    ToStringFunc g_to_string = nullptr;

    // --- State for audio recording and transcription ---
    std::atomic<bool> g_is_recording(false);
    std::thread g_recording_thread;
    std::vector<float> g_audio_buffer;
    std::mutex g_audio_mutex;
    std.string g_transcribed_text;
    SDL_AudioDeviceID g_audio_device_id = 0;

    // This function now contains a real audio capture loop using SDL.
    void audio_capture_thread_func(whisper_context* ctx) {
        // The audio device is already opened by WHISPER.START
        // This thread's job is to continuously read data from it.
        
        // Buffer to hold one chunk of audio data from SDL at a time.
        // Let's read in 1-second chunks (16000 samples * 4 bytes/sample).
        const size_t chunk_size_samples = 16000; 
        std::vector<float> sdl_chunk_buffer(chunk_size_samples);

        while (g_is_recording) {
            // Wait for and dequeue audio data from the recording device.
            // This function will block until it has enough data.
            if (SDL_DequeueAudioData(g_audio_device_id, sdl_chunk_buffer.data(), chunk_size_samples * sizeof(float)) > 0) {
                // Lock the mutex to safely append the captured chunk to our main buffer.
                std::lock_guard<std::mutex> lock(g_audio_mutex);
                g_audio_buffer.insert(g_audio_buffer.end(), sdl_chunk_buffer.begin(), sdl_chunk_buffer.end());
            }
        }

        // --- Once recording stops, process the audio ---
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        if (ctx && !g_audio_buffer.empty()) {
            whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            
            // Run the main whisper transcription function
            if (whisper_full(ctx, wparams, g_audio_buffer.data(), g_audio_buffer.size()) == 0) {
                const int n_segments = whisper_full_n_segments(ctx);
                std::string result_text;
                for (int i = 0; i < n_segments; ++i) {
                    const char* text = whisper_full_get_segment_text(ctx, i);
                    result_text += text;
                }
                g_transcribed_text = result_text;
            } else {
                 g_transcribed_text = "[Transcription Error]";
            }
        }
    }
}

// === BUILT-IN FUNCTION IMPLEMENTATIONS ===

// WHISPER.INIT$(model_path$) -> OpaqueHandle or FALSE
void builtin_whisper_init(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, "WHISPER.INIT requires 1 argument: model_path$");
        *out_result = false;
        return;
    }

    std::string model_path = g_to_string(args[0]);
    
    // Initialize the whisper context from the model file
    struct whisper_context* ctx = whisper_init_from_file(model_path.c_str());

    if (ctx == nullptr) {
        g_error_set(12, vm.runtime_current_line, "Failed to load Whisper model from: " + model_path);
        *out_result = false;
        return;
    }
    
    // Initialize SDL Audio Subsystem if not already done
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        g_error_set(1, vm.runtime_current_line, "Failed to initialize SDL Audio: " + std::string(SDL_GetError()));
        whisper_free(ctx);
        *out_result = false;
        return;
    }

    // Create a custom deleter for the OpaqueHandle to ensure whisper_free is called
    auto whisper_deleter = [](void* p) {
        if (p) {
            whisper_free(static_cast<whisper_context*>(p));
        }
        // Also clean up SDL when the handle is destroyed
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    };

    auto handle = std::make_shared<OpaqueHandle>(
        static_cast<void*>(ctx),
        "WHISPERCTX",
        whisper_deleter
    );

    *out_result = handle;
}

// WHISPER.START(handle)
void builtin_whisper_start(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, "WHISPER.START requires 1 argument: whisper_handle");
        *out_result = false; return;
    }
    if (g_is_recording) {
        g_error_set(1, vm.runtime_current_line, "Recording is already in progress.");
        *out_result = false; return;
    }
     if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "Argument must be a valid Whisper Handle.");
        *out_result = false; return;
    }
    const auto& handle_ptr = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!handle_ptr || !handle_ptr->ptr || handle_ptr->type_name != "WHISPERCTX") {
        g_error_set(15, vm.runtime_current_line, "Argument is not a valid Whisper Handle.");
        *out_result = false; return;
    }
    
    whisper_context* ctx = static_cast<whisper_context*>(handle_ptr->ptr);

    // --- Open the default recording device ---
    SDL_AudioSpec desired_spec, obtained_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = WHISPER_SAMPLE_RATE; // 16000 Hz
    desired_spec.format = SDL_AUDIO_F32;       // 32-bit float
    desired_spec.channels = 1;               // Mono
    desired_spec.samples = 4096;             // Buffer size

    g_audio_device_id = SDL_OpenAudioDevice(NULL, SDL_TRUE, &desired_spec, &obtained_spec, 0);
    if (g_audio_device_id == 0) {
        g_error_set(1, vm.runtime_current_line, "Failed to open audio device: " + std::string(SDL_GetError()));
        *out_result = false; return;
    }

    // Clear previous results and start recording
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        g_audio_buffer.clear();
        g_transcribed_text.clear();
    }
    
    g_is_recording = true;
    SDL_ResumeAudioDevice(g_audio_device_id); // Start capturing
    g_recording_thread = std::thread(audio_capture_thread_func, ctx);

    *out_result = true;
}

// WHISPER.STOP()
void builtin_whisper_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (!args.empty()) {
        g_error_set(8, vm.runtime_current_line, "WHISPER.STOP takes no arguments.");
        *out_result = false; return;
    }
    if (!g_is_recording) {
        g_error_set(1, vm.runtime_current_line, "Recording is not currently active.");
        *out_result = false; return;
    }

    g_is_recording = false; // Signal the thread to stop
    
    // Stop and close the audio device
    if (g_audio_device_id != 0) {
        SDL_PauseAudioDevice(g_audio_device_id);
        SDL_CloseAudioDevice(g_audio_device_id);
        g_audio_device_id = 0;
    }
    
    if (g_recording_thread.joinable()) {
        g_recording_thread.join(); // Wait for the thread to finish processing
    }

    *out_result = true;
}

// WHISPER.GET_TEXT$() -> string$
void builtin_whisper_get_text(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (!args.empty()) {
        g_error_set(8, vm.runtime_current_line, "WHISPER.GET_TEXT$ takes no arguments.");
        *out_result = std::string(""); return;
    }
    if (g_is_recording) {
        g_error_set(1, vm.runtime_current_line, "Cannot get text while recording is active. Call WHISPER.STOP first.");
        *out_result = std::string(""); return;
    }

    std::lock_guard<std::mutex> lock(g_audio_mutex);
    *out_result = g_transcribed_text;
}


// === REGISTRATION LOGIC ===

void register_whisper_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
    auto register_func = [&](const std::string& name, int arity, NativeDLLFunction func_ptr, bool is_proc = false) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_dll_impl = func_ptr;
        info.is_procedure = is_proc;
        table[g_to_upper(name)] = info;
    };

    register_func("WHISPER.INIT", 1, builtin_whisper_init);
    register_func("WHISPER.START", 1, builtin_whisper_start, true);
    register_func("WHISPER.STOP", 0, builtin_whisper_stop, true);
    register_func("WHISPER.GET_TEXT$", 0, builtin_whisper_get_text);
}

// The main entry point of the DLL.
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }

    // Store the callback function pointers provided by jdBasic
    g_error_set = services->error_set;
    g_to_upper = services->to_upper;
    g_to_string = services->to_string;
    
    // Register all our new functions with the interpreter's main function table
    register_whisper_functions(*vm, vm->main_function_table);
}
