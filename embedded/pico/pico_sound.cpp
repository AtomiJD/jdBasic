// PLAY: a melody in the background. An alarm hands the next note to the
// speaker when the current one is up, so the program keeps running while
// it sounds. Notes are parsed at safe points into a small ring; the
// alarm only consumes it, never parses.
//
// The string is the classic BASIC one:
//   A-G      note, with # or + for sharp, - for flat
//   O<n>     octave 0-8 (default 4), < and > step it
//   L<n>     default length, 1 whole 4 quarter 8 eighth (default 4)
//   T<n>     tempo in quarter notes per minute (default 120)
//   P<n>     rest, R is the same
//   .        after a note, hold it half again as long

#include "../../src/vm.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <string>
#include <cstring>

extern "C" void picocalc_snd_tone(int freq);
extern "C" void picocalc_snd_volume(int pct);

#define NOTE_RING 32

struct Note { uint16_t freq; uint16_t ms; };

static Note g_ring[NOTE_RING];
static volatile uint8_t g_head = 0, g_tail = 0;
static volatile bool g_running = false;
static alarm_id_t g_alarm = 0;

static std::string g_score;
static size_t g_cursor = 0;
static int g_octave = 4;
static int g_length = 4;
static int g_tempo = 120;

static inline uint8_t ring_next(uint8_t i) { return (uint8_t)((i + 1) % NOTE_RING); }

