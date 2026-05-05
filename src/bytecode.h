#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "value.h"

enum class OpCode : uint8_t {
    // Constants & variables
    LOAD_CONST,         // u16 index into constant pool
    LOAD_VAR,           // u16 variable slot
    STORE_VAR,          // u16 variable slot
    LOAD_GLOBAL,        // u16 global name index
    STORE_GLOBAL,       // u16 global name index

    // Arithmetic
    ADD, SUB, MUL, DIV, IDIV, MOD_OP, POW, NEG,

    // Comparison
    CMP_EQ, CMP_NE, CMP_LT, CMP_GT, CMP_LE, CMP_GE,

    // Logical
    LOG_AND, LOG_OR, LOG_NOT,

    // Bitwise
    BIT_AND, BIT_OR, BIT_XOR, BIT_NOT, BIT_SHL, BIT_SHR,

    // IN operator
    OP_IN,

    // Control flow
    JUMP,               // i16 offset (relative)
    JUMP_IF_FALSE,      // i16 offset
    JUMP_IF_TRUE,       // i16 offset
    JUMP_ABS,           // u32 absolute address

    // Functions
    CALL,               // u16 func_index, u8 argc
    RETURN_VOID,
    RETURN_VAL,

    // I/O
    PRINT,
    PRINT_NL,
    PRINT_SPACE,
    INPUT_VAR,          // u16 variable slot

    // Stack manipulation
    POP,
    DUP,

    // Arrays / Maps
    MAKE_ARRAY,         // u16 count
    MAKE_MAP,           // u16 count (key-value pairs)
    INDEX_GET,
    INDEX_SET,

    // Objects
    GET_FIELD,          // u16 name index
    SET_FIELD,          // u16 name index

    // Type cast
    CAST,               // u8 target ValueType

    // String concat
    STR_CONCAT,

    // Loop control (patched at compile time)
    BREAK_LOOP,         // jump to loop end (patched)
    CONTINUE_LOOP_OP,   // jump to loop continue point (patched)

    // Exception handling
    SETUP_TRY,          // u16 catch_offset (relative from after operand)
    POP_TRY,            // remove current try handler
    THROW_OP,           // pop error message, trigger handler

    CALL_METHOD,        // u16 method_name_idx, u8 argc — call method on stack object
    STOP_OP,            // pause execution (resumable)
    MULTI_INDEX_SET,    // u8 count  — container[i1][i2]...[iN] = val  with vectorization
                        // stack layout (top → bottom): val, iN, ..., i2, i1, container
                        // When any i_k is an ARRAY, iterates in parallel
                        // (NumPy-style fancy indexing).
    HALT,               // natural end-of-chunk marker; sub-runs continue normally
    END_PROGRAM,        // user `END` statement — always halts the whole program,
                        // even when nested in event handlers / REPL run_code

    // ── Superinstructions (peephole-fused) ────────────────────
    // All fused ops keep the total byte width of the original sequence
    // so that existing jump offsets remain valid. Unused trailing bytes
    // are filled with NOP.
    MARK_CONST,                  // u16 var_name_idx — mark global as constant
    NOP,                         // no-op, 1 byte, used as padding

    // LOAD_VAR slot + LOAD_CONST idx + ADD/SUB/MUL (7 bytes total)
    // Fused layout: op(1) + slot(2) + const_idx(2) + NOP(1) + NOP(1)
    LOAD_VAR_ADD_CONST,          // u16 slot, u16 const_idx
    LOAD_VAR_SUB_CONST,          // u16 slot, u16 const_idx
    LOAD_VAR_MUL_CONST,          // u16 slot, u16 const_idx

    // LOAD_VAR slot + LOAD_CONST idx + CMP_* (7 bytes total)
    LOAD_VAR_CMP_LT_CONST,       // u16 slot, u16 const_idx
    LOAD_VAR_CMP_GT_CONST,       // u16 slot, u16 const_idx
    LOAD_VAR_CMP_LE_CONST,       // u16 slot, u16 const_idx
    LOAD_VAR_CMP_GE_CONST,       // u16 slot, u16 const_idx
    LOAD_VAR_CMP_EQ_CONST,       // u16 slot, u16 const_idx
    LOAD_VAR_CMP_NE_CONST,       // u16 slot, u16 const_idx

