/*
    SPDX-FileCopyrightText: 2017 Maxim Golov <maxim.golov@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <atomic>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

std::atomic<bool> g_exit(false);

void* run_signal_thread(void*)
{
    // Unblock interesting signals for this thread only
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    if (sigprocmask(SIG_SETMASK, &mask, nullptr) < 0) {
        perror("failed to set signal mask");
        abort();
    }

    timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100 * 1000 * 1000;

    do {
        int sig = sigtimedwait(&mask, nullptr, &timeout);
        if (sig < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else {
                perror("signal wait failed");
                abort();
            }
        } else if (sig == SIGQUIT || sig == SIGINT || sig == SIGTERM) {
            g_exit = true;
        }
    } while (!g_exit);

    return nullptr;
}

int main()
{
    pthread_t signal_thread;

    // when tracked by heaptrack, this will initialize our background thread
    // without the signal mask set. thus this thread will handle the signal
    // and kill the whole application then
    char* p = new char[1000];

    // block all signals for this thread
    sigset_t mask;
    sigfillset(&mask);
    int ret = pthread_sigmask(SIG_SETMASK, &mask, nullptr);
    if (ret < 0) {
        perror("failed to block signals");
        abort();
    }

    ret = pthread_create(&signal_thread, nullptr, &run_signal_thread, nullptr);
    if (ret < 0) {
        perror("failed to create signal handler thread");
        abort();
    }

    fprintf(stderr, "Started, press Ctrl-C to abort\n");

    // main loop
    while (!g_exit) {
        usleep(1000 * 1000);
    }

    fprintf(stderr, "Interrupted\n");

    ret = pthread_join(signal_thread, nullptr);
    if (ret < 0) {
        perror("failed to join the signal handler thread");
        abort();
    }

    delete[] p;

    fprintf(stderr, "Done.\n");

    return 0;
}
