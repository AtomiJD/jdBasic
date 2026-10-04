// VM builtins: files, directories, CSV and paths.

#include "vm_internal.h"
#include <cstring>
#if !defined(_WIN32)
#include <utime.h>
#endif

// Skips a UTF-8 byte order mark at the start of a stream.
static void skip_utf8_bom(std::istream& f) {
    char b[3] = { 0, 0, 0 };
    f.read(b, 3);
    if (f.gcount() == 3 && (unsigned char)b[0] == 0xEF &&
        (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) return;
    f.clear();
    f.seekg(0);
}

// One CSV record as in RFC 4180: a field that starts with a quote runs to
// the closing quote, may hold the delimiter and line breaks, and writes a
// quote as "". Answers false at the end of the stream.
static bool read_csv_record(std::istream& f, const std::string& delim,
                            std::vector<std::string>& cells) {
    cells.clear();
    std::string line;
    if (!std::getline(f, line)) return false;
    auto chop_cr = [](std::string& s) { if (!s.empty() && s.back() == '\r') s.pop_back(); };
    chop_cr(line);
    const std::string sep = delim.empty() ? std::string(",") : delim;
    std::string cell;
    bool in_quotes = false;
    bool at_start = true;
    size_t i = 0;
    while (true) {
        if (i >= line.size()) {
            if (!in_quotes) break;
            if (!std::getline(f, line)) break;
            chop_cr(line);
            cell += '\n';
            i = 0;
            continue;
        }
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cell += '"'; i += 2; }
                else { in_quotes = false; i++; }
            } else {
                cell += c;
                i++;
            }
        } else if (at_start && c == '"') {
            in_quotes = true;
            at_start = false;
            i++;
        } else if (line.compare(i, sep.size(), sep) == 0) {
            cells.push_back(cell);
            cell.clear();
            at_start = true;
            i += sep.size();
        } else {
            cell += c;
            at_start = false;
            i++;
        }
    }
    cells.push_back(cell);
    return true;
}

