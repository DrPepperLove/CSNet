#define _POSIX_C_SOURCE 200809L
#include "rtp.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#define MAXTRY 50
#define SEQ_MAX 2147463647 // IntMax - 20000
#define MAX_WINDOW_SIZE 20001
#define TIMEOUT_MS 100      // 超时时间 100 毫秒
#define MAX_EVENTS 3

typedef struct Sender_State{
    int base;               // 窗口的起始序列号 (等待 ACK 的最小序列号)
    int nextseqnum;         // 下一个将要发送的分组序列号
    rtp_packet_t window[MAX_WINDOW_SIZE]; // 发送窗口缓存
    int acked[MAX_WINDOW_SIZE]; // 记录每个分组是否被确认 SR 使用
} Sender_State_t;


typedef struct cmd{
    char *receiver_ip;
    uint16_t receiver_port;
    char *file_path;
    uint32_t window_size;
    uint8_t mode;
}cmd_t;

cmd_t parse_cmd(int argc, char **argv) {
    cmd_t cmd;
    cmd.receiver_ip = argv[1];
    cmd.receiver_port = (uint16_t)atoi(argv[2]);
    cmd.file_path = argv[3];
    cmd.window_size = (uint32_t)atoi(argv[4]);
    cmd.mode = (uint8_t)atoi(argv[5]);
    return cmd;
}

int recv_with_timeout(int socketfd, void *buf, size_t len, int flags,
                      struct sockaddr *src_addr, socklen_t *addrlen,
                      int timeout_sec, int timeout_usec) {
    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = timeout_usec;

    if (setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        LOG_FATAL("Failed to set socket receive timeout\n");
        return -1;
    }
    return recvfrom(socketfd, buf, len, flags, src_addr, addrlen);
}

int next_seq(int seq){
    if(seq == SEQ_MAX){
        return 0;
    }
    return seq + 1;
}

int in_window(int seq, int base, int window_size){
    if(base + window_size <= SEQ_MAX){
        return seq >= base && seq < base + window_size;
    }else{
        return (seq >= base && seq <= SEQ_MAX) || (seq >= 0 && seq < (base + window_size) % (SEQ_MAX + 1));
    }
}

// 三次握手建立连接
int ConnectSocket(int socketfd, struct sockaddr_in *servaddr) {
    srand((unsigned int)time(NULL));
    int random_x = rand() % 10000;
    rtp_header_t syn_header;
    syn_header.flags = RTP_SYN;
    syn_header.seq_num = random_x;
    syn_header.length = 0;
    syn_header.checksum = 0;  // set the "checksum" field to 0 before computing
    syn_header.checksum = compute_checksum(&syn_header, sizeof(rtp_header_t));
    int tries = 0;
    while (tries < MAXTRY) {
        ssize_t n = sendto(socketfd, &syn_header, sizeof(rtp_header_t), 0,
                           (const struct sockaddr *)servaddr, sizeof(*servaddr)); // 第一次握手
        if (n < 0) {
            LOG_DEBUG("Failed to send SYN packet\n");
            tries++;
            continue;
        }
        
        ssize_t len;
        rtp_header_t ack_header;
        len = recv_with_timeout(socketfd, &ack_header, sizeof(rtp_header_t), 0, NULL, NULL, 0, 100*1000); // 第二次握手
        if (len < 0) {
            LOG_DEBUG("Failed to receive ACK packet\n");
            tries++;
            continue;
        }
        uint32_t received_checksum = ack_header.checksum;
        ack_header.checksum = 0;
        uint32_t computed_checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));    
        if (received_checksum != computed_checksum) {                              // 检查checksum
            LOG_DEBUG("Received corrupted ACK packet, bad checksum, try times %d\n", tries + 1);
            tries++;
            continue;
        }
        if(ack_header.flags != (RTP_ACK | RTP_SYN) || ack_header.seq_num != random_x + 1){
            LOG_DEBUG("Received invalid ACK packet, try times %d, flags is %d, seq_num is %d\n",
                                         tries + 1, ack_header.flags, ack_header.seq_num);
            tries++;
            continue;
        }
        break;
    }
    if(tries == MAXTRY){
        LOG_DEBUG("Failed to establish connection after %d tries\n", MAXTRY);
        return -1;
    }
    tries = 0;
    while(tries < MAXTRY){
        rtp_header_t final_ack;
        final_ack.flags = RTP_ACK;
        final_ack.seq_num = random_x + 1;
        final_ack.length = 0;
        final_ack.checksum = 0;
        final_ack.checksum = compute_checksum(&final_ack, sizeof(rtp_header_t));
        ssize_t n = sendto(socketfd, &final_ack, sizeof(rtp_header_t), 0,
                           (const struct sockaddr *)servaddr, sizeof(*servaddr)); // 第三次握手
        if (n < 0) {
            LOG_DEBUG("Failed to send final ACK packet\n");
            tries++;
            continue;
        }
        ssize_t len = recv_with_timeout(socketfd, NULL, 0, 0, NULL, NULL, 2, 0); // 观察对方是否收到最后的ACK
        if(len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){ // 超时，说明对方已经收到最后的ACK
            return final_ack.seq_num;
        }
        else{
            LOG_DEBUG("Receiver did not confirm final ACK, send again\n");
            tries++;
        }
    }
    
    LOG_DEBUG("Failed to confirm connection after %d tries\n", MAXTRY);
    return -1;
    
}

