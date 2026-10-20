#pragma once

#include <cstdint>
#include <string>

class VM;

// NODE.* and TOPIC.* builtins: the Node Bus (doc/design/node_bus_plan.md).
// Called from register_net_builtins, so it is part of the NET flag.
void register_node_builtins(VM& vm);

// The table `jdBasic --nodes` prints: every node in the directory.
std::string node_bus_report();

// For an observer outside a program (the MCP server): the other nodes with
// their topics, and the next value of one topic (waits at most timeout_ms).
// The observer joins as "mcp" on first use and never becomes the default node.
std::string node_observer_peers();
std::string node_observer_peek(const std::string& topic, int64_t timeout_ms);