void VM::register_file_builtins() {
    // ── File I/O ─────────────────────────────────────────────

    register_native("TXTREADER$", 1, 2, [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string encoding = (args.size() >= 2) ? args[1].as_string()->data : std::string();
        std::ifstream f(fname, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        std::ostringstream ss;
        ss << f.rdbuf();
        return Value::make_string(jdb_enc::decode_to_utf8(ss.str(), encoding));
    });

    register_native("TXTWRITER", 2, 4, [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string content = args[1].as_string()->data;
        bool append = (args.size() >= 3) ? args[2].to_bool() : false;
        std::string encoding = (args.size() >= 4) ? args[3].as_string()->data : std::string();
        std::string out = jdb_enc::encode_from_utf8(content, encoding);
        // Binary mode to match native jdb_txtwriter - avoids \r\n translation
        // so FILE.SIZE returns the same byte count in both runtimes.
        std::ios::openmode mode = std::ios::out | std::ios::binary |
                                   (append ? std::ios::app : std::ios::trunc);
        std::ofstream f(fname, mode);
        if (!f) throw std::runtime_error("Cannot write file: " + fname);
        f.write(out.data(), out.size());
        return Value::make_none();
    });

    register_native("BINREADER$", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::ifstream f(fname, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        size_t sz = f.tellg();
        f.seekg(0);
        std::string data(sz, '\0');
        f.read(&data[0], sz);
        return Value::make_string(data);
    });

    register_native("BINWRITER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::ofstream f(fname, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot write file: " + fname);
        f.write(data.c_str(), data.size());
        return Value::make_none();
    });

    // Text extraction from PDF files (uncompressed, FlateDecode, ASCIIHex/85
    // streams). Encrypted PDFs and image-only pages yield what is decodable.
    // Simple PDFs carry WinAnsi (cp1252) string bytes, so a non-UTF-8 extract
    // is re-decoded as cp1252 to keep umlauts intact.
    register_native("PDF.TEXT$", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::ifstream probe(fname, std::ios::binary);
        if (!probe) throw std::runtime_error("Cannot open file: " + fname);
        probe.close();
        std::string txt = pdf_extract::extract_text(fname);
        auto valid_utf8 = [](const std::string& s) {
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = s[i];
                size_t need;
                if (c < 0x80) need = 0;
                else if ((c >> 5) == 0x6) need = 1;
                else if ((c >> 4) == 0xE) need = 2;
                else if ((c >> 3) == 0x1E) need = 3;
                else return false;
                if (need > 0 && i + need >= s.size()) return false;
                for (size_t k = 1; k <= need; k++)
                    if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return false;
                i += need + 1;
            }
            return true;
        };
        if (!valid_utf8(txt)) txt = jdb_enc::decode_to_utf8(txt, "cp1252");
        std::string clean;
        clean.reserve(txt.size());
        for (unsigned char c : txt) {
            if (c >= 0x20 || c == '\n' || c == '\t') clean += static_cast<char>(c);
        }
        return Value::make_string(clean);
    });

    // "YYYY-MM-DD[ HH:MM:SS]", "YYYY-MM-DDTHH:MM:SS" or "DD.MM.YYYY" →
    // local epoch. Two-digit years are rejected as ambiguous.
    auto parse_csv_date = [](const std::string& s, double& out_epoch) -> bool {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
        int m = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
        if (m < 3) m = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se);
        if (m < 3) {
            h = mi = se = 0;
            m = std::sscanf(s.c_str(), "%d.%d.%d", &d, &mo, &y);
            if (m < 3) return false;
        }
        if (y < 100 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
        std::tm tm = {};
        tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
        tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se; tm.tm_isdst = -1;
        std::time_t t = std::mktime(&tm);
        if (t == (std::time_t)-1) {
            // Outside the CRT's range (pre-1970 on Windows) - UTC civil epoch.
            out_epoch = (double)(jdb_days_from_civil(y, mo, d) * 86400 +
                                 h * 3600 + mi * 60 + se);
            return true;
        }
        out_epoch = static_cast<double>(t);
        return true;
    };

    // One CSV cell → Value, honoring a per-column target type. Type names
    // follow the AS declaration types: AUTO (infer int → double → string),
    // STRING, INTEGER, DOUBLE, DATE, BOOLEAN.
    auto csv_cell_value = [parse_csv_date](const std::string& cell, const std::string& want) -> Value {
        if (want == "STRING") return Value::make_string(cell);
        if (want == "INTEGER") {
            try {
                size_t np;
                double d = std::stod(cell, &np);
                if (np == cell.size()) return Value::make_i64((int64_t)d);
            } catch (...) {}
            return Value::make_i64(0);
        }
        if (want == "DOUBLE") {
            try {
                size_t np;
                double d = std::stod(cell, &np);
                if (np == cell.size()) return Value::make_f64(d);
            } catch (...) {}
            return Value::make_f64(0.0);
        }
        if (want == "DATE") {
            double ep;
            if (parse_csv_date(cell, ep)) return Value::make_date(ep);
            return Value::make_date(0);
        }
        if (want == "BOOLEAN") {
            std::string lo = cell;
            for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
            return Value::make_bool(lo == "1" || lo == "true" || lo == "yes");
        }
        // AUTO / unknown type name: infer.
        try {
            size_t np;
            int64_t iv = std::stoll(cell, &np);
            if (np == cell.size()) return Value::make_i64(iv);
            double dv = std::stod(cell, &np);
            if (np == cell.size()) return Value::make_f64(dv);
        } catch (...) {}
        return Value::make_string(cell);
    };

    register_native("CSVREADER", [csv_cell_value](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string delim = (args.size() >= 2) ? args[1].as_string()->data : ",";
        bool has_header = (args.size() >= 3) ? args[2].to_bool() : false;
        // Optional per-column target types; columns beyond the list infer.
        std::vector<std::string> col_types;
        if (args.size() >= 4 && args[3].type == ValueType::ARRAY) {
            for (auto& t : args[3].as_array()->elements) {
                std::string u = t.to_string();
                for (auto& c : u) c = (char)std::toupper((unsigned char)c);
                col_types.push_back(u);
            }
        }
        std::ifstream f(fname, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        skip_utf8_bom(f);
        Value result = Value::make_array();
        std::vector<std::string> cells;
        bool first = true;
        static const std::string kAuto = "AUTO";
        while (read_csv_record(f, delim, cells)) {
            if (first && has_header) { first = false; continue; }
            first = false;
            Value row = Value::make_array();
            for (size_t col = 0; col < cells.size(); col++) {
                const std::string& want = (col < col_types.size()) ? col_types[col] : kAuto;
                row.as_array()->elements.push_back(csv_cell_value(cells[col], want));
            }
            result.as_array()->elements.push_back(std::move(row));
        }
        return result;
    });

    // Header row as a string array (CSVREADER discards it on skip).
    register_native("CSVHEADER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string delim = (args.size() >= 2) ? args[1].as_string()->data : ",";
        std::ifstream f(fname, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        skip_utf8_bom(f);
        Value r = Value::make_array();
        std::vector<std::string> cells;
        if (!read_csv_record(f, delim, cells)) return r;
        for (auto& cell : cells) r.as_array()->elements.push_back(Value::make_string(cell));
        return r;
    });

    register_native("CSVWRITER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        auto* data = args[1].as_array();
        std::string delim = (args.size() >= 3) ? args[2].as_string()->data : ",";
        std::ofstream f(fname);
        if (!f) throw std::runtime_error("Cannot write file: " + fname);
        // A text field quoted as RFC 4180 asks when it holds the delimiter,
        // a quote or a line break; a quote inside becomes "".
        auto field = [&delim](const std::string& s) {
            if (s.find(delim) == std::string::npos && s.find_first_of("\"\r\n") == std::string::npos)
                return s;
            std::string q = "\"";
            for (char c : s) { if (c == '"') q += '"'; q += c; }
            return q + "\"";
        };
        // Optional header array
        if (args.size() >= 4 && args[3].type == ValueType::ARRAY) {
            auto* hdr = args[3].as_array();
            for (size_t i = 0; i < hdr->elements.size(); i++) {
                if (i > 0) f << delim;
                f << field(hdr->elements[i].to_string());
            }
            f << "\n";
        }
        // Data rows
        for (auto& row : data->elements) {
            if (row.type == ValueType::ARRAY) {
                for (size_t i = 0; i < row.as_array()->elements.size(); i++) {
                    if (i > 0) f << delim;
                    auto& cell = row.as_array()->elements[i];
                    if (cell.type == ValueType::STRING) {
                        f << field(cell.as_string()->data);
                    } else {
                        f << cell.to_string();
                    }
                }
            } else {
                f << row.to_string();
            }
            f << "\n";
        }
        return Value::make_none();
    });

    // ── Filesystem ────────────────────────────────────────────

    register_native("DIR$", [](const std::vector<Value>& args) -> Value {
        std::string pattern = (args.size() >= 1) ? args[0].as_string()->data : "*";
        bool extended = (args.size() >= 2) ? args[1].to_bool() : false;
        Value result = Value::make_array();
#if defined(_WIN32)
        // A name that is a directory lists that directory, the same as it
        // does on the other platforms. Without this the call answers with
        // the directory's own entry, which is a different question than
        // the one anybody asks.
        if (!pattern.empty() && pattern.find_first_of("*?") == std::string::npos) {
            DWORD at = GetFileAttributesA(pattern.c_str());
            if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) {
                char last = pattern[pattern.size() - 1];
                if (last != '\\' && last != '/') pattern += "\\";
                pattern += "*";
            }
        }
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::string name(fd.cFileName);
                if (name == "." || name == "..") continue;
                if (!extended) {
                    result.as_array()->elements.push_back(Value::make_string(name));
                } else {
                    Value row = Value::make_array();
                    row.as_array()->elements.push_back(Value::make_string(name));
                    int64_t size = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    row.as_array()->elements.push_back(Value::make_i64(size));
                    std::string type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "DIR" :
                                       (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? "LINK" : "FILE";
                    row.as_array()->elements.push_back(Value::make_string(type));
                    SYSTEMTIME utc, st; FileTimeToSystemTime(&fd.ftLastWriteTime, &utc);
                    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &st);
                    char dt[32]; snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
                        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    row.as_array()->elements.push_back(Value::make_string(dt));
                    std::string attr;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) attr += "R";
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) attr += "W";
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) attr += "H";
                    row.as_array()->elements.push_back(Value::make_string(attr));
                    result.as_array()->elements.push_back(std::move(row));
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
#else
        // POSIX: opendir + readdir + stat. Pattern-matching uses fnmatch on
        // the basename. If the pattern has no slash and no glob chars, treat
        // it as a directory listing of the cwd.