// 二次挥手关闭连接
int CloseConnection(int socketfd, struct sockaddr_in *servaddr, int seq_num) {
    rtp_header_t fin_header;
    fin_header.flags = RTP_FIN;
    fin_header.seq_num = seq_num;
    fin_header.length = 0;
    fin_header.checksum = 0;
    fin_header.checksum = compute_checksum(&fin_header, sizeof(rtp_header_t));
    int tries = 0;
    while(tries < MAXTRY){
        ssize_t n = sendto(socketfd, &fin_header, sizeof(rtp_header_t), 0,
                           (const struct sockaddr *)servaddr, sizeof(*servaddr)); // 发送FIN包
        if (n < 0) {
            LOG_DEBUG("Failed to send FIN packet\n");
            tries++;
            continue;
        }
        ssize_t len;
        rtp_header_t ack_header;
        len = recv_with_timeout(socketfd, &ack_header, sizeof(rtp_header_t), 0, NULL, NULL, 0, 100*1000); // 等待ACK包
        if (len < 0) {
            LOG_DEBUG("Failed to receive ACK packet for FIN\n");
            tries++;
            continue;
        }
        uint32_t received_checksum = ack_header.checksum;
        ack_header.checksum = 0;
        uint32_t computed_checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));    
        if (received_checksum != computed_checksum) {                              // 检查checksum
            LOG_DEBUG("Received corrupted ACK packet for FIN, bad checksum, try again...\n");
            tries++;
            continue;
        }
        if(ack_header.flags != (RTP_ACK | RTP_FIN) || ack_header.seq_num != seq_num){
            LOG_DEBUG("Received invalid ACK packet for FIN, flags is %d, seq_num is %d ,try again...\n",
                                                     ack_header.flags, ack_header.seq_num);
            tries++;
            continue;
        }
        break;
    }
    if(tries == MAXTRY){
        LOG_DEBUG("Failed to close connection after %d tries\n", MAXTRY);
        return -1;
    }
    return 0;
}

// 创建并返回 timerfd 文件描述符
static int create_timer_fd() {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (tfd == -1) {
        perror("timerfd_create");
    }
    return tfd;
}

// 启动定时器
static int start_timer(int tfd) {
    struct itimerspec its;
    memset(&its, 0, sizeof(its));

    // 设置超时时间
    its.it_value.tv_sec  = TIMEOUT_MS / 1000;
    its.it_value.tv_nsec = (long)(TIMEOUT_MS % 1000) * 1000000L;
    // it_interval 保持为 0，确保是单次定时器

    if (timerfd_settime(tfd, 0, &its, NULL) == -1) {
        perror("timerfd_settime start");
        return -1;
    }
    return 0;
}

// 停止定时器 (将它设置为 0 超时时间)
static int stop_timer(int tfd) {
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    // it_value 和 it_interval 均为 0，禁用定时器
    if (timerfd_settime(tfd, 0, &its, NULL) == -1) {
        perror("timerfd_settime stop");
        return -1;
    }
    return 0;
}

// 尝试清空 timerfd 中的积压超时计数，避免误触发
static void drain_timerfd(int tfd) {
    uint64_t expirations;
    for (;;) {
        ssize_t n = read(tfd, &expirations, sizeof(expirations));
        if (n == -1) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }
        if (n == 0) break;
    }
}

static int init_epoll(int tfd, int socketfd) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        LOG_DEBUG("epoll_create1");
        return -1;
    }

    struct epoll_event ev;

    // 1. 添加 timerfd
    ev.events = EPOLLIN;
    ev.data.fd = tfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tfd, &ev) == -1) {
        LOG_DEBUG("epoll_ctl tfd\n"); goto err_cleanup;
    }

    // 2. 添加网络套接字 (接收 ACK)
    ev.events = EPOLLIN;
    ev.data.fd = socketfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socketfd, &ev) == -1) {
        LOG_DEBUG("epoll_ctl sockfd\n"); goto err_cleanup;
    }

    return epoll_fd;

