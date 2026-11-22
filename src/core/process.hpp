#pragma once

#include <string>
#include <vector>

namespace tetra {

// Starts a program fully detached from the caller: double fork, new session,
// default signal mask and handlers, stdin from /dev/null. The program keeps
// running if the caller (e.g. the shell) exits or restarts.
// Returns false if it could not be started.
bool spawn_detached(const std::vector<std::string>& argv);

} // namespace tetra