#ifdef ESP32
        // IDF builds FATFS with FF_FS_RPATH 0, so there is no working
        // directory to be relative to and a bare listing starts at the
        // root. Everywhere else "." is the cwd, as it should be.
        std::string dir_part = "/", base_pat = pattern;
#else
        std::string dir_part = ".", base_pat = pattern;
#endif
        // A name that is a directory lists that directory. Splitting it at
        // the last slash instead would look for a file called "sd" in the
        // root, find nothing, and report an empty listing - which reads as
        // "the directory is empty" rather than "you meant something else".
        struct stat dst;
        if (!pattern.empty() && pattern.find_first_of("*?") == std::string::npos &&
            stat(pattern.c_str(), &dst) == 0 && S_ISDIR(dst.st_mode)) {
            dir_part = pattern;
            base_pat = "*";
        } else {
            size_t sl = pattern.find_last_of('/');
            if (sl != std::string::npos) { dir_part = pattern.substr(0, sl); base_pat = pattern.substr(sl + 1); }
            if (base_pat.empty()) base_pat = "*";
        }
        DIR* d = opendir(dir_part.c_str());
        if (d) {
            struct dirent* de;
            while ((de = readdir(d))) {
                std::string name = de->d_name;
                if (name == "." || name == "..") continue;
                if (fnmatch(base_pat.c_str(), name.c_str(), 0) != 0) continue;
                if (!extended) {
                    result.as_array()->elements.push_back(Value::make_string(name));
                } else {
                    Value row = Value::make_array();
                    row.as_array()->elements.push_back(Value::make_string(name));
                    std::string full = dir_part + "/" + name;
                    struct stat st; int64_t size = 0;
                    std::string type = "FILE", attr = "W";
                    char dt[32] = "";
                    if (stat(full.c_str(), &st) == 0) {
                        size = (int64_t)st.st_size;
                        if (S_ISDIR(st.st_mode)) type = "DIR";
                        else if (S_ISLNK(st.st_mode)) type = "LINK";
                        if ((st.st_mode & S_IWUSR) == 0) attr = "R";
                        struct tm* tmv = localtime(&st.st_mtime);
                        if (tmv) snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
                            tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                            tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
                    }
                    row.as_array()->elements.push_back(Value::make_i64(size));
                    row.as_array()->elements.push_back(Value::make_string(type));
                    row.as_array()->elements.push_back(Value::make_string(dt));
                    row.as_array()->elements.push_back(Value::make_string(attr));
                    result.as_array()->elements.push_back(std::move(row));
                }
            }
            closedir(d);
        }
