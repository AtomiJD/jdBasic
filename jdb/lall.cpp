// In NeReLaBasic.cpp

BasicValue NeReLaBasic::launch_bsync_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    auto promise = std::make_shared<std::promise<BasicValue>>();
    std::future<BasicValue> future = promise->get_future();

    // The thread lambda captures the promise and arguments.
    std::thread worker_thread([this, promise, func_info, args]() {
        try {
            // 1. Create a completely isolated copy of the interpreter.
            auto thread_vm = std::make_unique<NeReLaBasic>(*this);

            // 2. Execute the function synchronously *within this thread* using the isolated VM.
            BasicValue result = thread_vm->execute_synchronous_function(func_info, args);

            // 3. Fulfill the promise with the result.
            promise->set_value(result);
        } catch (const std::exception& e) {
            // Handle exceptions within the thread to avoid crashing.
            promise->set_exception(std::current_exception());
        }
    });

    // Get the ID of the thread we just created.
    std::thread::id worker_id = worker_thread.get_id();

    // Store the future so we can get the result later, keyed by the thread ID.
    {
        std::lock_guard<std::mutex> lock(background_tasks_mutex);
        background_tasks[worker_id] = std::move(future);
    }

    // Detach the thread to let it run independently. The `future` is our way to sync with it.
    worker_thread.detach();

    // Return the handle to the BASIC script.
    return ThreadHandle{ worker_id };
}

// In NeReLaBasic::parse_primary()

BasicValue NeReLaBasic::parse_primary() {
    BasicValue current_value;
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::BSYNC) {
        pcode++; // Consume BSYNC
        // Expect a function call right after
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::CALLFUNC) {
            Error::set(1, runtime_current_line, "BSYNC must be followed by a function call.");
            return {};
        }
        pcode++; // Consume CALLFUNC

        std::string func_name = to_upper(read_string(*this));
        if (!active_function_table->count(func_name)) {
            Error::set(22, runtime_current_line, "Function not found for BSYNC: " + func_name);
            return {};
        }
        const auto& func_info = active_function_table->at(func_name);

        // Parse arguments just like a normal function call
        std::vector<BasicValue> args;
        if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
            while (true) {
                args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (separator == Tokens::ID::C_RIGHTPAREN) break;
                if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
            }
        }
        pcode++; // Consume ')'

        // Launch the function in a background thread and get the handle
        return launch_bsync_function(func_info, args);
    }
    else if (token == Tokens::ID::OP_START_TASK) {
        // ... your existing OP_START_TASK logic
    }
    // ... rest of parse_primary()
}