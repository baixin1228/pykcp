#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#define DEFAULT_PORT 8888
#define BUFFER_SIZE 1024
#define DEFAULT_BUSY_POLL_US 50

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-p <port>] [-b [<busy_poll_us>]] [-n]\r\n", prog_name);
    fprintf(stderr, "  -b: Enable SO_BUSY_POLL (optional value in microseconds, default %d)\r\n", DEFAULT_BUSY_POLL_US);
    fprintf(stderr, "  -n: Enable TCP_NODELAY\r\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    int port = DEFAULT_PORT;
    int enable_busy_poll = 0;
    int busy_poll_us = DEFAULT_BUSY_POLL_US;
    int enable_tcp_nodelay = 0;
    int c;

    while ((c = getopt(argc, argv, "p:b::n")) != -1) {
        switch (c) {
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Invalid port number.\r\n");
                    usage(argv[0]);
                }
                break;
            case 'b':
                if (geteuid() != 0) {
                    fprintf(stderr, "Error: SO_BUSY_POLL requires root privileges. Please run as root.\r\n");
                    close(new_socket);
                    close(server_fd);
                    exit(EXIT_FAILURE);
                }
                enable_busy_poll = 1;
                if (optarg) {
                    busy_poll_us = atoi(optarg);
                    if (busy_poll_us <= 0) {
                        fprintf(stderr, "Invalid busy poll value.\r\n");
                        usage(argv[0]);
                    }
                }
                printf("SO_BUSY_POLL enabled with %d us for new connection\r\n", busy_poll_us);
                break;
            case 'n':
                enable_tcp_nodelay = 1;
                printf("TCP_NODELAY enabled for new connection\r\n");
                break;
            default:
                usage(argv[0]);
        }
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\r\n", port);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // 检查 root 权限（仅在启用 SO_BUSY_POLL 时）
        if (enable_busy_poll) {
            if (setsockopt(new_socket, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us)) < 0) {
                perror("Failed to set SO_BUSY_POLL");
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
        }

        if (enable_tcp_nodelay) {
            int flag = 1;
            if (setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
                perror("Failed to set TCP_NODELAY");
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
        }

        while (1) {
            ssize_t valread = read(new_socket, buffer, BUFFER_SIZE);
            if (valread <= 0) {
                break;
            }
            send(new_socket, buffer, strlen(buffer), 0);
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}