    // LOAD_VAR slot + RETURN_VAL (4 bytes total)
    // Fused layout: op(1) + slot(2) + NOP(1)
    LOAD_VAR_RETURN,             // u16 slot

    // STATIC local variables (per-FuncProto persistent slots).
    // Storage hangs off Chunk::static_values/static_inited so it survives
    // across calls and is shared across recursion. Cross-module dispatch
    // resolves to the same FuncProto, so the slot is identity-stable.
    LOAD_STATIC,                 // u16 slot — push static_values[slot]
    STORE_STATIC,                // u16 slot — pop into static_values[slot]
    // Guard for lazy init: if static_inited[slot] is set, jump by i16
    // offset (skip past the init block). Otherwise set the guard FIRST
    // (so a recursive call during init terminates against the default
    // slot value, not infinite recursion) and fall through.
    MAYBE_INIT_STATIC,           // u16 slot, i16 offset
};

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<std::string> var_names;      // slot -> name mapping
    std::vector<int> line_info;              // bytecode offset -> source line
    std::string source_file;                 // originating source file path (for debugger)

    // Per-chunk inline cache for CALL dispatch. Each entry packs a 32-bit
    // generation counter (high) and a 32-bit resolved user-function index
    // (low). The generation counter lets us invalidate the cache lazily when
    // func_map changes (no need to walk every chunk on reload).
    //
    //   bits 63..32: vm.func_map_generation at the time of caching
    //   bits 31..0 : int32_t (>= 0 = user func idx, -1 = cached-as-native,
    //                         -2 = unresolved / indirect call)
    //
    // Indexed by constants[] index. Grows on first CALL at a given name.
    mutable std::vector<uint64_t> call_cache;

    // Per-chunk inline cache for global-variable dispatch. Same 64-bit
    // encoding (generation in high bits, resolved global slot in low bits).
    // Indexed by var_names[] index. Grows on first LOAD_GLOBAL/STORE_GLOBAL
    // at a given name. Invalidated on VM reset/restore_state via the same
    // generation counter used for CALL caches.
    mutable std::vector<uint64_t> global_cache;

    // STATIC local variables: per-FuncProto persistent storage. Sized at
    // compile time, slot indexes baked into LOAD_STATIC/STORE_STATIC bytes.
    // Init guard is mutated on first execution of MAYBE_INIT_STATIC, hence
    // mutable (CallFrame::chunk is `const Chunk*`).
    mutable std::vector<Value>   static_values;
    mutable std::vector<uint8_t> static_inited;

    // Emit helpers
    size_t emit(OpCode op, int line) {
        size_t pos = code.size();
        code.push_back(static_cast<uint8_t>(op));
        line_info.push_back(line);
        return pos;
    }

    size_t emit_u8(uint8_t val, int line) {
        size_t pos = code.size();
        code.push_back(val);
        line_info.push_back(line);
        return pos;
    }

    size_t emit_u16(uint16_t val, int line) {
        size_t pos = code.size();
        code.push_back(val & 0xFF);
        code.push_back((val >> 8) & 0xFF);
        line_info.push_back(line);
        line_info.push_back(line);
        return pos;
    }

    size_t emit_i16(int16_t val, int line) {
        return emit_u16(static_cast<uint16_t>(val), line);
    }

    uint16_t read_u16(size_t offset) const {
        return code[offset] | (code[offset + 1] << 8);
    }

    void patch_u16(size_t offset, uint16_t val) {
        code[offset] = val & 0xFF;
        code[offset + 1] = (val >> 8) & 0xFF;
    }

    void patch_i16(size_t offset, int16_t val) {
        patch_u16(offset, static_cast<uint16_t>(val));
    }

    uint16_t add_constant(Value v) {
        constants.push_back(std::move(v));
        return static_cast<uint16_t>(constants.size() - 1);
    }

    uint16_t add_var_name(const std::string& name) {
        for (uint16_t i = 0; i < var_names.size(); i++) {
            if (var_names[i] == name) return i;
        }
        var_names.push_back(name);
        return static_cast<uint16_t>(var_names.size() - 1);
    }
};

