#pragma once
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#include <vector>
#include <string>

#define DEFAULT_WAIT_TIMES 3000
extern char **environ;
namespace UI{
    class ChildProcess{
        public:
            ChildProcess() : pid_(-1) {}
            ~ChildProcess();

            pid_t pid() const { return pid_; }
            bool isRunning() const { return pid_ > 0 && !reaped_; }

            int lunch(const std::string& program,
               const std::vector<std::string>& args);

            int stop(int timeoutMs);

            bool pollExit(int* exitCode);

        private:
            pid_t pid_;
            bool reaped_ = false;
            int parentPipe_ = -1;
    };
}
