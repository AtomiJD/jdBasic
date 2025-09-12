#include "BetterCodeFunctions.hpp"
#include "KeywordRepository.hpp"
#include "Error.hpp"
#include "Types.hpp"
#include "BuiltinFunctions.hpp" // For functions like builtin_transpose
#include "Compiler.hpp"
#include "Commands.hpp"       // For utility functions like to_double, to_string
#include "TextIO.hpp"
#include "Statements.hpp"
#include "StringUtils.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm> // transform, max
#include <cctype>    // isalpha, isalnum, toupper, tolower
#include <cstring>   // strlen
#include <sstream>
#include <initializer_list>

// ===================== PRETTY (code formatter) =====================
// PRETTY [PREVIEW] [STYLE UPPER|VB] [WIDTH n]
//
// - Formats vm.source_lines in-place (unless PREVIEW).
// - Indentation based on block keywords (IF/ELSE/ENDIF, FOR/NEXT, DO/LOOP, WHILE/WEND,
//   TRY/CATCH/FINALLY/ENDTRY, SELECT/CASE/ENDSELECT or SWITCH/CASE/ENDSWITCH,
//   SUB/ENDSUB, FUNC/ENDFUNC, TYPE/ENDTYPE, ENUM/ENDENUM).
// - Preserves strings and comments verbatim.
// - Normalizes spacing around common operators and punctuation.
// - Canonicalizes keyword casing (UPPERCASE by default; VB Title Case optional).

namespace {
    // --- Utilities ---

    static inline bool is_ident_start(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    static inline bool is_ident_part(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    }

    // A comprehensive list to prevent misidentifying them as variables.
    const std::unordered_set<std::string> BUILTIN_FUNCTIONS = {
        "SIN", "COS", "TAN", "SQR", "RND", "LOG", "LOG10", "FAC", "INT", "FLOOR", "CEIL", "ROUND", "TRUNC",
        "LEFT$", "RIGHT$", "MID$", "LEN", "LCASE$", "UCASE$", "TRIM$", "STR$", "VAL", "CHR$", "ASC", "INSTR$", "SPLIT", "FRMV$",
        "APPEND", "DIFF", "IOTA", "SUM", "PRODUCT", "MIN", "MAX", "ANY", "ALL", "SCAN", "SELECT", "FILTER", "REDUCE",
        "TAKE", "DROP", "RESHAPE", "REVERSE", "TRANSPOSE", "MATMUL", "MVLET", "INTEGRATE", "SOLVE", "INVERT",
        "NORMALIZE", "UNIQUE", "SHUFFLE", "FIND_IN_ARRAY", "DISTANCE", "STACK", "SLICE", "LERP", "GRADE", "OUTER",
        "ROTATE", "SHIFT", "XSORT", "CONVOLVE", "PLACE",
        "TXTREADER$", "CSVREADER",
        "GETENV$", "TICK", "DATE$", "TIME$", "NOW", "DATEADD", "DATEDIFF", "CVDATE",
        "TYPEOF",
        "OS.GETOS", "OS.ARGS", "OS.EXEC",
        "JSON.PARSE$", "JSON.STRINGIFY$", "CREATEOBJECT",
        "HTTP.GET$", "HTTP.POST$", "HTTP.PUT$", "HTTP.STATUSCODE",
        "TENSOR.FROM", "TENSOR.TOARRAY", "TENSOR.BACKWARD", "TENSOR.SIGMOID", "TENSOR.RELU", "TENSOR.SOFTMAX",
        "TENSOR.CROSS_ENTROPY_LOSS", "TENSOR.TOKENIZE", "TENSOR.POSITIONAL_ENCODING", "TENSOR.LAYERNORM",
        "TENSOR.CONV2D", "TENSOR.MAXPOOL2D",
        "THREAD.ISDONE", "THREAD.GETRESULT",
        "REGEX.MATCH", "REGEX.FINDALL", "REGEX.REPLACE", "AWAIT",
        "PI"
    };

    // Block keywords: affect indentation.
    static const std::unordered_set<std::string> kOpeners = {
        "IF","DO","FOR","TRY","SELECT","SWITCH","SUB","FUNC","TYPE","ENUM"
    };
    static const std::unordered_set<std::string> kClosers = {
        "ENDIF","LOOP","NEXT","ENDTRY","ENDSELECT","ENDSWITCH","ENDSUB","ENDFUNC","ENDTYPE","ENDENUM"
    };
    // Mid-block adjusters: dedent before printing them, then re-indent after the line.
    static const std::unordered_set<std::string> kMids = {
        "ELSE","ELSEIF","CATCH","FINALLY","CASE","DEFAULT"
    };

    // Convert UPPER keyword to desired case
    static std::string case_keyword(const std::string& upper, bool vbStyle) {
        if (!vbStyle) return upper; // UPPERCASE

        // VB-style Title Case with special-casing some words
        auto title = [&](const std::string& s)->std::string {
            if (s.empty()) return s;
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), ::tolower);
            out[0] = static_cast<char>(::toupper(out[0]));
            return out;
            };

