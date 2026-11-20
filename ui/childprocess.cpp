#include "childprocess.h"
#include "uihelper.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace UI{
namespace {
    constexpr int kParentWatchFd = 3;
}

    int ChildProcess::lunch(const std::string& program,
               const std::vector<std::string>& args){
        if (isRunning()){
            return 0;
        }
        if (pid_ > 0 && reaped_){
            waitpid(pid_, nullptr, 0);
            pid_ = -1;
            reaped_ = false;
        }
        if (parentPipe_ != -1){
            close(parentPipe_);
            parentPipe_ = -1;
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& arg : args){
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        std::vector<char*> env;
        for (char** variable = environ; *variable; ++variable){
            env.push_back(*variable);
        }
        std::string parentFdVariable = "WALLPAPER_PARENT_FD=" +
                                       std::to_string(kParentWatchFd);
        env.push_back(const_cast<char*>(parentFdVariable.data()));
        env.push_back(nullptr);

        int parentPipe[2] = {-1, -1};
        posix_spawn_file_actions_t actions;
        bool useActions = false;
        bool useParentPipe = pipe(parentPipe) == 0;
        if (useParentPipe){
            int flags = fcntl(parentPipe[0], F_GETFL);
            if (flags == -1 ||
                fcntl(parentPipe[0], F_SETFL, flags | O_NONBLOCK) == -1){
                close(parentPipe[0]);
                close(parentPipe[1]);
                useParentPipe = false;
            }
        }

        if (useParentPipe){
            if (posix_spawn_file_actions_init(&actions) != 0){
                close(parentPipe[0]);
                close(parentPipe[1]);
                useParentPipe = false;
            } else {
                useActions = true;
                posix_spawn_file_actions_adddup2(&actions, parentPipe[0],
                                                 kParentWatchFd);
                posix_spawn_file_actions_addclose(&actions, parentPipe[0]);
                posix_spawn_file_actions_addclose(&actions, parentPipe[1]);
            }
        }

        int err = posix_spawn(&pid_, program.c_str(),
                              useActions ? &actions : nullptr, nullptr,
                              argv.data(), env.data());
        if (useActions){
            posix_spawn_file_actions_destroy(&actions);
        }

        if (err != 0) {
            pid_ = -1;
            reaped_ = false;
            if (useParentPipe){
                close(parentPipe[0]);
                close(parentPipe[1]);
            }

            return -1;
        }

        if (useParentPipe){
            close(parentPipe[0]);
            parentPipe_ = parentPipe[1];
        }
        reaped_ = false;
        return 0;
    }

    ChildProcess::~ChildProcess(){
        if (parentPipe_ != -1){
            close(parentPipe_);
        }
    }

    int ChildProcess::stop(int timeoutMs){
        if (pid_ <= 0 || reaped_) return -1;
        kill(pid_, SIGTERM);

        int status;
        pid_t ret = waitpid(pid_, &status, WNOHANG);
        if (ret == pid_){
            reaped_ = true;
            pid_ = -1;
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }

        // 简单超时等待
        for (int i = 0; i < timeoutMs; i += 50){
            ret = waitpid(pid_, &status, WNOHANG);
            if (ret == pid_){
                reaped_ = true;
                pid_ = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
        reaped_ = true;
        pid_ = -1;
        if (parentPipe_ != -1){
            close(parentPipe_);
            parentPipe_ = -1;
        }

        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    bool ChildProcess::pollExit(int* exitCode){
        if (pid_ <= 0 || reaped_) return true;

        int status;
        pid_t ret = waitpid(pid_, &status, WNOHANG);
        if (ret == pid_){
            reaped_ = true;
            pid_ = -1;
            if (exitCode){
                if (WIFEXITED(status)){
                    *exitCode = WEXITSTATUS(status);
                } else{
                    *exitCode = -1;
                }
            }
            return true;
        }
        return false;
    }
}