// Returns the *total* byte width of an instruction (opcode + operands).
// Used by the compiler's peephole pass to walk the bytecode correctly.
inline int opcode_width(OpCode op) {
    switch (op) {
        // u16 operand
        case OpCode::LOAD_CONST:
        case OpCode::LOAD_VAR:
        case OpCode::STORE_VAR:
        case OpCode::LOAD_GLOBAL:
        case OpCode::STORE_GLOBAL:
        case OpCode::LOAD_STATIC:
        case OpCode::STORE_STATIC:
        case OpCode::INPUT_VAR:
        case OpCode::MAKE_ARRAY:
        case OpCode::MAKE_MAP:
        case OpCode::GET_FIELD:
        case OpCode::SET_FIELD:
        case OpCode::JUMP:
        case OpCode::JUMP_IF_FALSE:
        case OpCode::JUMP_IF_TRUE:
        case OpCode::JUMP_ABS:
        case OpCode::SETUP_TRY:
        case OpCode::BREAK_LOOP:
        case OpCode::CONTINUE_LOOP_OP:
            return 3; // opcode + u16

        // u16 slot + i16 jump offset
        case OpCode::MAYBE_INIT_STATIC:
            return 5;

        // u16 + u8
        case OpCode::CALL:
        case OpCode::CALL_METHOD:
            return 4;

        // u8 operand
        case OpCode::CAST:                  // u8 target ValueType
        case OpCode::MULTI_INDEX_SET:       // u8 count — was missing → peephole
                                            // walked into the count byte and
                                            // corrupted the rest of the chunk
            return 2;

        // Fused: op + u16 slot + u16 const_idx + 2 NOP padding = 7
        case OpCode::LOAD_VAR_ADD_CONST:
        case OpCode::LOAD_VAR_SUB_CONST:
        case OpCode::LOAD_VAR_MUL_CONST:
        case OpCode::LOAD_VAR_CMP_LT_CONST:
        case OpCode::LOAD_VAR_CMP_GT_CONST:
        case OpCode::LOAD_VAR_CMP_LE_CONST:
        case OpCode::LOAD_VAR_CMP_GE_CONST:
        case OpCode::LOAD_VAR_CMP_EQ_CONST:
        case OpCode::LOAD_VAR_CMP_NE_CONST:
            return 7;

        // Fused: op + u16 slot + 1 NOP padding = 4
        case OpCode::LOAD_VAR_RETURN:
            return 4;

        // Everything else: single-byte opcode, no operands
        default:
            return 1;
    }
}

