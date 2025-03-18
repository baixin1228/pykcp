#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <netinet/tcp.h>

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_PORT 8888
#define DEFAULT_TEST_DURATION 5
#define BUFFER_SIZE 1024
#define DEFAULT_BUSY_POLL_US 50

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: [-i <server_ip>] [-p <port>] [-d <test_duration>] [-b [<busy_poll_us>]] [-n]\r\n");
    fprintf(stderr, "  -b: Enable SO_BUSY_POLL (optional value in microseconds, default %d)\r\n", DEFAULT_BUSY_POLL_US);
    fprintf(stderr, "  -n: Enable TCP_NODELAY\r\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    int num = 1;
    struct timespec start_time, end_time;
    double total_rtt = 0;
    int sent_count = 0;
    char server_ip[INET_ADDRSTRLEN] = DEFAULT_SERVER_IP;
    int port = DEFAULT_PORT;
    int test_duration = DEFAULT_TEST_DURATION;
    int enable_busy_poll = 0;
    int busy_poll_us = DEFAULT_BUSY_POLL_US;
    int enable_tcp_nodelay = 0;
    int c;

    while ((c = getopt(argc, argv, "i:p:d:b::n")) != -1) {
        switch (c) {
            case 'i':
                strncpy(server_ip, optarg, INET_ADDRSTRLEN - 1);
                server_ip[INET_ADDRSTRLEN - 1] = '\0';
                break;
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Invalid port number.\r\n");
                    usage(argv[0]);
                }
                break;
            case 'd':
                test_duration = atoi(optarg);
                if (test_duration <= 0) {
                    fprintf(stderr, "Invalid test duration.\r\n");
                    usage(argv[0]);
                }
                break;
            case 'b':
                enable_busy_poll = 1;
                if (optarg) {
                    busy_poll_us = atoi(optarg);
                    if (busy_poll_us <= 0) {
                        fprintf(stderr, "Invalid busy poll value.\r\n");
                        usage(argv[0]);
                    }
                }
                break;
            case 'n':
                enable_tcp_nodelay = 1;
                break;
            default:
                usage(argv[0]);
        }
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation error");
        return -1;
    }

    // 检查 root 权限（仅在启用 SO_BUSY_POLL 时）
    if (enable_busy_poll) {
        if (geteuid() != 0) {
            fprintf(stderr, "Error: SO_BUSY_POLL requires root privileges. Please run as root.\r\n");
            close(sock);
            return -1;
        }
        if (setsockopt(sock, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us)) < 0) {
            perror("Failed to set SO_BUSY_POLL");
            close(sock);
            return -1;
        }
        printf("SO_BUSY_POLL enabled with %d us\r\n", busy_poll_us);
    }

    if (enable_tcp_nodelay) {
        int flag = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
            perror("Failed to set TCP_NODELAY");
            close(sock);
            return -1;
        }
        printf("TCP_NODELAY enabled\r\n");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        close(sock);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    time_t start = time(NULL);
    time_t last_print_time = start;

    while (time(NULL) - start < test_duration) {
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double send_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

        sprintf(buffer, "%d %.9f", num, send_time);
        send(sock, buffer, strlen(buffer), 0);

        memset(buffer, 0, BUFFER_SIZE);
        ssize_t valread = read(sock, buffer, BUFFER_SIZE);
        if (valread > 0) {
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            double recv_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
            double rtt = (recv_time - send_time) * 1000;
            total_rtt += rtt;
            sent_count++;
        }

        num++;

        time_t current_time = time(NULL);
        if (current_time - last_print_time >= 1) {
            if (sent_count > 0) {
                double avg_rtt = total_rtt / sent_count;
                double send_rate = (double)sent_count / (current_time - start);
                printf("At %ld seconds - Average RTT: %.3f ms, Send rate: %.2f packets per second\r\n",
                       current_time - start, avg_rtt, send_rate);
            }
            last_print_time = current_time;
        }
    }

    if (sent_count > 0) {
        double avg_rtt = total_rtt / sent_count;
        double send_rate = (double)sent_count / test_duration;
        printf("Final - Average RTT: %.3f ms, Send rate: %.2f packets per second\r\n", avg_rtt, send_rate);
    }

    close(sock);
    return 0;
}