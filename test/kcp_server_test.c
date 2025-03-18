#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>
#include "ikcp.h"

#define DEFAULT_PORT 9999
#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 3 // 超时时间（秒）
#define DEFAULT_CONV 1 // 默认通道号，与客户端一致
#define DEFAULT_BUSY_POLL_US 50

// 客户端数据结构
typedef struct client_node {
    struct sockaddr_in client_addr; // 客户端地址
    ikcpcb *kcp;                    // KCP 对象
    time_t last_active;             // 上次活跃时间
    pthread_spinlock_t spinlock;    // 自旋锁
    int sock;                       // 服务端 socket 引用
    struct client_node *next;       // 链表下一节点
} client_node_t;

// 服务端数据结构
typedef struct {
    int sock;
    int running;
    client_node_t *clients;         // 客户端链表
    pthread_spinlock_t list_lock;   // 保护客户端链表的自旋锁
} server_data_t;

// KCP 输出回调
static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    client_node_t *client = (client_node_t *)user;
    return sendto(client->sock, buf, len, 0, (struct sockaddr *)&client->client_addr, sizeof(client->client_addr));
}

// 查找客户端，若不存在则创建
static client_node_t *find_or_create_client(server_data_t *server, struct sockaddr_in *addr) {
    pthread_spin_lock(&server->list_lock);
    client_node_t *client = server->clients;
    client_node_t *prev = NULL;

    // 查找现有客户端
    while (client) {
        if (memcmp(&client->client_addr, addr, sizeof(*addr)) == 0) {
            client->last_active = time(NULL); // 更新活跃时间
            pthread_spin_unlock(&server->list_lock);
            return client;
        }
        prev = client;
        client = client->next;
    }

    // 不存在则创建新客户端
    client = (client_node_t *)malloc(sizeof(client_node_t));
    if (!client) {
        pthread_spin_unlock(&server->list_lock);
        return NULL;
    }
    client->client_addr = *addr;
    client->kcp = ikcp_create(DEFAULT_CONV, client); // 使用固定的通道号
    client->kcp->output = udp_output;
    client->sock = server->sock;
    ikcp_nodelay(client->kcp, 1, 10, 2, 1); // 加速模式
    client->kcp->rx_minrto = 10;
    ikcp_wndsize(client->kcp, 128, 128);
    client->last_active = time(NULL);
    pthread_spin_init(&client->spinlock, PTHREAD_PROCESS_PRIVATE);
    client->next = NULL;

    if (prev) {
        prev->next = client;
    } else {
        server->clients = client;
    }
    pthread_spin_unlock(&server->list_lock);
    return client;
}

// 删除超时的客户端
static void cleanup_clients(server_data_t *server) {
    pthread_spin_lock(&server->list_lock);
    client_node_t *prev = NULL;
    client_node_t *client = server->clients;
    time_t now = time(NULL);

    while (client) {
        if (now - client->last_active > TIMEOUT_SEC) {
            client_node_t *next = client->next;
            if (prev) {
                prev->next = next;
            } else {
                server->clients = next;
            }
            ikcp_release(client->kcp);
            printf("Client %s:%d disconnected\n", inet_ntoa(client->client_addr.sin_addr), ntohs(client->client_addr.sin_port));
            pthread_spin_destroy(&client->spinlock);
            free(client);
            client = next;
        } else {
            prev = client;
            client = client->next;
        }
    }
    pthread_spin_unlock(&server->list_lock);
}

// 封装 nanosleep 以处理信号中断
void safe_nanosleep(const long sleep_ns) {
    struct timespec rem;
    struct timespec req;
    req.tv_sec = sleep_ns / 1000000000;
    req.tv_nsec = sleep_ns % 1000000000;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}
