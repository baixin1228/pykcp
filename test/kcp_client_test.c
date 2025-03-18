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

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_PORT 9999
#define DEFAULT_TEST_DURATION 5
#define BUFFER_SIZE 1024
#define LOCAL_PORT 10000 // 客户端固定的本地端口
#define DEFAULT_CONV 1   // 默认通道号，与服务端一致
#define DEFAULT_BUSY_POLL_US 50

// 用户数据结构
typedef struct {
    int sock;
    struct sockaddr_in server_addr;
    ikcpcb *kcp;
    int running;
    pthread_spinlock_t spinlock; // 自旋锁
} kcp_userdata_t;

// KCP 输出回调
static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    kcp_userdata_t *ud = (kcp_userdata_t *)user;
    return sendto(ud->sock, buf, len, 0, (struct sockaddr *)&ud->server_addr, sizeof(ud->server_addr));
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
    kcp_userdata_t *ud = (kcp_userdata_t *)arg;

    while (ud->running) {
        // 获取当前时间（单位：毫秒）
        uint32_t current_time = get_current_time();
        // 计算下一次需要调用 ikcp_update 的时间
        pthread_spin_lock(&ud->spinlock);
        uint32_t next_update_time = ikcp_check(ud->kcp, current_time);
        pthread_spin_unlock(&ud->spinlock);

        // 计算需要睡眠的时长（单位：纳秒）
        long sleep_ns = (next_update_time - current_time) * 1000000;
        if (sleep_ns > 0) {
            safe_nanosleep(sleep_ns);
        }

        // 更新当前时间
        current_time = get_current_time();
        // 调用 ikcp_update 更新 KCP 状态
        pthread_spin_lock(&ud->spinlock);
        ikcp_update(ud->kcp, current_time);
        pthread_spin_unlock(&ud->spinlock);
    }
    return NULL;
}

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-i <server_ip>] [-p <port>] [-d <test_duration>] [-b [<busy_poll_us>]] [-e]\r\n", prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server_addr, local_addr;
    char buffer[BUFFER_SIZE] = {0};
    char udp_buffer[BUFFER_SIZE] = {0};
    int num = 1;
    struct timespec start_time, end_time;
    double total_rtt = 0;
    int sent_count = 0;
    char server_ip[INET_ADDRSTRLEN] = DEFAULT_SERVER_IP;
    int port = DEFAULT_PORT;
    int test_duration = DEFAULT_TEST_DURATION;
    int enable_busy_poll = 0;
    int busy_poll_us = DEFAULT_BUSY_POLL_US;
    int enable_epoll = 0;
    int c;

    while ((c = getopt(argc, argv, "i:p:d:b::e")) != -1) {
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
            case 'e':
                enable_epoll = 1;
                break;
            default:
                usage(argv[0]);
        }
    }

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation error");
        return -1;
    }

    if (enable_busy_poll) {
        if (geteuid() != 0) {
            fprintf(stderr, "Error: SO_BUSY_POLL requires root privileges. Please run as root.\r\n");
            close(sock);
            return -1;
        }
        setsockopt(sock, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));
    }

    // 绑定客户端到本地端口
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(LOCAL_PORT);
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind failed");
        close(sock);
        return -1;
    }

    // 设置服务端地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sock);
        return -1;
    }

    // 创建 KCP 对象
    kcp_userdata_t ud;
    ud.sock = sock;
    ud.server_addr = server_addr;
    ud.kcp = ikcp_create(DEFAULT_CONV, (void *)&ud);
    ud.kcp->output = udp_output;
    ikcp_nodelay(ud.kcp, 1, 10, 2, 1); // 加速模式
    /* extreme settings */
    ud.kcp->rx_minrto = 10;
    ikcp_wndsize(ud.kcp, 128, 128);
    ud.running = 1;
    pthread_spin_init(&ud.spinlock, PTHREAD_PROCESS_PRIVATE);

    // 启动 KCP 更新线程
    pthread_t update_thread;
    if (pthread_create(&update_thread, NULL, kcp_update_thread, &ud) != 0) {
        perror("Failed to create KCP update thread");
        ikcp_release(ud.kcp);
        close(sock);
        return -1;
    }

    // 主线程负责发送和接收测试数据
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    time_t start = time(NULL);
    time_t last_print_time = start;

    int epfd;
    struct epoll_event ev, events[1];
    if (enable_epoll) {
        epfd = epoll_create1(0);
        ev.events = EPOLLIN;
        ev.data.fd = ud.sock;
        epoll_ctl(epfd, EPOLL_CTL_ADD, ud.sock, &ev);
    }

    int need_sleep = 0;
    while (time(NULL) - start < test_duration) {
        // 发送数据
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double send_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
        sprintf(buffer, "%d %.9f", num, send_time);
        pthread_spin_lock(&ud.spinlock);
        ikcp_send(ud.kcp, buffer, strlen(buffer));
        ikcp_flush(ud.kcp); // 立即发送
        pthread_spin_unlock(&ud.spinlock);

        // 检查接收数据
        while(1)
        {
            ssize_t n;
            if (enable_epoll) {
                int nfds = epoll_wait(epfd, events, 1, -1);
                if (nfds > 0)
                    n = recvfrom(ud.sock, udp_buffer, BUFFER_SIZE, 0, NULL, NULL);
            } else {
                n = recvfrom(ud.sock, udp_buffer, BUFFER_SIZE, MSG_DONTWAIT, NULL, NULL);
            }

            if (n > 0) {
                pthread_spin_lock(&ud.spinlock);
                ikcp_input(ud.kcp, udp_buffer, n);
                int hr = ikcp_recv(ud.kcp, buffer, BUFFER_SIZE);
                pthread_spin_unlock(&ud.spinlock);
                if (hr > 0) {
                    clock_gettime(CLOCK_MONOTONIC, &end_time);
                    double recv_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
                    double rtt = (recv_time - send_time) * 1000;
                    total_rtt += rtt;
                    sent_count++;
                    break;
                }
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

    ud.running = 0;
    pthread_join(update_thread, NULL);
    pthread_spin_destroy(&ud.spinlock);
    ikcp_release(ud.kcp);
    close(sock);
    return 0;
}