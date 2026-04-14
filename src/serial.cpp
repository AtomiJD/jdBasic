#ifdef USE_SERIAL
// Serial Communication for jdBasic (Windows)
// Compiled only with /DUSE_SERIAL

#include "value.h"
#include "vm.h"
#include "errors.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <iostream>

// ── Handle storage ───────────────────────────────────────────

static std::unordered_map<int, HANDLE> g_serial_handles;
static std::mutex g_serial_mutex;
static int g_serial_next_id = 1;

// ── Register serial builtins ─────────────────────────────────

void register_serial_builtins(VM& vm) {

    // SERIAL.OPEN(port$, baud_rate) → handle (stored as object with __TYPE__="SERIAL_PORT")
    vm.register_native("SERIAL.OPEN", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string port = args[0].as_string()->data;
        int baud = (int)args[1].to_int();

        // Windows needs \\.\COMn for ports >= COM10, also works for COM1-9
        std::string dev = port;
        if (dev.find("\\\\.\\") == std::string::npos && dev.find("COM") != std::string::npos)
            dev = "\\\\.\\" + port;

        HANDLE hSerial = CreateFileA(dev.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);

        if (hSerial == INVALID_HANDLE_VALUE)
            throw jdError(ErrCode::RUNTIME_ERROR, "Cannot open serial port: " + port);

        // Configure port
        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(hSerial, &dcb)) {
            CloseHandle(hSerial);
            throw jdError(ErrCode::RUNTIME_ERROR, "Cannot get serial state: " + port);
        }

        dcb.BaudRate = baud;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;

        if (!SetCommState(hSerial, &dcb)) {
            CloseHandle(hSerial);
            throw jdError(ErrCode::RUNTIME_ERROR, "Cannot configure serial port: " + port);
        }

        // Set timeouts: non-blocking reads
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 100;
        SetCommTimeouts(hSerial, &timeouts);

        // Flush buffers
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

        // Store handle
        int id;
        { std::lock_guard<std::mutex> lock(g_serial_mutex);
          id = g_serial_next_id++;
          g_serial_handles[id] = hSerial; }

        // Return as object with __TYPE__ = "SERIAL_PORT"
        Value v = Value::make_object();
        v.as_object()->set("__TYPE__", Value::make_string("SERIAL_PORT"));
        v.as_object()->set("__HANDLE__", Value::make_i64(id));
        v.as_object()->set("PORT", Value::make_string(port));
        v.as_object()->set("BAUD", Value::make_i64(baud));
        return v;
    });

    // Helper: extract HANDLE from serial object
    auto get_handle = [](const Value& v) -> HANDLE {
        int id = 0;
        if (v.type == ValueType::OBJECT) {
            Value* h = v.as_object()->get("__HANDLE__");
            if (h) id = (int)h->to_int();
        } else {
            id = (int)v.to_int();
        }
        std::lock_guard<std::mutex> lock(g_serial_mutex);
        auto it = g_serial_handles.find(id);
        if (it == g_serial_handles.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "Invalid serial handle");
        return it->second;
    };

    // SERIAL.CLOSE(handle)
    vm.register_native("SERIAL.CLOSE", 1, 1, [get_handle](const std::vector<Value>& args) -> Value {
        HANDLE h = get_handle(args[0]);
        CloseHandle(h);
        // Remove from map
        int id = 0;
        if (args[0].type == ValueType::OBJECT) {
            Value* hv = args[0].as_object()->get("__HANDLE__");
            if (hv) id = (int)hv->to_int();
        } else { id = (int)args[0].to_int(); }
        { std::lock_guard<std::mutex> lock(g_serial_mutex);
          g_serial_handles.erase(id); }
        return Value::make_none();
    });

    // SERIAL.WRITE(handle, data$)
    vm.register_native("SERIAL.WRITE", 2, 2, [get_handle](const std::vector<Value>& args) -> Value {
        HANDLE h = get_handle(args[0]);
        std::string data = args[1].as_string()->data;
        DWORD written = 0;
        if (!WriteFile(h, data.c_str(), (DWORD)data.size(), &written, NULL))
            throw jdError(ErrCode::RUNTIME_ERROR, "Serial write failed");
        return Value::make_i64(written);
    });

    // SERIAL.READ$(handle, max_bytes) → string
    vm.register_native("SERIAL.READ$", 2, 2, [get_handle](const std::vector<Value>& args) -> Value {
        HANDLE h = get_handle(args[0]);
        int max_bytes = (int)args[1].to_int();
        std::string buf(max_bytes, '\0');
        DWORD bytesRead = 0;
        ReadFile(h, &buf[0], (DWORD)max_bytes, &bytesRead, NULL);
        buf.resize(bytesRead);
        return Value::make_string(buf);
    });

    // SERIAL.AVAILABLE(handle) → number of bytes waiting
    vm.register_native("SERIAL.AVAILABLE", 1, 1, [get_handle](const std::vector<Value>& args) -> Value {
        HANDLE h = get_handle(args[0]);
        DWORD errors;
        COMSTAT stat;
        if (ClearCommError(h, &errors, &stat))
            return Value::make_i64(stat.cbInQue);
        return Value::make_i64(0);
    });

    // SERIAL.FLUSH(handle)
    vm.register_native("SERIAL.FLUSH", 1, 1, [get_handle](const std::vector<Value>& args) -> Value {
        HANDLE h = get_handle(args[0]);
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return Value::make_none();
    });
}

#endif // USE_SERIAL
