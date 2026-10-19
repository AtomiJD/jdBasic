#pragma once

#include <string>

class VM;

// NODE.* and TOPIC.* builtins: the Node Bus (doc/design/node_bus_plan.md).
// Called from register_net_builtins, so it is part of the NET flag.
void register_node_builtins(VM& vm);

// The table `jdBasic --nodes` prints: every node in the directory.
std::string node_bus_report();
