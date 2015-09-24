// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "android/base/Limits.h"

namespace android {
namespace base {

// Wrapper class for platform-specific threads.
// To create your own thread, define a sub-class of emugl::Thread
// and override its main() method.
//
// For example:
//
//    class MyThread : public emugl::Thread {
//    public:
//        MyThread() : Thread() {}
//
//        virtual intptr_t main() {
//            ... main thread loop implementation
//            return 0;
//        }
//    };
//
//    ...
//
//    // Create new instance, but does not start it.
//    MyThread* thread = new MyThread();
//
//    // Start the thread.
//    thread->start();
//
//    // Wait for thread completion, and gets result into |exitStatus|.
//    int exitStatus;
//    thread->wait(&exitStatus);
//
class Thread {
public:
    // Public constructor.
    Thread();

    // Virtual destructor.
    virtual ~Thread();

    // Override this method in your own thread sub-classes. This will
    // be called when start() is invoked on the Thread instance.
    virtual intptr_t main() = 0;

    // Override this if you need to execute some code after thread has
    // exited main() and is guaranteed not to access any of its members.
    // E.g. if you need to delete a thread object from the same thread
    // it has created
    virtual void onExit() {}

    // Start a thread instance. Return true on success, false otherwise
    // (e.g. if the thread was already started or terminated).
    bool start();

    // Wait for thread termination and retrieve exist status into
    // |*exitStatus|. Return true on success, false otherwise.
    // NOTE: |exitStatus| can be NULL.
    bool  wait(intptr_t *exitStatus);

    // Check whether a thread has terminated. On success, return true
    // and sets |*exitStatus|. On failure, return false.
    // NOTE: |exitStatus| can be NULL.
    bool tryWait(intptr_t *exitStatus);

private:
#ifdef _WIN32
    static DWORD WINAPI thread_main(void* arg);

    HANDLE mThread;
    DWORD mThreadId;
    CRITICAL_SECTION mLock;
#else // !WIN32
    static void* thread_main(void* arg);

    pthread_t mThread;
    pthread_mutex_t mLock;
#endif
    intptr_t mExitStatus;
    bool mIsRunning;
};

}  // namespace base
}  // namespace android
