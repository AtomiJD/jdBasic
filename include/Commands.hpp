// Commands.hpp
#pragma once
#include "Types.hpp" // For BasicValue
#include <string>

class NeReLaBasic; // Forward declaration to avoid circular dependencies

namespace Commands {
    void do_dim(NeReLaBasic& vm);
    void do_input(NeReLaBasic& vm);
    void do_print(NeReLaBasic& vm);
    void do_let(NeReLaBasic& vm);
    void do_goto(NeReLaBasic& vm);
    void do_if(NeReLaBasic& vm);
    void do_else(NeReLaBasic& vm);
    void do_for(NeReLaBasic& vm);
    void do_next(NeReLaBasic& vm);
    void do_exit_for(NeReLaBasic& vm);
    void do_exit_do(NeReLaBasic& vm);

    void do_func(NeReLaBasic& vm);
    void do_callfunc(NeReLaBasic& vm);
    void do_return(NeReLaBasic& vm);
    void do_endfunc(NeReLaBasic& vm);
    void do_sub(NeReLaBasic& vm);
    void do_endsub(NeReLaBasic& vm);
    void do_callsub(NeReLaBasic& vm);
    void do_onerrorcall(NeReLaBasic& vm);
    void do_resume(NeReLaBasic& vm);
    void do_on(NeReLaBasic& vm);
    void do_raiseevent(NeReLaBasic& vm);
    void do_do(NeReLaBasic& vm);
    void do_loop(NeReLaBasic& vm);
    void do_end(NeReLaBasic& vm);
    
    void do_dllimport(NeReLaBasic& vm);

    void do_edit(NeReLaBasic& vm);
    void do_list(NeReLaBasic& vm);
    void do_load(NeReLaBasic& vm);
    void do_save(NeReLaBasic& vm);
    void do_compile(NeReLaBasic& vm);
    void do_run(NeReLaBasic& vm);

    void do_tron(NeReLaBasic& vm);
    void do_troff(NeReLaBasic& vm);
    void do_dump(NeReLaBasic& vm);
    void do_stop(NeReLaBasic& vm);
}

BasicValue& get_variable(NeReLaBasic& vm, const std::string& name);
void set_variable(NeReLaBasic& vm, const std::string& name, const BasicValue& value, bool force = false);
std::string to_string(const BasicValue& val);
std::string to_upper(std::string s);
std::string read_string(NeReLaBasic& vm);

void dump_p_code(const std::vector<uint8_t>& p_code_to_dump, const std::string& name);


