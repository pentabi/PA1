/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/*
Please specify the group members here

# Student #1: Tabito Satoh
# Student #2: Leo Yang
# Student #3:

*/

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd; /* File descriptor for the epoll instance, used for monitoring
                     events on the socket. */
    int socket_fd; /* File descriptor for the client socket connected to the
                      server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages
                            sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on
                            RTT and total messages. */
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with
 * the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] =
        "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    // Hint 1: register the "connected" client_thread's socket in the its epoll
    // instance Hint 2: use gettimeofday() and "struct timeval start, end" to
    // record timestamp, which can be used to calculated RTT.

    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);

    for (int i = 0; i < num_requests; i++) {
        gettimeofday(&start, NULL);
        send(data->socket_fd, send_buf, MESSAGE_SIZE, 0);

        int num_events = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
        if (num_events > 0) {
            recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0);
            gettimeofday(&end, NULL);
            long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL +
                            (end.tv_usec - start.tv_usec);
            data->total_rtt += rtt;
            data->total_messages++;
        }
    }
    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a
 * server, collect performance data of each threads, and compute aggregated
 * metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;

    // Hint: use thread_data to save the created socket and epoll instance for
    // each thread You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        thread_data[i].epoll_fd = epoll_create1(0);
        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;

        struct sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

        connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr));
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0;

    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        for (int i = 0; i < num_client_threads; i++) {
            close(thread_data[i].socket_fd);
            close(thread_data[i].epoll_fd);
        }
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
    }

    total_request_rate = (float)total_messages / ((float)total_rtt / 1000000.0);
    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);

    int epoll_fd = epoll_create1(0);
    struct epoll_event event, events[MAX_EVENTS];
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    /* Server's run-to-completion event loop */
    while (1) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
            } else {
                char buffer[MESSAGE_SIZE];
                recv(events[i].data.fd, buffer, MESSAGE_SIZE, 0);
                send(events[i].data.fd, buffer, MESSAGE_SIZE, 0);
                close(events[i].data.fd);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf(
            "Usage: %s <server|client> [server_ip server_port "
            "num_client_threads num_requests]\n",
            argv[0]);
    }

    return 0;
}