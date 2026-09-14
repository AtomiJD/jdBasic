#pragma once

class VM;

// NET.* builtins: TCP clients and servers and UDP sockets.
void register_net_builtins(VM& vm);
