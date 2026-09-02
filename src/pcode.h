// Compiled programs on disk: the chunk the compiler produced, written
// out and read back without going near the lexer or the parser.
//
// This is what makes a board able to run a program it could never have
// translated. On a Fruit Jam 16 KB of source takes fifty seconds to
// compile and the growth is quadratic; the same program as p-code loads
// in the time it takes to read the file. Compile on the desktop, copy
// the .jdpb across, RUN it.
//
// The format is explicit about every width and little-endian on both
// ends, because the two ends are an x64 desktop and a 32-bit ARM. It
// carries the opcode count, so a file built against a different
// instruction set is refused rather than executed as rubbish.

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "bytecode.h"

// "JDPB", then the format revision, then the opcode count.
extern const char PCODE_MAGIC[4];
#define PCODE_FORMAT_VERSION 1

// True when the file begins with the magic. Cheap: reads four bytes.
bool pcode_is_file(const std::string& path);

bool pcode_write(std::ostream& out, const Chunk& main_chunk,
                 const std::vector<FuncProto>& funcs, std::string& err);

bool pcode_read(std::istream& in, Chunk& main_chunk,
                std::vector<FuncProto>& funcs, std::string& err);