        static const std::unordered_map<std::string, std::string> overrides = {
            {"ENDIF","EndIf"}, {"ELSEIF","ElseIf"}, {"ENDFUNC","EndFunc"},
            {"ENDSUB","EndSub"}, {"ENDTYPE","End Type"}, {"ENDENUM","EndEnum"},
            {"ENDTRY","EndTry"},  {"ENDSWITCH","EndSwitch"},
            {"EXITFUNC","ExitFunc"},{"EXITFOR","ExitFor"},{"EXITDO","ExitDo"},
            {"DEFAULT","Default"}, {"CASE","Case"}, {"SWITCH","Switch"},
            {"FUNC","Func"}, {"DO","Do"}, {"LOOP","Loop"}, {"UNTIL","Until"},
            {"SHL","Shl"}, {"SHR","Shr"}, {"BAND","Band"}, {"BOR","Bor"}, {"BXOR","Bxor"},
            {"MOD","Mod"}, {"AND","And"}, {"OR","Or"}, {"XOR","Xor"}, {"NOT","Not"},
            {"TRUE","True"}, {"FALSE","False"}, {"PRINT","Print"},
        };

        auto it = overrides.find(upper);
        if (it != overrides.end()) return it->second;

        // Fallback: simple Title Case
        return title(upper);
    }

    // Token kinds for spacing rules
    enum class TK { Ident, Number, String, Op, Punct, Comment, Label, EOL};

    struct Tok {
        TK kind;
        std::string text;    // exact text (for strings/comments)
        std::string upper;   // UPPERCASE cache for idents
        int pos;
    };

    // Tokenize one logical line, preserving strings/comments
    static std::vector<Tok> tokenize_code_line(const std::string& line) {
        std::vector<Tok> out;
        size_t i = 0, n = line.size();

        // Strip leading spaces (indent handled separately)
        while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;

        bool in_comment = false;
        while (i < n) {
            char c = line[i];

            if (c == ' ' || c == '\t') { ++i; continue; }

            // Comment start: apostrophe outside string OR REM at token boundary
            if (!in_comment && c == '\'') {
                // Remainder is a comment
                out.push_back({ TK::Comment, line.substr(i), "" });
                break;
            }
            // REM comment (only at a token boundary)
            if (!in_comment && is_ident_start(c)) {
                size_t j = i;
                while (j < n && is_ident_part(line[j])) j++;
                std::string ident = line.substr(i, j - i);
                std::string upper = ident; std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if (upper == "REM" && (j == n || !is_ident_part(line[j]))) {
                    out.push_back({ TK::Comment, line.substr(i), "" });
                    break;
                }
            }

            // String literal
            if (c == '\"') {
                size_t j = i + 1;
                while (j < n) {
                    if (line[j] == '"') {
                        if (j + 1 < n && line[j + 1] == '"') { j += 2; continue; } // escaped "" inside strings
                        j++; break;
                    }
                    j++;
                }
                out.push_back({ TK::String, line.substr(i, j - i), "" });
                i = j;
                continue;
            }

            // Ident or Number
            if (is_ident_start(c)) {
                size_t j = i + 1;
                while (j < n && is_ident_part(line[j])) j++;
                std::string ident = line.substr(i, j - i);
                std::string upper = ident; std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                out.push_back({ TK::Ident, ident, upper });
                i = j; continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t j = i + 1;
                bool seen_dot = false, seen_e = false;
                while (j < n) {
                    char d = line[j];
                    if (std::isdigit(static_cast<unsigned char>(d))) { j++; continue; }
                    if (d == '.' && !seen_dot) { seen_dot = true; j++; continue; }
                    if ((d == 'e' || d == 'E') && !seen_e) { seen_e = true; j++; if (j < n && (line[j] == '+' || line[j] == '-')) j++; continue; }
                    break;
                }
                out.push_back({ TK::Number, line.substr(i, j - i), "" });
                i = j; continue;
            }

            // Operators / punctuation (treat multi-char first)
            auto starts = [&](const char* s)->bool {
                size_t L = std::strlen(s);
                return i + L <= n && line.compare(i, L, s) == 0;
                };
            // multi-char ops
            if (starts("<=") || starts(">=") || starts("<>") || starts("==") || starts("->") || starts("::")) {
                out.push_back({ TK::Op, line.substr(i, 2), "" }); i += 2; continue;
            }
            // single-char ops/punct
            if (std::string("+-*/\\%^=,:;()[]{}").find(c) != std::string::npos ||
                std::string("<>").find(c) != std::string::npos) {
                TK kind = (std::string("()[]{}").find(c) != std::string::npos || c == ',' || c == ';' || c == ':') ? TK::Punct : TK::Op;
                out.push_back({ kind, std::string(1, c), "" });
                i++; continue;
            }

            // Fallback: preserve unknown char
            out.push_back({ TK::Punct, std::string(1, c), "" });
            i++;
        }
        return out;
    }

    // Build a pretty line from tokens, given indent and style
    static std::string render_pretty_line(const std::vector<Tok>& toks, int indent, int indent_width, bool vbStyle) {
        std::string out;
        out.append(std::max(0, indent) * indent_width, ' ');

        auto emit_kw = [&](const std::string& upper) {
            std::string k = case_keyword(upper, vbStyle);
            out += k;
        };

        auto is_op_word = [](const std::string& upper)->bool {
            static const std::unordered_set<std::string> kOpWords = {
                "AND","OR","XOR","BAND","BOR","BXOR","MOD","SHL","SHR"
            };
            return kOpWords.count(upper) != 0;
            };

        auto need_space = [&](const Tok* prev, const Tok& cur)->bool {
            if (!prev) return false;                      // start of line
            if (cur.kind == TK::Comment) return true;     // 1 space before comment

            // --- No space around dot (member access) ---
            if ((prev->kind == TK::Punct && prev->text == ".") ||
                (cur.kind == TK::Punct && cur.text == ".")) return false;

            // --- NEW: No space BEFORE openers '(' '[' '{' (calls/indexers) ---
            if (cur.kind == TK::Punct &&
                (cur.text == "(" || cur.text == "[" || cur.text == "{")) return false;

            // --- NEW: No space BEFORE '@' (suffix operator) ---
            if (cur.kind == TK::Op && cur.text == "@") return false;

            // No space AFTER openers
            if (prev->kind == TK::Punct &&
                (prev->text == "(" || prev->text == "[" || prev->text == "{")) return false;

            // No space BEFORE closers
            if (cur.kind == TK::Punct &&
                (cur.text == ")" || cur.text == "]" || cur.text == "}")) return false;

            // Comma/semicolon/colon: no space BEFORE, one AFTER (handled below)
            if (cur.kind == TK::Punct && (cur.text == "," || cur.text == ";" || cur.text == ":")) return false;
            if (prev->kind == TK::Punct && (prev->text == "," || prev->text == ";" || prev->text == ":")) return true;

            // Symbolic ops: add spaces, but try to keep unary +/-
            if (prev->kind == TK::Op || cur.kind == TK::Op) {
                if (cur.kind == TK::Op && (cur.text == "+" || cur.text == "-")) {
                    if (prev->kind == TK::Op) return false;
                    if (prev->kind == TK::Punct &&
                        (prev->text == "(" || prev->text == "[" || prev->text == "{" ||
                            prev->text == "," || prev->text == ";" || prev->text == ":")) return false;
                }
                return true;
            }

            // Word ops (AND/BAND/…)
            if (cur.kind == TK::Ident && is_op_word(cur.upper)) return (prev != nullptr);
            if (prev->kind == TK::Ident && is_op_word(prev->upper)) return true;

            // Default: space between id/num/str tokens
            return true;
        };

        // In render loop:
        const Tok* prev = nullptr;
        for (size_t i = 0; i < toks.size(); ++i) {
            const auto& t = toks[i];
            if (need_space(prev, t)) { if (!out.empty() && out.back() != ' ') out.push_back(' '); }

            if (t.kind == TK::Ident) {
                if (KeywordRepository::is_keyword(t.upper)) out += case_keyword(t.upper, vbStyle);
                else out += t.text;
            }
            else {
                out += t.text;
            }

            // One space AFTER comma/semicolon/colon
            if (t.kind == TK::Punct && (t.text == "," || t.text == ";" || t.text == ":")) {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
            }
            prev = &t;
        }

        auto want_space_before = [&](const Tok& t)->bool {
            if (t.kind == TK::Comment) return true; // a space before comment if not at line start
            if (t.kind == TK::Punct) {
                if (t.text == "," || t.text == ";" || t.text == ":") return false;
                if (t.text == ")" || t.text == "]" || t.text == "}") return false;
            }
            if (t.kind == TK::Op) {
                if (t.text == ")" || t.text == "]" || t.text == "}") return true;
                return true;
            }
            return true;
            };
        auto want_space_after = [&](const Tok& t)->bool {
            if (t.kind == TK::Comment) return true;
            if (t.kind == TK::Punct) {
                if (t.text == "," || t.text == ";" || t.text == ":") return true;
                if (t.text == "(" || t.text == "[" || t.text == "{") return false;
            }
            if (t.kind == TK::Op) {
                if (t.text == "(" || t.text == "[" || t.text == "{") return false;
                return true;
            }
            return true;
            };

        // Trim trailing spaces
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    // Decide indent change for a tokenized line
    struct IndentDelta { int before = 0; int after = 0; };

    static IndentDelta indent_change_for(const std::vector<Tok>& toks) {
        // Skip empty/comment-only lines
        size_t i = 0;
        while (i < toks.size() && toks[i].kind == TK::Comment) i++;
        if (i >= toks.size()) return {};

        // Skip optional label: Ident followed by ':'
        if (i + 1 < toks.size() && toks[i].kind == TK::Ident &&
            toks[i + 1].kind == TK::Punct && toks[i + 1].text == ":") {
            i += 2;
            while (i < toks.size() && toks[i].kind == TK::Comment) i++;
            if (i >= toks.size()) return {};
        }

        auto firstKW = (toks[i].kind == TK::Ident) ? toks[i].upper : std::string();

        IndentDelta d{};

        // Closers dedent before
        if (kClosers.count(firstKW)) { d.before--; return d; }

        // Mid-block keywords: dedent before, re-indent after
        if (kMids.count(firstKW)) { d.before--; d.after++; return d; }

        // Openers indent after
        if (kOpeners.count(firstKW)) {
            if (firstKW == "IF") {
                bool hasTHEN = false;
                bool hasColonAfterThen = false;
                for (size_t j = i; j < toks.size(); ++j) {
                    if (toks[j].kind == TK::Ident && toks[j].upper == "THEN") {
                        hasTHEN = true;
                        // anything other than comment after THEN on same line?
                        for (size_t k = j + 1; k < toks.size(); ++k) {
                            if (toks[k].kind == TK::Comment) break;
                            if (toks[k].kind != TK::Punct || toks[k].text == ":") { hasColonAfterThen = true; break; }
                        }
                        break;
                    }
                }
                if (hasTHEN && !hasColonAfterThen) d.after++;
            }
            else {
                d.after++;
            }
        }

        return d;
    }

    struct LintMessage {
        int line;
        std::string message;
        bool operator<(const LintMessage& other) const { return line < other.line; }
    };

    struct VarInfo {
        int decl_line = 0;
        bool is_param = false;
        std::vector<int> write_lines;
        std::vector<int> read_lines;
    };

    struct FuncInfo {
        std::unordered_set<std::string> params;
        std::unordered_map<std::string, VarInfo> locals;
    };

    // Helper to determine if an IF statement is single-line or block
    bool is_single_line_if(NeReLaBasic& vm, Compiler& compiler, const std::string& line) {
        vm.lineinput = line;
        vm.prgptr = 0;
        bool is_start = true;
        bool then_found = false;
        while (vm.prgptr < vm.lineinput.length()) {
            Tokens::ID token = compiler.parse(vm, is_start);
            if (token == Tokens::ID::NOCMD) break;
            if (token == Tokens::ID::THEN) {
                then_found = true;
                continue;
            }
            if (then_found && token != Tokens::ID::REM) return true; // Found a command after THEN
            is_start = (token == Tokens::ID::C_COLON);
        }
        return false;
    }

    static BasicValue builtin_lint(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        std::vector<LintMessage> messages;
        std::unordered_map<std::string, VarInfo> global_vars;
        std::unordered_map<std::string, FuncInfo> user_funcs;
        std::vector<std::pair<std::string, int>> block_stack;
        bool option_explicit_on = false;

        Compiler linter_compiler;

        // Pass 1: Collect function/sub definitions and their parameters
        for (int line_num = 1; line_num <= static_cast<int>(vm.source_lines.size()); ++line_num) {
            const auto& line = vm.source_lines[line_num - 1];
            vm.lineinput = line;
            vm.prgptr = 0;

            Tokens::ID first_token = linter_compiler.parse(vm, true);
            if (first_token == Tokens::ID::FUNC || first_token == Tokens::ID::SUB) {
                linter_compiler.parse(vm, false);
                std::string func_name = to_upper(vm.buffer);
                user_funcs[func_name] = FuncInfo();

                size_t open_paren = line.find('(');
                size_t close_paren = line.rfind(')');
                if (open_paren != std::string::npos && close_paren != std::string::npos) {
                    std::string params_str = line.substr(open_paren + 1, close_paren - open_paren - 1);
                    std::stringstream ss(params_str);
                    std::string param;
                    while (getline(ss, param, ',')) {
                        StringUtils::trim(param);
                        if (param.length() > 2 && param.substr(param.length() - 2) == "[]") {
                            param.resize(param.length() - 2);
                        }
                        if (!param.empty()) {
                            std::string upper_param = to_upper(param);
                            user_funcs[func_name].params.insert(upper_param);
                            user_funcs[func_name].locals[upper_param].decl_line = line_num;
                            user_funcs[func_name].locals[upper_param].is_param = true;
                        }
                    }
                }
            }
        }

        // Pass 2: Full analysis
        std::string current_scope = "GLOBAL";
        for (int line_num = 1; line_num <= static_cast<int>(vm.source_lines.size()); ++line_num) {
            const auto& line = vm.source_lines[line_num - 1];
            vm.lineinput = line;
            vm.prgptr = 0;

            std::vector<std::pair<Tokens::ID, std::string>> line_tokens;
            bool is_start = true;
            while (vm.prgptr < vm.lineinput.length()) {
                Tokens::ID token = linter_compiler.parse(vm, is_start);
                if (token == Tokens::ID::NOCMD) break;
                if (token != Tokens::ID::LABEL) line_tokens.push_back({ token, vm.buffer });
                is_start = (token == Tokens::ID::C_COLON);
            }
            if (line_tokens.empty()) continue;

            auto& first_token_pair = line_tokens[0];
            Tokens::ID first_token = first_token_pair.first;
            std::string first_kw_upper = to_upper(first_token_pair.second);

            if ((first_token == Tokens::ID::FUNC || first_token == Tokens::ID::SUB) && line_tokens.size() > 1) {
                current_scope = to_upper(line_tokens[1].second);
            }
            else if (first_token == Tokens::ID::ENDFUNC || first_token == Tokens::ID::ENDSUB) {
                current_scope = "GLOBAL";
            }

            static const std::unordered_map<std::string, std::string> block_pairs = { {"IF", "ENDIF"}, {"FOR", "NEXT"}, {"DO", "LOOP"}, {"SUB", "ENDSUB"}, {"FUNC", "ENDFUNC"}, {"TYPE", "ENDTYPE"}, {"SWITCH", "ENDSWITCH"} };
            if (block_pairs.count(first_kw_upper)) {
                if (first_kw_upper == "IF" && is_single_line_if(vm, linter_compiler, line)) {}
                else {
                    block_stack.push_back({ first_kw_upper, line_num });
                }
            }
            else {
                for (const auto& pair : block_pairs) {
                    if (first_kw_upper == pair.second) {
                        if (block_stack.empty() || block_stack.back().first != pair.first) {
                            messages.push_back({ line_num, "'" + first_token_pair.second + "' without a matching '" + pair.first + "'." });
                        }
                        else { block_stack.pop_back(); }
                    }
                }
            }

            bool is_comparison_context = (first_token == Tokens::ID::IF || first_token == Tokens::ID::WHILE || first_token == Tokens::ID::UNTIL);

            for (size_t i = 0; i < line_tokens.size(); ++i) {
                auto& [token, buffer] = line_tokens[i];
                std::string upper_buffer = to_upper(buffer);

                bool is_var_type = (token == Tokens::ID::VARIANT || token == Tokens::ID::STRVAR || token == Tokens::ID::ARRAY_ACCESS || token == Tokens::ID::MAP_ACCESS);
                if (!is_var_type || KeywordRepository::is_keyword(upper_buffer) || user_funcs.count(upper_buffer)) continue;

                // If on FUNC/SUB line, skip params as they are declarations
                if ((first_token == Tokens::ID::FUNC || first_token == Tokens::ID::SUB) &&
                    (current_scope != "GLOBAL" && user_funcs.count(current_scope) && user_funcs.at(current_scope).params.count(upper_buffer))) {
                    continue;
                }

                // Handle dotted access (e.g., Player.X)
                size_t dot_pos = upper_buffer.find('.');
                if (dot_pos != std::string::npos) {
                    std::string base_var_name = upper_buffer.substr(0, dot_pos);
                    VarInfo* base_v_info = (current_scope != "GLOBAL" && user_funcs.count(current_scope) && user_funcs.at(current_scope).locals.count(base_var_name))
                        ? &user_funcs.at(current_scope).locals.at(base_var_name)
                        : &global_vars[base_var_name];
                    base_v_info->read_lines.push_back(line_num);
                }

                VarInfo* v_info = (current_scope != "GLOBAL" && user_funcs.count(current_scope) && user_funcs.at(current_scope).locals.count(upper_buffer))
                    ? &user_funcs.at(current_scope).locals.at(upper_buffer)
                    : &global_vars[upper_buffer];

                if (first_token == Tokens::ID::DIM) { if (i > 0) { v_info->decl_line = line_num; } continue; }

                bool is_write = false;
                if ((first_token == Tokens::ID::FOR || first_token == Tokens::ID::FOR_EACH) && i == 1) { is_write = true; }
                else if (!is_comparison_context && i > 0 && line_tokens[i - 1].first == Tokens::ID::C_EQ) { is_write = false; } // it's a comparison
                else if (!is_comparison_context && i + 1 < line_tokens.size() && line_tokens[i + 1].first == Tokens::ID::C_EQ) { is_write = true; }
                else if (first_token == Tokens::ID::INPUT) { is_write = true; }


                if (is_write) v_info->write_lines.push_back(line_num);
                else v_info->read_lines.push_back(line_num);
            }
        }

        // Pass 3: Report issues
        if (!block_stack.empty()) {
            for (const auto& block : block_stack) { messages.push_back({ block.second, "Unclosed block: '" + block.first + "' started here is never closed." }); }
        }

        auto check_vars = [&](const std::unordered_map<std::string, VarInfo>& vars, const std::string& scope_name) {
            for (const auto& pair : vars) {
                const auto& name = pair.first;
                const auto& info = pair.second;
                if (info.read_lines.empty()) {
                    std::string msg_prefix = (scope_name == "GLOBAL") ? "" : "In function '" + scope_name + "': ";
                    if (info.is_param) {
                        messages.push_back({ info.decl_line, msg_prefix + "Unused parameter '" + name + "' is never read." });
                    }
                    else if (info.decl_line > 0 && info.write_lines.empty()) {
                        messages.push_back({ info.decl_line, msg_prefix + "Unused variable '" + name + "' is declared but never used." });
                    }
                    else if (!info.write_lines.empty()) {
                        messages.push_back({ info.write_lines[0], msg_prefix + "Write-only variable '" + name + "' is assigned a value but never read." });
                    }
                }
            }
            };

        check_vars(global_vars, "GLOBAL");
        for (const auto& func_pair : user_funcs) {
            check_vars(func_pair.second.locals, func_pair.first);
        }

        // Final Report
        TextIO::print("--- Lint Report ---"); TextIO::nl();
        if (messages.empty()) {
            TextIO::print("No issues found. Your code is clean!"); TextIO::nl();
        }
        else {
            std::sort(messages.begin(), messages.end());
            for (const auto& msg : messages) {
                TextIO::print("Line " + std::to_string(msg.line) + ": " + msg.message); TextIO::nl();
            }
            TextIO::nl();
            TextIO::print("Found " + std::to_string(messages.size()) + " potential issue(s)."); TextIO::nl();
        }
        return false;
    }
}

