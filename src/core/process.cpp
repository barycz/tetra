#include "process.hpp"

#include <csignal>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tetra {

bool spawn_detached(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        return false;
    }
    // Everything the children need is prepared before fork().
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str())); // NOLINT: execvp's signature
    }
    args.push_back(nullptr);

    int pipe_fds[2]; // NOLINT: reports exec failure back to us
    if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }
    if (child == 0) {
        setsid();
        if (fork() != 0) {
            _exit(0); // the grandchild is adopted by init
        }
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int sig = 1; sig < NSIG; ++sig) {
            signal(sig, SIG_DFL); // NOLINT
        }
        const int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        close(pipe_fds[0]);
        execvp(args.front(), args.data());
        const char failed = 1;
        [[maybe_unused]] auto n = write(pipe_fds[1], &failed, 1);
        _exit(127);
    }

    close(pipe_fds[1]);
    waitpid(child, nullptr, 0); // the intermediate child exits immediately
    // The pipe closes on successful exec (O_CLOEXEC) or carries one byte.
    char failed = 0;
    const auto n = read(pipe_fds[0], &failed, 1);
    close(pipe_fds[0]);
    return n == 0;
}

} // namespace tetra