uint32_t get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// KCP 更新线程
void *kcp_update_thread(void *arg) {
    server_data_t *server = (server_data_t *)arg;
    uint32_t current_time;
    long min_sleep_ns;

    while (server->running) {
        min_sleep_ns = 0;

        pthread_spin_lock(&server->list_lock);
        client_node_t *client = server->clients;
        while (client) {
            current_time = get_current_time();
            pthread_spin_lock(&client->spinlock);
            uint32_t next_update_time = ikcp_check(client->kcp, current_time);
            pthread_spin_unlock(&client->spinlock);

            if(next_update_time == current_time)
            {
                pthread_spin_lock(&client->spinlock);
                ikcp_update(client->kcp, current_time);
                pthread_spin_unlock(&client->spinlock);
            } else {
                long sleep_ns = (next_update_time - current_time) * 1000000;
                if (sleep_ns < min_sleep_ns) {
                    min_sleep_ns = sleep_ns;
                }
            }
            client = client->next;
        }
        pthread_spin_unlock(&server->list_lock);

        // 清理超时的客户端
        cleanup_clients(server);
        
        safe_nanosleep(min_sleep_ns);
    }
    return NULL;
}

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-p <port>] [-b [<busy_poll_us>]] [-e]\r\n", prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    int port = DEFAULT_PORT;
    int c;
    char udp_buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int enable_busy_poll = 0;
    int busy_poll_us = DEFAULT_BUSY_POLL_US;
    int enable_epoll = 0;

    while ((c = getopt(argc, argv, "p:b::e")) != -1) {
        switch (c) {
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Invalid port number.\r\n");
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
            case 'e':
                enable_epoll = 1;
                break;
            default:
                usage(argv[0]);
        }
    }

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (enable_busy_poll) {
        if (geteuid() != 0) {
            fprintf(stderr, "Error: SO_BUSY_POLL requires root privileges. Please run as root.\r\n");
            close(sock);
            return -1;
        }
        setsockopt(sock, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\r\n", port);

    // 初始化服务端数据
    server_data_t server;
    server.sock = sock;
    server.running = 1;
    server.clients = NULL;
    pthread_spin_init(&server.list_lock, PTHREAD_PROCESS_PRIVATE);

    // 启动 KCP 更新线程
    pthread_t update_thread;
    if (pthread_create(&update_thread, NULL, kcp_update_thread, &server) != 0) {
        perror("Failed to create KCP update thread");
        close(sock);
        exit(EXIT_FAILURE);
    }

    int epfd;
    struct epoll_event ev, events[1];
    if (enable_epoll) {
        epfd = epoll_create1(0);
        ev.events = EPOLLIN;
        ev.data.fd = server.sock;
        epoll_ctl(epfd, EPOLL_CTL_ADD, server.sock, &ev);
    }

    // 主线程处理 KCP 数据接收和回传
    char buffer[BUFFER_SIZE];
    int need_sleep = 0;
    while (server.running) {
        ssize_t n;
        if (enable_epoll) {
            int nfds = epoll_wait(epfd, events, 1, -1);
            if (nfds > 0)
                n = recvfrom(server.sock, udp_buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addrlen);
        } else {
            n = recvfrom(server.sock, udp_buffer, BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *)&client_addr, &addrlen);
        }

        if (n > 0) {
            client_node_t *client = find_or_create_client(&server, &client_addr);
            if (client) {
                pthread_spin_lock(&client->spinlock);
                ikcp_input(client->kcp, udp_buffer, n);
                pthread_spin_unlock(&client->spinlock);
            }

            pthread_spin_lock(&server.list_lock);
            client = server.clients;
            while (client) {
                pthread_spin_lock(&client->spinlock);
                int hr = ikcp_recv(client->kcp, buffer, BUFFER_SIZE);
                if (hr > 0) {
                    ikcp_send(client->kcp, buffer, hr);
                    ikcp_flush(client->kcp); // 立即发送
                    client->last_active = time(NULL); // 更新活跃时间
                }
                pthread_spin_unlock(&client->spinlock);
                client = client->next;
            }
            pthread_spin_unlock(&server.list_lock);
            need_sleep = 0;
        } else {
            need_sleep++;
        }
        if (need_sleep > 1000)
        {
            usleep(100);
            need_sleep = 0;
        }
    }

    server.running = 0;
    pthread_join(update_thread, NULL);

    // 清理所有客户端
    client_node_t *client = server.clients;
    while (client) {
        client_node_t *next = client->next;
        ikcp_release(client->kcp);
        pthread_spin_destroy(&client->spinlock);
        free(client);
        client = next;
    }
    pthread_spin_destroy(&server.list_lock);
    close(sock);
    return 0;
}