// Runs a peephole pass over a chunk: fuses common LOAD_VAR + LOAD_CONST + op
// sequences into single super-instructions. The replacement is in-place and
// keeps the original byte width (padding with NOP) so all jump offsets remain
// valid without recomputation.
inline void peephole_optimize(Chunk& chunk) {
    auto& code = chunk.code;
    const size_t n = code.size();
    if (n < 7) return;

    // 1. Collect all instruction starts so we never rewrite inside an operand.
    std::vector<bool> is_insn_start(n, false);
    // 2. Collect all jump targets so we never replace a sequence that a jump
    //    lands inside of (except at the starting byte).
    std::vector<bool> is_jump_target(n, false);

    size_t ip = 0;
    while (ip < n) {
        is_insn_start[ip] = true;
        OpCode op = (OpCode)code[ip];
        int w = opcode_width(op);

        // Jump targets
        if (op == OpCode::JUMP || op == OpCode::JUMP_IF_FALSE ||
            op == OpCode::JUMP_IF_TRUE) {
            int16_t off = (int16_t)(code[ip + 1] | (code[ip + 2] << 8));
            size_t target = ip + 3 + off;
            if (target < n) is_jump_target[target] = true;
        } else if (op == OpCode::JUMP_ABS) {
            uint16_t addr = code[ip + 1] | (code[ip + 2] << 8);
            if (addr < n) is_jump_target[addr] = true;
        } else if (op == OpCode::SETUP_TRY) {
            uint16_t off = code[ip + 1] | (code[ip + 2] << 8);
            size_t target = ip + 3 + off;
            if (target < n) is_jump_target[target] = true;
        } else if (op == OpCode::MAYBE_INIT_STATIC) {
            // op(1) + slot(2) + i16 offset(2). Offset relative to ip+5.
            int16_t off = (int16_t)(code[ip + 3] | (code[ip + 4] << 8));
            size_t target = ip + 5 + off;
            if (target < n) is_jump_target[target] = true;
        }
        ip += w;
        if (w <= 0) break; // safety
    }

    // Helper: true if no jump lands *within* [start+1, end)
    auto safe_range = [&](size_t start, size_t end) {
        for (size_t i = start + 1; i < end; i++)
            if (i < n && is_jump_target[i]) return false;
        return true;
    };

    // 3. Scan and rewrite.
    ip = 0;
    while (ip < n) {
        OpCode op = (OpCode)code[ip];

        // Pattern: LOAD_VAR(3) + LOAD_CONST(3) + ARITH/CMP(1) = 7 bytes
        if (op == OpCode::LOAD_VAR && ip + 7 <= n &&
            (OpCode)code[ip + 3] == OpCode::LOAD_CONST &&
            is_insn_start[ip + 3] && is_insn_start[ip + 6] &&
            safe_range(ip, ip + 7))
        {
            OpCode op3 = (OpCode)code[ip + 6];
            OpCode fused = OpCode::NOP;
            switch (op3) {
                case OpCode::ADD:    fused = OpCode::LOAD_VAR_ADD_CONST; break;
                case OpCode::SUB:    fused = OpCode::LOAD_VAR_SUB_CONST; break;
                case OpCode::MUL:    fused = OpCode::LOAD_VAR_MUL_CONST; break;
                case OpCode::CMP_LT: fused = OpCode::LOAD_VAR_CMP_LT_CONST; break;
                case OpCode::CMP_GT: fused = OpCode::LOAD_VAR_CMP_GT_CONST; break;
                case OpCode::CMP_LE: fused = OpCode::LOAD_VAR_CMP_LE_CONST; break;
                case OpCode::CMP_GE: fused = OpCode::LOAD_VAR_CMP_GE_CONST; break;
                case OpCode::CMP_EQ: fused = OpCode::LOAD_VAR_CMP_EQ_CONST; break;
                case OpCode::CMP_NE: fused = OpCode::LOAD_VAR_CMP_NE_CONST; break;
                default: break;
            }
            if (fused != OpCode::NOP) {
                // Layout before: LOAD_VAR, slot_lo, slot_hi, LOAD_CONST, idx_lo, idx_hi, OP3
                // Layout after:  FUSED,    slot_lo, slot_hi, idx_lo,     idx_hi,  NOP,    NOP
                code[ip + 0] = (uint8_t)fused;
                // slot bytes (ip+1, ip+2) are unchanged
                code[ip + 3] = code[ip + 4]; // idx_lo
                code[ip + 4] = code[ip + 5]; // idx_hi
                code[ip + 5] = (uint8_t)OpCode::NOP;
                code[ip + 6] = (uint8_t)OpCode::NOP;
                ip += 7;
                continue;
            }
        }

        // Pattern: LOAD_VAR(3) + RETURN_VAL(1) = 4 bytes
        if (op == OpCode::LOAD_VAR && ip + 4 <= n &&
            (OpCode)code[ip + 3] == OpCode::RETURN_VAL &&
            is_insn_start[ip + 3] &&
            safe_range(ip, ip + 4))
        {
            code[ip + 0] = (uint8_t)OpCode::LOAD_VAR_RETURN;
            // slot bytes unchanged
            code[ip + 3] = (uint8_t)OpCode::NOP;
            ip += 4;
            continue;
        }

        int w = opcode_width(op);
        if (w <= 0) break;
        ip += w;
    }
}

struct FuncProto {
    std::string name;
    int arity = 0;
    std::vector<std::string> param_names;
    Chunk chunk;
    bool is_sub = false;   // SUB vs FUNCTION
    bool is_async = false; // ASYNC FUNC
};