err_cleanup:
    close(epoll_fd);
    return -1;
}

int Send_with_GBN(int socketfd, struct sockaddr_in *servaddr, int seq_num, 
                    int window_size, int epoll_fd, FILE* file , int tfd, Sender_State_t* GBN_state) {
    
    struct epoll_event events[MAX_EVENTS];
    int input_eof = 0; 
    int i;

    for (;;) {
        // --- 1. 检查是否完成 ---
        if (input_eof && GBN_state->base == GBN_state->nextseqnum) {
            break;
        }

        // --- 1.5 先尽力填充窗口 ---
        if (!input_eof) {
            while (in_window(GBN_state->nextseqnum, GBN_state->base, window_size) && !input_eof) {

                rtp_packet_t *pkt = &GBN_state->window[GBN_state->nextseqnum % MAX_WINDOW_SIZE];

                size_t bytes_read = fread(pkt->payload, 1, PAYLOAD_MAX, file);

                if (bytes_read > 0) {
                    pkt->rtp.seq_num = GBN_state->nextseqnum;
                    pkt->rtp.length = (uint16_t)bytes_read;
                    pkt->rtp.flags = 0;
                    pkt->rtp.checksum = 0;
                    pkt->rtp.checksum = compute_checksum(pkt, sizeof(rtp_header_t) + bytes_read);

                    size_t pkt_len = sizeof(rtp_header_t) + (size_t)bytes_read;
                    sendto(socketfd, pkt, pkt_len, 0,
                           (const struct sockaddr *)servaddr, sizeof(*servaddr));

                    if (GBN_state->base == GBN_state->nextseqnum) {
                        start_timer(tfd);
                    }
                    LOG_DEBUG("[SEND] Sent packet seq_num=%d, length=%d\n", 
                              pkt->rtp.seq_num, pkt->rtp.length);
                    GBN_state->nextseqnum = next_seq(GBN_state->nextseqnum);
                }
                else if (bytes_read == 0) {
                    // 文件读取完毕
                    LOG_DEBUG("End of input file reached\n");
                    input_eof = 1;
                    break;
                }
                else { // bytes_read == -1
                    LOG_DEBUG("Error reading from file\n");
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    // 其他错误：停止填充，等待后续事件
                    break;
                }
            }

        }

        // 设置 epoll_wait 超时：如果文件已读完，且窗口内还有未确认分组，设置有限超时等待 ACK/重传；否则无限等待。
        int timeout = TIMEOUT_MS * 2;
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout);

        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        // --- 2. 处理事件 ---
        for (i = 0; i < nfds; i++) {
            int current_fd = events[i].data.fd;
            
            // ---  定时器超时事件 (tfd) ---
            if (current_fd == tfd && (events[i].events & EPOLLIN)) {
                uint64_t expirations;
                if (read(tfd, &expirations, sizeof(expirations)) == -1) {
                    perror("read timerfd");
                    continue;
                }
                
                LOG_DEBUG("[TIMEOUT] Timer expired. 重传所有未确认分组 (%d to %d)\n", 
                       GBN_state->base, GBN_state->nextseqnum - 1);
                
                // GBN 重传逻辑：从 base 开始重传
                int current_base = GBN_state->base;
                while (current_base != GBN_state->nextseqnum) {
                    rtp_packet_t *pkt = &GBN_state->window[current_base % MAX_WINDOW_SIZE];
                    size_t pkt_len = sizeof(rtp_header_t) + (size_t)pkt->rtp.length;
                    sendto(socketfd, pkt, pkt_len, 0,
                           (const struct sockaddr *)servaddr, sizeof(*servaddr));

                    current_base = next_seq(current_base);
                }
                
                // 重新启动定时器
                start_timer(tfd);
            }

            // --- 网络套接字事件 (接收 ACK) ---
            else if (current_fd == socketfd && (events[i].events & EPOLLIN)) {
                rtp_header_t ack_header;
                
                ssize_t len = recvfrom(socketfd, (void *)&ack_header, sizeof(rtp_header_t), 0, NULL, NULL);
                if(len < 0){
                    LOG_DEBUG("Failed to receive ACK packet\n");
                    continue;
                }

                uint32_t received_checksum = ack_header.checksum;
                ack_header.checksum = 0;
                uint32_t computed_checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));    
                if (received_checksum != computed_checksum) {                              // 检查checksum
                    LOG_DEBUG("Received corrupted ACK packet, bad checksum, ignoring...\n");
                    continue;
                }
                if ((ack_header.flags & RTP_ACK) == 0) {
                    LOG_DEBUG("Received packet is not an ACK, ignoring...\n");
                    continue;
                }
                int ack_num = ack_header.seq_num;

                if (in_window(ack_num, next_seq(GBN_state->base), window_size)) {
                    // 确认号 k 表示 [base, k) 均已确认，base 前进到 k
                    // printf("[ACK] Received ACK %d. Base moved from %d to %d.\n", ack_num, GBN_state->base, ack_num);

                    GBN_state->base = ack_num; // 累积确认
                    LOG_DEBUG("[ACK] Received ACK %d. Base moved to %d.\n", ack_num, GBN_state->base);
                    // 重新管理定时器
                    if (GBN_state->base != GBN_state->nextseqnum) {
                        // 清理可能的积压超时事件后重启
                        drain_timerfd(tfd);
                        start_timer(tfd);
                    } else {
                        // 窗口清空，停止定时器
                        stop_timer(tfd);
                        drain_timerfd(tfd);
                    }

                    // ACK 到来，窗口滑动，可能会有新的空间。
                    // 依靠下一次 epoll_wait 循环来检测 input_fd 事件并填充窗口。
                } else {
                    LOG_DEBUG("[ACK] Received old ACK %d. Ignoring.\n", ack_num);
                }
            }
        }
    }
   return GBN_state->nextseqnum;
}