#endif
        return result;
    });

    register_native("DIR", [this](const std::vector<Value>& args) -> Value {
        std::string pattern = (args.size() >= 1) ? args[0].as_string()->data : "*";
        Value files = call_function("DIR$", {Value::make_string(pattern), Value::make_bool(true)});
        auto* arr = files.as_array();
        for (auto& row : arr->elements) {
            if (row.type != ValueType::ARRAY) continue;
            auto* r = row.as_array();
            std::string type = (r->elements.size() > 2) ? r->elements[2].to_string() : "";
            std::string name = r->elements[0].to_string();
            std::string size_s = (r->elements.size() > 1) ? r->elements[1].to_string() : "";
            std::string date_s = (r->elements.size() > 3) ? r->elements[3].to_string() : "";
#ifdef JDB_MCU
            // The board has no clock worth printing and forty columns to
            // spend: name, a tab, the size.
            if (type == "DIR") {
                emit(name + "\t<DIR>\n");
            } else {
                emit(name + "\t" + size_s + "\n");
            }
#else
            if (type == "DIR") {
                emit(date_s + "    <DIR>          " + name + "\n");
            } else {
                char buf[16]; snprintf(buf, sizeof(buf), "%12s", size_s.c_str());
                emit(date_s + " " + std::string(buf) + " " + name + "\n");
            }
#endif
        }
        emit("  " + std::to_string(arr->elements.size()) + " item(s)\n");
        return Value::make_none();
    });

    register_native("CD", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            // CD without args: just return current dir (no print in script mode)
#if defined(_WIN32)
            char buf[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, buf);
#else
            char buf[4096];
            if (!getcwd(buf, sizeof(buf))) buf[0] = '\0';
#endif
            return Value::make_string(buf);
        }
        std::string path = args[0].to_string();
        if (path.empty()) {
            throw jdError(ErrCode::RUNTIME_ERROR, "CD: empty path");
        }