static BasicValue builtin_pretty(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    bool preview = false;
    bool style_set = false;
    bool style_vb = vm.pretty_keywords_vb;
    int  width_set = 0;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = to_upper(to_string(args[i]));
        if (a == "PREVIEW") { preview = true; continue; }
        if (a == "STYLE") {
            if (i + 1 >= args.size()) { Error::set(1, vm.runtime_current_line, "PRETTY STYLE requires UPPER or VB"); return false; }
            std::string v = to_upper(to_string(args[i + 1]));
            if (v == "UPPER") { style_vb = false; style_set = true; i++; continue; }
            if (v == "VB") { style_vb = true;  style_set = true; i++; continue; }
            Error::set(1, vm.runtime_current_line, "Unknown PRETTY STYLE (use UPPER or VB)"); return false;
        }
        if (a == "WIDTH") {
            if (i + 1 >= args.size()) { Error::set(1, vm.runtime_current_line, "PRETTY WIDTH requires a number"); return false; }
            int w = static_cast<int>(to_double(args[i + 1]));
            if (w < 0 || w > 16) { Error::set(1, vm.runtime_current_line, "PRETTY WIDTH must be 0..16"); return false; }
            width_set = w; i++; continue;
        }
    }

    if (style_set) vm.pretty_keywords_vb = style_vb;
    if (width_set) vm.pretty_indent_width = width_set;

    if (vm.source_lines.empty()) {
        TextIO::print("(no source loaded)"); TextIO::nl();
        return false;
    }

    std::vector<std::string> pretty_lines;
    int indent = 0;

    for (const auto& line : vm.source_lines) {
        auto toks = tokenize_code_line(line);
        IndentDelta d = indent_change_for(toks);
        indent += d.before;
        if (indent < 0) indent = 0;

        pretty_lines.push_back(render_pretty_line(toks, indent, vm.pretty_indent_width, vm.pretty_keywords_vb));

        indent += d.after;
    }

    if (preview) {
        for (const auto& l : pretty_lines) { TextIO::print(l); TextIO::nl(); }
    }
    else {
        vm.source_lines = std::move(pretty_lines);
        TextIO::print("Formatted."); TextIO::nl();
    }

    return false;
}


// Register PRETTY in your builtins registry:
void register_better_code_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate) {
    // Helper lambda to make registration cleaner
    auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        table_to_populate[to_upper(info.name)] = info;
        };
    // --- Register Procedures ---
    auto register_proc = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        info.is_procedure = true; // Mark this as a procedure
        table_to_populate[to_upper(info.name)] = info;
        };

    register_proc("PRETTY", -1, builtin_pretty);
    register_proc("LINT", -1, builtin_lint);
}