int Send_with_SR(int socketfd, struct sockaddr_in *servaddr, int seq_num, 
                    int window_size, int epoll_fd, FILE* file , int tfd, Sender_State_t* SR_state) {
    struct epoll_event events[MAX_EVENTS];
    int input_eof = 0;
    int i;
    for(;;){
        if(input_eof && SR_state->base == SR_state->nextseqnum){
            break;
        }

        while(!input_eof && in_window(SR_state->nextseqnum, SR_state->base, window_size)){
            rtp_packet_t* pkt = &SR_state->window[SR_state->nextseqnum % MAX_WINDOW_SIZE];

            SR_state->acked[SR_state->nextseqnum % MAX_WINDOW_SIZE] = 0;

            size_t bytes_read = fread(pkt->payload, 1, PAYLOAD_MAX, file);

            if(bytes_read > 0){
                pkt->rtp.seq_num = SR_state->nextseqnum;
                pkt->rtp.length = (uint16_t)bytes_read;
                pkt->rtp.flags = 0;
                pkt->rtp.checksum = 0;
                pkt->rtp.checksum = compute_checksum(pkt, sizeof(rtp_header_t) + bytes_read);

                size_t pkt_len = sizeof(rtp_header_t) + (size_t)bytes_read;
                sendto(socketfd, pkt, pkt_len, 0,
                       (const struct sockaddr *)servaddr, sizeof(*servaddr));

                if(SR_state->base == SR_state->nextseqnum){
                    start_timer(tfd);
                }
                LOG_DEBUG("[SEND] Sent packet seq_num=%d, length=%d\n", 
                          pkt->rtp.seq_num, pkt->rtp.length);
                SR_state->nextseqnum = next_seq(SR_state->nextseqnum);
            }
            else if(bytes_read == 0){
                LOG_DEBUG("End of input file reached\n");
                input_eof = 1;
                break;
            }
            else{ // bytes_read == -1
                LOG_DEBUG("Error reading from file\n");
                if(errno == EINTR) continue;
                if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }
        }

        int timeout = TIMEOUT_MS * 2;
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout);
        if(nfds == -1){
            if(errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for(i = 0; i < nfds; i++){
            int current_fd = events[i].data.fd;
            // 定时器超时事件
            if(current_fd == tfd && (events[i].events & EPOLLIN)){
                uint64_t expirations;
                if(read(tfd, &expirations, sizeof(expirations)) == -1){
                    perror("read timerfd");
                    continue;
                }

                LOG_DEBUG("[TIMEOUT] Timer expired. 重传所有未确认分组 (%d to %d)\n", 
                       SR_state->base, SR_state->nextseqnum - 1);

                int current_seq = SR_state->base;
                while(current_seq != SR_state->nextseqnum){
                    if(SR_state->acked[current_seq % MAX_WINDOW_SIZE] == 0){
                        rtp_packet_t* pkt = &SR_state->window[current_seq % MAX_WINDOW_SIZE];
                        size_t pkt_len = sizeof(rtp_header_t) + (size_t)pkt->rtp.length;
                        sendto(socketfd, pkt, pkt_len, 0,
                               (const struct sockaddr *)servaddr, sizeof(*servaddr));
                    }
                    current_seq = next_seq(current_seq);
                }

                start_timer(tfd);
            }
            else if(current_fd == socketfd && (events[i].events & EPOLLIN)){
                rtp_header_t ack_header;

                ssize_t len = recvfrom(socketfd, (void*)&ack_header, sizeof(rtp_header_t), 0, NULL, NULL);
                if(len < 0){
                    LOG_DEBUG("Failed to receive ACK packet\n");
                    continue;
                }

                uint32_t received_checksum = ack_header.checksum;
                ack_header.checksum = 0;
                uint32_t computed_checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));    
                if(received_checksum != computed_checksum){                              // 检查checksum
                    LOG_DEBUG("Received corrupted ACK packet, bad checksum, ignoring...\n");
                    continue;
                }
                if((ack_header.flags & RTP_ACK) == 0){
                    LOG_DEBUG("Received packet is not an ACK, ignoring...\n");
                    continue;
                }
                int ack_num = ack_header.seq_num;

                if(in_window(ack_num, SR_state->base, window_size)){
                    SR_state->acked[ack_num % MAX_WINDOW_SIZE] = 1;
                    LOG_DEBUG("[ACK] Received ACK %d.\n", ack_num);
                    if(ack_num == SR_state->base){
                        while(SR_state->acked[SR_state->base % MAX_WINDOW_SIZE] == 1){
                            SR_state->base = next_seq(SR_state->base);
                        }
                        if(SR_state->base != SR_state->nextseqnum){
                            drain_timerfd(tfd);
                            start_timer(tfd);
                        }else{
                            stop_timer(tfd);
                            drain_timerfd(tfd);
                        }
                    }
                }else{
                    LOG_DEBUG("[ACK] Received out-of-window ACK %d. Ignoring.\n", ack_num);
                }
            }
        }
    }
    return SR_state->nextseqnum;
}