#if defined(_WIN32)
        if (!SetCurrentDirectoryA(path.c_str()))
            throw jdError(ErrCode::FILE_NOT_FOUND, "Cannot change to directory: " + path);
        char buf[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buf);
        return Value::make_string(buf);
#else
        if (chdir(path.c_str()) != 0)
            throw jdError(ErrCode::FILE_NOT_FOUND, "Cannot change to directory: " + path);
        char buf[4096];
        getcwd(buf, sizeof(buf));
        return Value::make_string(buf);
#endif
    });

    register_native("PWD", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        char buf[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buf);
#else
        char buf[4096];
        if (!getcwd(buf, sizeof(buf))) buf[0] = '\0';
#endif
        return Value::make_string(buf);
    });

    // MKDIR creates every missing parent too, like mkdir -p.
    register_native("MKDIR", [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
        std::string part;
        for (size_t i = 0; i <= path.size(); i++) {
            bool sep = i == path.size() || path[i] == '/' || path[i] == '\\';
            if (sep && !part.empty() && !(part.size() == 2 && part[1] == ':')) {
#if defined(_WIN32)
                CreateDirectoryA(part.c_str(), NULL);
#else
                mkdir(part.c_str(), 0755);
#endif
            }
            if (i < path.size()) part += path[i];
        }
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !(st.st_mode & S_IFDIR))
            throw std::runtime_error("Cannot create directory: " + path);
        return Value::make_none();
    });

    register_native("RMDIR", [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
#if defined(_WIN32)
        if (!RemoveDirectoryA(path.c_str()))
            throw std::runtime_error("Cannot remove directory: " + path);
#else
        if (rmdir(path.c_str()) != 0)
            throw std::runtime_error("Cannot remove directory: " + path);
#endif
        return Value::make_none();
    });

    register_native("KILL", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::ARRAY) {
            for (auto& f : args[0].as_array()->elements) {
                if (std::remove(f.to_string().c_str()) != 0)
                    throw std::runtime_error("Cannot delete: " + f.to_string());
            }
        } else {
            if (std::remove(args[0].as_string()->data.c_str()) != 0)
                throw std::runtime_error("Cannot delete: " + args[0].as_string()->data);
        }
        return Value::make_none();
    });

#ifndef JDB_LEAN
    // FILE.MOVE / FILE.COPY: a target that is an existing directory takes the
    // source's file name; an existing target file is replaced only with
    // overwrite. Both answer TRUE and raise on any failure.
    struct FileTransfer {
        static bool is_dir(const std::string& p) {
#if defined(_WIN32)
            DWORD attr = GetFileAttributesA(p.c_str());
            return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
            struct stat st;
            return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
        }
        static bool exists(const std::string& p) {
#if defined(_WIN32)
            return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
            struct stat st;
            return stat(p.c_str(), &st) == 0;
#endif
        }
        static std::string target(const std::string& src, const std::string& dst) {
            if (!is_dir(dst)) return dst;
            size_t cut = src.find_last_of("/\\");
            std::string name = (cut == std::string::npos) ? src : src.substr(cut + 1);
            if (!dst.empty() && (dst.back() == '/' || dst.back() == '\\')) return dst + name;
            return dst + "/" + name;
        }
        static std::string last_error() {
#if defined(_WIN32)
            DWORD code = GetLastError();
            char buf[256] = {0};
            DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, buf, sizeof(buf), nullptr);
            while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == '.')) buf[--n] = 0;
            return n ? std::string(buf) : ("error " + std::to_string(code));
