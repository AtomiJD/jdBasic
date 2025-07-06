void NeReLaBasic::execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode) {
    if (code_to_run.empty()) return;

    task_queue.clear();
    next_task_id = 0;

    auto main_task = std::make_shared<Task>();
    main_task->id = next_task_id++;
    main_task->status = TaskStatus::RUNNING;
    main_task->p_code_ptr = &code_to_run;
    main_task->p_code_counter = resume_mode ? this->pcode : 0;
    //if (dap_handler) {
    //    main_task->debug_state = DebugState::PAUSED;
    //    dap_handler->send_stopped_message("entry", 0, this->program_to_debug);
    //}
    task_queue[main_task->id] = main_task;

    Error::clear();
    g_vm_instance_ptr = this;

    //dap_handler->send_output_message("We are in execute main.\n");

    if (dap_handler) { // Check if the debugger is attached
        debug_state = DebugState::PAUSED;
        dap_handler->send_output_message("debug_state = DebugState::PAUSED.\n");
        // Tell the client we are paused at the entry point.
        runtime_current_line = (*active_p_code)[0] | ((*active_p_code)[1] << 8);
        dap_handler->send_output_message("runtime_current_line = (*active_p_code)[0] | ((*active_p_code)[1] << 8);\n");
        dap_handler->send_stopped_message("entry", runtime_current_line, this->program_to_debug);
        dap_handler->send_output_message("dap_handler->send_stopped_message('entry', runtime_current_line, this->program_to_debug);\n");
        // Wait for the first "continue" or "next" command from the client.
        pause_for_debugger();
        dap_handler->send_output_message("pause_for_debugger();\n");
    }

    while (!task_queue.empty()) {
#ifdef SDL3
        if (graphics_system.is_initialized) { if (!graphics_system.handle_events()) break; }
#endif
        if (!nopause_active && _kbhit()) {
            char key = _getch();
            if (key == 27) { TextIO::print("\n--- BREAK ---\n"); break; }
            else if (key == ' ') {
                TextIO::print("\n--- PAUSED (Press any key to resume) ---\n");
                _getch();
                TextIO::print("--- RESUMED ---\n");
            }
        }

        for (auto it = task_queue.begin(); it != task_queue.end(); ) {
            current_task = it->second.get();

            // --- Context Switch: Load ---
            this->pcode = current_task->p_code_counter;
            this->active_p_code = current_task->p_code_ptr;
            this->call_stack = current_task->call_stack;
            this->for_stack = current_task->for_stack;
            // Restore the correct function table for the current context
            if (!this->call_stack.empty()) {
                this->active_function_table = this->call_stack.back().previous_function_table_ptr;
            }
            else {
                this->active_function_table = &this->main_function_table;
            }
            current_task->yielded_execution = false;

            bool task_removed = false;

            // --- Task Execution Logic ---
            if (current_task->status == TaskStatus::RUNNING) {
                // --- Task Completion & Error Check at the TOP ---
                if (Error::get() != 0 || pcode >= active_p_code->size() || 
                    (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD &&
                     static_cast<Tokens::ID>((*active_p_code)[pcode-1]) != Tokens::ID::C_CR)
                    ) {
                    current_task->status = (Error::get() != 0) ? TaskStatus::ERRORED : TaskStatus::COMPLETED;
                    if (variables.count("RETVAL")) {
                        current_task->result = variables["RETVAL"];
                    }
                    goto end_of_task_processing; // Skip to cleanup
                }
                // --- DAP and Breakpoint Logic ---
                if (dap_handler) {
                    if (current_task->debug_state == DebugState::PAUSED) {
                        pause_for_debugger();
                    }
                    runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
                    if (breakpoints.count(runtime_current_line)) {
                        if (current_task->debug_state != DebugState::STEP_OVER || call_stack.size() <= current_task->step_over_stack_depth) {
                            current_task->debug_state = DebugState::PAUSED;
                            dap_handler->send_stopped_message("breakpoint", runtime_current_line, this->program_to_debug);
                            pause_for_debugger();
                            continue; // Re-evaluate this task in the next scheduler cycle
                        }
                    }
                }

                // --- Main Statement Execution ---
                //if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR && static_cast<Tokens::ID>((*active_p_code)[pcode-1]) != Tokens::ID::C_CR) {
                //    pcode++;
                //}
                runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
                pcode += 2;

                bool line_is_done = false;
                bool statement_is_run = false;
                while (!line_is_done && pcode < active_p_code->size()) {
                    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR) {
                        statement();
                    }
                    if (Error::get() != 0 || is_stopped) {
                        line_is_done = true; // Stop processing this line on error
                        continue;
                    }
                    
                    // After the statement, check the token that follows.
                    if (pcode < active_p_code->size()) {
                        Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                        if (next_token == Tokens::ID::C_COLON) {
                            pcode++; // Consume the separator and loop again for the next statement.
                        }
                        else {
                            // Token (C_CR, NOCMD) means the line is finished.
                            if (next_token == Tokens::ID::C_CR || next_token == Tokens::ID::NOCMD) {
                                line_is_done = true;
                            }
                        }
                    }

                    // --- Post-Line Processing ---
                    if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                        pcode++;
                    }

                    // --- Post-Statement DAP Logic ---
                    if (dap_handler && (current_task->debug_state == DebugState::STEP_OVER && call_stack.size() <= current_task->step_over_stack_depth)) {
                        current_task->debug_state = DebugState::PAUSED;
                        uint16_t next_line = (pcode < active_p_code->size() - 1) ? (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8) : runtime_current_line;
                        dap_handler->send_stopped_message("step", next_line, this->program_to_debug);
                        pause_for_debugger();
                    }

                    // --- Error Handling Logic ---
                    if (Error::get() != 0 && current_task->error_handler_active && current_task->jump_to_error_handler) {
                        jump_to_error_handler = false; // Reset the flag
                        // Clear the actual error code so ERR/ERL can be read by the handler
                        uint8_t caught_error_code = Error::get();
                        Error::clear();

                        // Invoke the error handling function (like a CALLSUB)
                        // You can re-use your do_callsub logic or call it directly:
                        if (active_function_table->count(error_handler_function_name)) {
                            const auto& proc_info = active_function_table->at(error_handler_function_name);
                            // Call the procedure without arguments (ERR and ERL are global vars)
                            NeReLaBasic::StackFrame frame;
                            frame.return_p_code_ptr = this->active_p_code;
                            frame.return_pcode = this->pcode;
                            frame.previous_function_table_ptr = this->active_function_table;
                            frame.for_stack_size_on_entry = this->for_stack.size();
                            frame.function_name = error_handler_function_name;
                            frame.linenr = runtime_current_line;
                            // No args to pass if ERR and ERL are global
                            this->call_stack.push_back(frame);

                            if (!proc_info.module_name.empty() && compiled_modules.count(proc_info.module_name)) {
                                auto& target_module = compiled_modules[proc_info.module_name];
                                this->active_p_code = &target_module.p_code;
                                this->active_function_table = &target_module.function_table;
                            }
                            uint16_t current_error_pcode_snapshot = pcode; // Save where the error occurred

                            // Find the end of the current logical line
                            while (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::NOCMD) {
                                pcode++; // Move past the current problematic statement/expression
                            }
                            if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                                pcode++; // Consume C_CR
                                pcode += 2; // Consume line number bytes for the *next* line
                            }
                            // Now pcode points to the start of the *next* statement after the error line.
                            resume_pcode_next_statement = pcode;

                            // Restore pcode to where it was for ERL to be accurate in the handler
                            pcode = current_error_pcode_snapshot;

                            Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                            if (next_token == Tokens::ID::C_CR)
                                pcode++; // Consume the C_CR
                        }
                        else {
                            // If the error handler function is somehow not found at runtime,
                            // this is a fatal error, print the original error and stop.
                            Error::set(caught_error_code, runtime_current_line); // Re-set original error
                            Error::print();
                            break; // Stop execution
                        }
                        continue; // Continue execution loop from the new pcode (the error handler)
                    }
                }
            }
            else if (current_task->status == TaskStatus::PAUSED_ON_AWAIT) {
                if (current_task->awaiting_task && current_task->awaiting_task->status == TaskStatus::COMPLETED) {
                    current_task->status = TaskStatus::RUNNING;
                    current_task->awaiting_task = nullptr;
                }
            }

        end_of_task_processing:
            // --- Context Switch: Save ---
            current_task->p_code_counter = this->pcode;
            current_task->call_stack = this->call_stack;
            current_task->for_stack = this->for_stack;

            if (current_task->status == TaskStatus::COMPLETED || current_task->status == TaskStatus::ERRORED) {
                it = task_queue.erase(it);
                task_removed = true;
            }

            if (!task_removed) { ++it; }
        }

        if (task_queue.empty() || !task_queue.count(0)) { break; }
    }

#ifdef SDL3
    graphics_system.shutdown();
    sound_system.shutdown();
#endif
    g_vm_instance_ptr = nullptr;
    current_task = nullptr;
}