int main(int argc, char **argv) {
    if (argc != 6) {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }
    cmd_t cmd = parse_cmd(argc, argv);
    int sockfd = -1;
    int tfd = -1;
    int epoll_fd = -1;
    struct sockaddr_in servaddr;
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        LOG_FATAL("Socket creation failed\n");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(cmd.receiver_port);
    inet_pton(AF_INET, cmd.receiver_ip, &servaddr.sin_addr);
    int seq_num = 0;
    // 建立连接
    if((seq_num = ConnectSocket(sockfd, &servaddr)) < 0){
        LOG_FATAL("Failed to establish connection with receiver\n");
    }

    // --- 初始化 文件读写 ---
    FILE *file = fopen(cmd.file_path, "rb");
    if (file == NULL) {
        LOG_FATAL("Failed to open file: %s\n", cmd.file_path);
    }

    // --- 初始化 timerfd ---
    tfd = create_timer_fd();
    if (tfd == -1) {
        goto cleanup;
    }

    // --- 初始化 epoll ---
    epoll_fd = init_epoll(tfd, sockfd);
    if (epoll_fd == -1) {
        goto cleanup;
    }

    // 发送数据
    if(cmd.mode == 0){
        // 初始化 GBN 发送状态
        Sender_State_t* GBN_state = (Sender_State_t*)malloc(sizeof(Sender_State_t));
        if(GBN_state == NULL){
            LOG_FATAL("Failed to allocate memory for GBN sender state\n");
        }
        GBN_state->base = seq_num;
        GBN_state->nextseqnum = seq_num;

        LOG_DEBUG("Sender mode: GBN\n");
        seq_num = Send_with_GBN(sockfd, &servaddr, seq_num, cmd.window_size, epoll_fd, file, tfd, GBN_state);
        free(GBN_state);
    }else{
        Sender_State_t* SR_state = (Sender_State_t*)malloc(sizeof(Sender_State_t));
        if(SR_state == NULL){
            LOG_FATAL("Failed to allocate memory for SR sender state\n");
        }
        SR_state->base = seq_num;
        SR_state->nextseqnum = seq_num;
        memset(SR_state->acked, 0, sizeof(SR_state->acked));

        LOG_DEBUG("Sender mode: SR\n");
        seq_num = Send_with_SR(sockfd, &servaddr, seq_num, cmd.window_size, epoll_fd, file, tfd, SR_state);
        free(SR_state);
    }

    if(CloseConnection(sockfd, &servaddr, seq_num) < 0){
        LOG_FATAL("Failed to close connection with receiver\n");
    }
    LOG_DEBUG("Sender: exiting...\n");

cleanup:
    if (tfd != -1) close(tfd);
    if (sockfd != -1) close(sockfd);
    if (epoll_fd != -1) close(epoll_fd);
    if (file != NULL) fclose(file);

    return 0;
}