// C=0 .. B=11, the semitone each letter sits on.
static const int LETTER_SEMITONE[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G

static int note_freq(int octave, int semitone) {
    // A4 = 440 Hz sits on semitone 9 of octave 4.
    double steps = (double)(semitone - 9) + 12.0 * (double)(octave - 4);
    double f = 440.0;
    // 2^(steps/12) without pulling in pow: halve/double per octave, then
    // twelve roots by repeated multiplication.
    while (steps >= 12.0) { f *= 2.0; steps -= 12.0; }
    while (steps < 0.0)   { f /= 2.0; steps += 12.0; }
    static const double SEMI = 1.0594630943592953; // 2^(1/12)
    for (int i = 0; i < (int)steps; i++) f *= SEMI;
    return (int)(f + 0.5);
}

static int note_ms(int length, int dots) {
    if (length < 1) length = 1;
    // A quarter note is one beat; a whole note is four.
    int ms = (int)(4.0 * 60000.0 / (double)g_tempo / (double)length);
    int add = ms / 2;
    for (int i = 0; i < dots; i++) { ms += add; add /= 2; }
    return ms;
}

static bool ring_push(uint16_t freq, uint16_t ms) {
    uint8_t next = ring_next(g_head);
    if (next == g_tail) return false;
    g_ring[g_head].freq = freq;
    g_ring[g_head].ms = ms;
    g_head = next;
    return true;
}

static int read_number(int fallback) {
    if (g_cursor >= g_score.size() || !isdigit((unsigned char)g_score[g_cursor]))
        return fallback;
    int v = 0;
    while (g_cursor < g_score.size() && isdigit((unsigned char)g_score[g_cursor]))
        v = v * 10 + (g_score[g_cursor++] - '0');
    return v;
}

// Parse forward until the ring is full or the score runs out.
static void score_fill() {
    while (g_cursor < g_score.size()) {
        char c = (char)toupper((unsigned char)g_score[g_cursor]);
        if (c == ' ' || c == ',') { g_cursor++; continue; }
        g_cursor++;

        if (c == 'O') { g_octave = read_number(g_octave); continue; }
        if (c == '>') { if (g_octave < 8) g_octave++; continue; }
        if (c == '<') { if (g_octave > 0) g_octave--; continue; }
        if (c == 'L') { g_length = read_number(g_length); continue; }
        if (c == 'T') { g_tempo = read_number(g_tempo); if (g_tempo < 20) g_tempo = 20; continue; }

        if (c == 'P' || c == 'R') {
            int len = read_number(g_length);
            int dots = 0;
            while (g_cursor < g_score.size() && g_score[g_cursor] == '.') { dots++; g_cursor++; }
            if (!ring_push(0, (uint16_t)note_ms(len, dots))) { g_cursor--; return; }
            continue;
        }

        if (c >= 'A' && c <= 'G') {
            int semi = LETTER_SEMITONE[c - 'A'];
            size_t mark = g_cursor;
            if (g_cursor < g_score.size()) {
                char a = g_score[g_cursor];
                if (a == '#' || a == '+') { semi++; g_cursor++; }
                else if (a == '-') { semi--; g_cursor++; }
            }
            int oct = g_octave;
            if (semi > 11) { semi -= 12; oct++; }
            if (semi < 0)  { semi += 12; oct--; }
            int len = read_number(g_length);
            int dots = 0;
            while (g_cursor < g_score.size() && g_score[g_cursor] == '.') { dots++; g_cursor++; }
            if (!ring_push((uint16_t)note_freq(oct, semi), (uint16_t)note_ms(len, dots))) {
                g_cursor = mark - 1;
                return;
            }
            continue;
        }
        // Anything else is not ours; skip it rather than stop the tune.
    }
}

static int64_t note_alarm(alarm_id_t, void*) {
    if (g_tail == g_head) {
        picocalc_snd_tone(0);
        g_running = false;
        g_alarm = 0;
        return 0;
    }
    Note n = g_ring[g_tail];
    g_tail = ring_next(g_tail);
    picocalc_snd_tone(n.freq);
    return -(int64_t)n.ms * 1000;
}

// Start the alarm if the queue has work and nothing is driving it.
// Interrupts off for the decision: the alarm clears g_running when it
// finds the ring empty, and that must not race with a fresh PLAY.
static void snd_kick() {
    uint32_t save = save_and_disable_interrupts();
    bool idle = !g_running && g_tail != g_head;
    if (idle) g_running = true;
    restore_interrupts(save);
    if (idle) g_alarm = add_alarm_in_ms(1, note_alarm, nullptr, true);
}

static void snd_reset() {
    if (g_alarm) { cancel_alarm(g_alarm); g_alarm = 0; }
    g_running = false;
    g_head = g_tail = 0;
    g_score.clear();
    g_cursor = 0;
    picocalc_snd_tone(0);
}

// Rides on the VM's periodic tick, which runs whether or not the
// program registered event handlers, so a long score keeps feeding.
static void pico_sound_pump() {
    if (g_cursor < g_score.size()) score_fill();
    snd_kick();
}

extern "C" void picocalc_snd_beep(int freq, int ms);

void register_pico_sound(VM& vm) {
    vm.on_tick = []() { pico_sound_pump(); };
    vm.register_native("BEEP", 0, 2, [](const std::vector<Value>& args) -> Value {
        int freq = args.size() >= 1 ? (int)args[0].to_double() : 880;
        int ms   = args.size() >= 2 ? (int)args[1].to_double() : 200;
        snd_reset();
        picocalc_snd_beep(freq, ms);
        return Value();
    });
    vm.register_native("PLAY", 1, 1, [](const std::vector<Value>& args) -> Value {
        snd_reset();
        g_score = args[0].to_string();
        g_cursor = 0;
        g_octave = 4; g_length = 4; g_tempo = 120;
        score_fill();
        snd_kick();
        return Value();
    });
    vm.register_native("PLAY.STOP", 0, 0, [](const std::vector<Value>&) -> Value {
        snd_reset();
        return Value();
    });
    vm.register_native("PLAY.BUSY", 0, 0, [](const std::vector<Value>&) -> Value {
        bool busy = g_running || g_tail != g_head || g_cursor < g_score.size();
        return Value::make_bool(busy);
    });
    vm.register_native("PLAY.VOLUME", 1, 1, [](const std::vector<Value>& args) -> Value {
        picocalc_snd_volume((int)args[0].to_double());
        return Value();
    });
    vm.register_native("TONE", 1, 1, [](const std::vector<Value>& args) -> Value {
        snd_reset();
        picocalc_snd_tone((int)args[0].to_double());
        return Value();
    });
}