#else
            return std::strerror(errno);
#endif
        }
        static void copy_bytes(const std::string& src, const std::string& dst) {
            std::ifstream in(src, std::ios::binary);
            if (!in) throw std::runtime_error("Cannot open: " + src);
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("Cannot write: " + dst);
            out << in.rdbuf();
            if (!out) throw std::runtime_error("Cannot write: " + dst);
        }
        static void run(const std::vector<Value>& args, bool move) {
            const char* what = move ? "FILE.MOVE" : "FILE.COPY";
            std::string src = args[0].to_string();
            std::string dst = target(src, args[1].to_string());
            bool overwrite = args.size() >= 3 && args[2].to_bool();
            if (!exists(src)) throw std::runtime_error(std::string(what) + ": no such file: " + src);
            if (!move && is_dir(src)) throw std::runtime_error(std::string(what) + ": is a directory: " + src);
            if (exists(dst) && !overwrite)
                throw std::runtime_error(std::string(what) + ": target exists: " + dst);
#if defined(_WIN32)
            BOOL ok = move
                ? MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH |
                                                        (overwrite ? MOVEFILE_REPLACE_EXISTING : 0))
                : CopyFileA(src.c_str(), dst.c_str(), overwrite ? FALSE : TRUE);
            if (!ok) throw std::runtime_error(std::string(what) + ": " + src + " -> " + dst + ": " + last_error());
#else
            if (move) {
                if (std::rename(src.c_str(), dst.c_str()) == 0) return;
                if (errno != EXDEV)
                    throw std::runtime_error(std::string(what) + ": " + src + " -> " + dst + ": " + last_error());
                if (is_dir(src)) throw std::runtime_error(std::string(what) + ": cannot move a directory across file systems: " + src);
            }
            copy_bytes(src, dst);
            struct stat st;
            if (stat(src.c_str(), &st) == 0) {
                chmod(dst.c_str(), st.st_mode & 07777);
                struct utimbuf times;
                times.actime = st.st_atime;
                times.modtime = st.st_mtime;
                utime(dst.c_str(), &times);
            }
            if (move && std::remove(src.c_str()) != 0)
                throw std::runtime_error(std::string(what) + ": copied but cannot delete " + src + ": " + last_error());
#endif
        }
    };

    register_native("FILE.MOVE", 2, 3, [](const std::vector<Value>& args) -> Value {
        FileTransfer::run(args, true);
        return Value::make_bool(true);
    });

    register_native("FILE.COPY", 2, 3, [](const std::vector<Value>& args) -> Value {
        FileTransfer::run(args, false);
        return Value::make_bool(true);
    });
#endif

    // ── Path Functions ───────────────────────────────────────

#ifndef JDB_LEAN
    register_native("PATH.JOIN$", [](const std::vector<Value>& args) -> Value {
#if defined(_WIN32)
        char sep = '\\';
#else
        char sep = '/';
#endif
        std::string result;
        for (size_t i = 0; i < args.size(); i++) {
            std::string part = args[i].to_string();
            if (i > 0 && !result.empty() && result.back() != sep && result.back() != '/' && result.back() != '\\')
                result += sep;
            result += part;
        }
        return Value::make_string(result);
    });
#endif

#ifndef JDB_LEAN
    register_native("PATH.BASENAME$", [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
        size_t pos = p.find_last_of("/\\");
        return Value::make_string(pos == std::string::npos ? p : p.substr(pos + 1));
    });
#endif

#ifndef JDB_LEAN
    register_native("PATH.EXT$", [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
        size_t slash = p.find_last_of("/\\");
        size_t dot = p.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return Value::make_string("");
        return Value::make_string(p.substr(dot));
    });
#endif

#ifndef JDB_LEAN
    register_native("PATH.DIRNAME$", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
        size_t pos = p.find_last_of("/\\");
        if (pos == std::string::npos) return Value::make_string("");
        // Strip trailing separator unless it's the root.
        if (pos == 0) return Value::make_string(p.substr(0, 1));
        return Value::make_string(p.substr(0, pos));
    });
#endif

