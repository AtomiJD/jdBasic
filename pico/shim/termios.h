#pragma once
struct termios { unsigned c_lflag; unsigned c_iflag; unsigned c_oflag; unsigned c_cflag; unsigned char c_cc[32]; };
#define ICANON 2
#define ECHO 8
#define TCSANOW 0
#define VMIN 6
#define VTIME 5
static inline int tcgetattr(int f, struct termios* t) { (void)f; (void)t; return -1; }
static inline int tcsetattr(int f, int a, const struct termios* t) { (void)f; (void)a; (void)t; return -1; }
#define ICRNL 0400
#define IXON 02000
#define ISIG 1
#define IEXTEN 0100000
#define ECHONL 0100
#define ONLCR 4
#define OPOST 1
#define BRKINT 2
#define INPCK 020
#define ISTRIP 040
#define CS8 060