#ifndef JDB_LEAN
    register_native("PATH.NORMALIZE$", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
#if defined(_WIN32)
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        // Normalize all separators to native
        std::string s; s.reserve(p.size());
        for (char c : p) s.push_back((c == '/' || c == '\\') ? sep : c);
        // Detect drive prefix like "C:" so we don't mangle it
        std::string prefix;
        std::string body = s;
#if defined(_WIN32)
        if (body.size() >= 2 && isalpha((unsigned char)body[0]) && body[1] == ':') {
            prefix = body.substr(0, 2);
            body = body.substr(2);
        }
#endif
        bool rooted = !body.empty() && body[0] == sep;
        // Split into parts
        std::vector<std::string> parts;
        size_t i = 0;
        while (i < body.size()) {
            while (i < body.size() && body[i] == sep) i++;
            size_t j = i;
            while (j < body.size() && body[j] != sep) j++;
            if (i < j) parts.push_back(body.substr(i, j - i));
            i = j;
        }
        // Resolve . and ..
        std::vector<std::string> out;
        for (auto& part : parts) {
            if (part == ".") continue;
            if (part == "..") {
                if (!out.empty() && out.back() != "..") out.pop_back();
                else if (!rooted) out.push_back("..");
            } else {
                out.push_back(part);
            }
        }
        std::string res = prefix;
        if (rooted) res.push_back(sep);
        for (size_t k = 0; k < out.size(); k++) {
            if (k) res.push_back(sep);
            res += out[k];
        }
        if (res.empty()) res = ".";
        return Value::make_string(res);
    });
#endif

    // ── File metadata ───────────────────────────────────────────
    register_native("FILE.EXISTS", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
#if defined(_WIN32)
        DWORD attr = GetFileAttributesA(p.c_str());
        return Value::make_bool(attr != INVALID_FILE_ATTRIBUTES);
#else
        struct stat st;
        return Value::make_bool(stat(p.c_str(), &st) == 0);
#endif
    });

    register_native("FILE.SIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
#if defined(_WIN32)
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fad))
            return Value::make_i64(-1);
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return Value::make_i64(-1);
        int64_t sz = ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        return Value::make_i64(sz);
#else
        struct stat st;
        if (stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return Value::make_i64(-1);
        return Value::make_i64((int64_t)st.st_size);
#endif
    });

    register_native("FILE.ISDIR", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
#if defined(_WIN32)
        DWORD attr = GetFileAttributesA(p.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) return Value::make_bool(false);
        return Value::make_bool((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
        struct stat st;
        if (stat(p.c_str(), &st) != 0) return Value::make_bool(false);
        return Value::make_bool(S_ISDIR(st.st_mode));
#endif
    });

    register_native("FILE.STAT", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
        Value m = Value::make_object();
        auto* o = m.as_object();
#if defined(_WIN32)
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fad)) {
            o->set("exists", Value::make_bool(false));
            return m;
        }
        bool is_dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int64_t sz = is_dir ? 0 : (((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow);
        o->set("exists", Value::make_bool(true));
        o->set("size", Value::make_i64(sz));
        o->set("is_dir", Value::make_bool(is_dir));
        o->set("readonly", Value::make_bool((fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0));
        o->set("hidden", Value::make_bool((fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0));
        SYSTEMTIME utc, st; FileTimeToSystemTime(&fad.ftLastWriteTime, &utc);
        SystemTimeToTzSpecificLocalTime(nullptr, &utc, &st);
        char dt[32]; snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        o->set("mtime", Value::make_string(dt));
#else
        struct stat st;
        if (stat(p.c_str(), &st) != 0) {
            o->set("exists", Value::make_bool(false));
            return m;
        }
        o->set("exists", Value::make_bool(true));
        o->set("size", Value::make_i64((int64_t)st.st_size));
        o->set("is_dir", Value::make_bool(S_ISDIR(st.st_mode)));
        o->set("readonly", Value::make_bool((st.st_mode & S_IWUSR) == 0));
        o->set("hidden", Value::make_bool(false));
        char dt[32]; struct tm* tmv = localtime(&st.st_mtime);
        if (tmv) {
            snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
                tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
            o->set("mtime", Value::make_string(dt));
        }
#endif
        return m;
    });
}
