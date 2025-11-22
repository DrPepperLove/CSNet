#include "rtp.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#define MAXTRY 50
#define MAX_WINDOW_SIZE 20001
#define SEQ_MAX 2147463647 // IntMax - 20000

typedef struct Receive_State{
    int base;               // 窗口的起始序列号 (等待 ACK 的最小序列号)
    int nextseqnum;         // 下一个将要发送的分组序列号
    rtp_packet_t window[MAX_WINDOW_SIZE]; // 发送窗口缓存
    int acked[MAX_WINDOW_SIZE]; // 记录每个分组是否被确认 SR 使用
} Receive_State_t;


typedef struct cmd{
    uint16_t listen_port;
    char *file_path;
    uint32_t window_size;
    uint8_t mode;
}cmd_t;

cmd_t parse_cmd(int argc, char **argv) {
    cmd_t cmd;
    cmd.listen_port = (uint16_t)atoi(argv[1]);
    cmd.file_path = argv[2];
    cmd.window_size = (uint32_t)atoi(argv[3]);
    cmd.mode = (uint8_t)atoi(argv[4]);
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

int ConnectSocket(int socketfd, struct sockaddr_in *cliaddr) {
    rtp_header_t syn_header;
    socklen_t addrlen = sizeof(cliaddr);
    while(1){
        ssize_t n = recv_with_timeout(socketfd, &syn_header, sizeof(rtp_header_t), 0,
                           (struct sockaddr *)cliaddr, &addrlen, 5, 0); // 等待SYN包
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1; // 超时，返回失败
        }
        else if(n < 0){
            LOG_DEBUG("Failed to receive SYN packet\n");
            continue;
        }
        uint32_t received_checksum = syn_header.checksum;
        syn_header.checksum = 0;
        uint32_t computed_checksum = compute_checksum(&syn_header, sizeof(rtp_header_t));    
        if (received_checksum != computed_checksum) {                              // 检查checksum
            LOG_DEBUG("Received corrupted SYN packet, bad checksum, try again...\n");
            continue;
        }
        if(syn_header.flags != RTP_SYN){
            LOG_DEBUG("Received invalid SYN packet, flags is %d, try again...\n", syn_header.flags);
            continue;
        }
        break;
    }
    int tries = 0;
    while(tries < MAXTRY){
        rtp_header_t ack_header;
        ack_header.flags = RTP_ACK | RTP_SYN;
        ack_header.seq_num = syn_header.seq_num + 1;
        ack_header.length = 0;
        ack_header.checksum = 0;
        ack_header.checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));
        ssize_t n = sendto(socketfd, &ack_header, sizeof(rtp_header_t), 0,
                           (const struct sockaddr *)cliaddr, sizeof(*cliaddr)); // 发送ACK包
        if (n < 0) {
            LOG_DEBUG("Failed to send ACK packet\n");
            tries++;
            continue;
        }
        
        ssize_t len;
        rtp_header_t final_ack;
        len = recv_with_timeout(socketfd, &final_ack, sizeof(rtp_header_t), 0, NULL, NULL, 0, 100*1000); // 等待最后的ACK包
        if (len < 0) {
            LOG_DEBUG("Failed to receive final ACK packet\n");
            tries++;
            continue;
        }
        uint32_t received_checksum = final_ack.checksum;
        final_ack.checksum = 0;
        uint32_t computed_checksum = compute_checksum(&final_ack, sizeof(rtp_header_t));
        if (received_checksum != computed_checksum) {                              // 检查checksum
            LOG_DEBUG("Received corrupted final ACK packet, bad checksum, try again...\n");
            tries++;
            continue;
        }
        if(final_ack.flags != RTP_ACK || final_ack.seq_num != syn_header.seq_num + 1){
            LOG_DEBUG("Received invalid final ACK packet, flags is %d, seq_num is %d ,try again...\n", final_ack.flags, final_ack.seq_num);
            tries++;
            continue;
        }
        return final_ack.seq_num; // 连接建立成功
    }
    return -1; // 超过最大尝试次数，连接失败
}

int CloseConnection(int socketfd, struct sockaddr_in *cliaddr, int seq_num){
    rtp_header_t ack_header;
    ack_header.flags = RTP_FIN | RTP_ACK;
    ack_header.seq_num = seq_num;
    ack_header.length = 0;
    ack_header.checksum = 0;
    ack_header.checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));
    while(1){
        ssize_t n = sendto(socketfd, &ack_header, sizeof(rtp_header_t), 0,
                           (const struct sockaddr *)cliaddr, sizeof(*cliaddr)); // 发送ACK包
        if (n < 0) {
            LOG_DEBUG("Failed to send ACK packet\n");
            continue;
        }
        ssize_t len;
        rtp_header_t ack_header;
        len = recv_with_timeout(socketfd, &ack_header, sizeof(rtp_header_t), 0, NULL, NULL, 2, 0); // 等待client的FIN包
        if (len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            LOG_DEBUG("Timeout, client exit\n");
            return 0;
        }        
    }
}

int Receive_with_GBN(int socketfd, struct sockaddr_in *cliaddr, int seq_num, int window_size, FILE* fp){
    rtp_packet_t packet;
    int next_expected_seq = seq_num;
    
    while(1){
        int len = recv_with_timeout(socketfd, &packet, sizeof(rtp_packet_t), 0, 
                                    NULL, NULL, 5, 0); // 等待数据包
        if (len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            LOG_FATAL("Timeout, no data received for 5 seconds, exiting...\n");
            return -1;
        }
        else if(len < 0){
            LOG_DEBUG("Failed to receive data packet\n");
            continue;
        }
        
        // 先检查长度合法性，防止越界
        if(packet.rtp.length > PAYLOAD_MAX){
            LOG_DEBUG("Received packet with invalid length %u (max %u), ignoring...\n", 
                     packet.rtp.length, PAYLOAD_MAX);
            // GBN：对非法包也要发 ACK（期望序号）
            goto send_ack;
        }
        
        uint32_t received_checksum = packet.rtp.checksum;
        packet.rtp.checksum = 0;
        uint32_t computed_checksum = compute_checksum(&packet, sizeof(rtp_header_t) + packet.rtp.length);    
        if (received_checksum != computed_checksum) {
            LOG_DEBUG("Received corrupted data packet (seq %d), bad checksum, ignoring...\n", 
                     packet.rtp.seq_num);
            // GBN：校验失败也要发 ACK（期望序号）
            goto send_ack;
        }
        
        // 收到 FIN 包
        if(packet.rtp.flags == RTP_FIN) {
            if(packet.rtp.seq_num == next_expected_seq){
                // 按序 FIN，文件传输完成
                if(fflush(fp) != 0){
                    LOG_DEBUG("Warning: failed to flush/close file properly\n");
                }
                LOG_DEBUG("Received FIN packet, file transfer complete\n");
                return packet.rtp.seq_num;
            } else {
                // 乱序 FIN（不应该出现在 GBN 中，但处理以防万一）
                LOG_DEBUG("Received out-of-order FIN (seq %d, expected %d), ignoring...\n",
                         packet.rtp.seq_num, next_expected_seq);
                goto send_ack;
            }
        }
        
        // 收到数据包
        if(packet.rtp.seq_num == next_expected_seq && packet.rtp.flags == 0){ 
            // 按序数据包
            LOG_DEBUG("Received expected packet %d, writing to file\n", packet.rtp.seq_num);
            size_t written = fwrite(packet.payload, 1, packet.rtp.length, fp);
            if(written != packet.rtp.length){
                // 写入失败，文件可能损坏
                LOG_DEBUG("ERROR: fwrite failed, expected %u bytes, wrote %zu bytes\n",
                         packet.rtp.length, written);
                fclose(fp);
                return -1;
            }
            next_expected_seq = next_seq(next_expected_seq);
        } else {
            // 乱序包或重复包
            LOG_DEBUG("Received out-of-order packet (seq %d, expected %d), discarding...\n",
                     packet.rtp.seq_num, next_expected_seq);
        }
        
send_ack:
        // GBN：无论收到什么包（正确/错误/乱序），都发送当前期望序号的 ACK（累积确认）
        LOG_DEBUG("Sending ACK for seq %d\n", next_expected_seq);
        rtp_header_t ack_header;
        ack_header.flags = RTP_ACK;
        ack_header.seq_num = next_expected_seq;
        ack_header.length = 0;
        ack_header.checksum = 0;
        ack_header.checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));
        sendto(socketfd, &ack_header, sizeof(rtp_header_t), 0,
              (const struct sockaddr *)cliaddr, sizeof(*cliaddr));
    }
}

int Receive_with_SR(int socketfd, struct sockaddr_in *cliaddr, int seq_num, int window_size, FILE* fp){

    Receive_State_t* SR_state;
    SR_state = (Receive_State_t*)malloc(sizeof(Receive_State_t));
    SR_state->base = seq_num;
    SR_state->nextseqnum = seq_num;
    memset(SR_state->acked, 0, sizeof(SR_state->acked));

    rtp_packet_t packet;
    while(1){
        int len = recv_with_timeout(socketfd, &packet, sizeof(rtp_packet_t), 0, 
                                    NULL, NULL, 5, 0); // 等待数据包
        if (len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            LOG_FATAL("Timeout, no data received for 5 seconds, exiting...\n");
            free(SR_state);
            return -1;
        }
        else if(len < 0){
            LOG_DEBUG("Failed to receive data packet\n");
            continue;
        }
        
        // 先检查长度合法性，防止越界
        if(packet.rtp.length > PAYLOAD_MAX){
            LOG_DEBUG("Received packet with invalid length %u (max %u), ignoring...\n", 
                     packet.rtp.length, PAYLOAD_MAX);
            continue;
        }
        
        uint32_t received_checksum = packet.rtp.checksum;
        packet.rtp.checksum = 0;
        uint32_t computed_checksum = compute_checksum(&packet, sizeof(rtp_header_t) + packet.rtp.length);    
        if (received_checksum != computed_checksum) {
            LOG_DEBUG("Received corrupted data packet (seq %d), bad checksum, ignoring...\n", 
                     packet.rtp.seq_num);
            continue;
        }
        
        // 收到 FIN 包
        if(packet.rtp.flags == RTP_FIN) {
            if(packet.rtp.seq_num == SR_state->base){
                // 按序 FIN，文件传输完成
                if(fflush(fp) != 0){
                    LOG_DEBUG("Warning: failed to flush/close file properly\n");
                }
                LOG_DEBUG("Received FIN packet, file transfer complete\n");
                free(SR_state);
                return packet.rtp.seq_num;
            } else {
                // 乱序 FIN
                LOG_DEBUG("Received out-of-order FIN (seq %d, expected %d), ignoring...\n",
                         packet.rtp.seq_num, SR_state->base);
                continue;
            }
        }
        
        // 收到数据包
        if(in_window(packet.rtp.seq_num, SR_state->base, window_size) && packet.rtp.flags == 0){ 
            // 窗口内数据包
            if(SR_state->acked[packet.rtp.seq_num % MAX_WINDOW_SIZE] == 0){
                // 未接收过
                LOG_DEBUG("Received expected packet %d, writing to file\n", packet.rtp.seq_num);
                
                SR_state->acked[packet.rtp.seq_num % MAX_WINDOW_SIZE] = 1;
                SR_state->window[packet.rtp.seq_num % MAX_WINDOW_SIZE] = packet;
                // 写入所有按序数据包
                if(packet.rtp.seq_num == SR_state->base){
                    int current_seq = SR_state->base;
                    while(SR_state->acked[current_seq % MAX_WINDOW_SIZE]){
                        rtp_packet_t* pkt = &SR_state->window[current_seq % MAX_WINDOW_SIZE];
                        size_t written = fwrite(pkt->payload, 1, pkt->rtp.length, fp);
                        if(written != pkt->rtp.length){
                            // 写入失败，文件可能损坏
                            LOG_DEBUG("ERROR: fwrite failed, expected %u bytes, wrote %zu bytes\n",
                                     pkt->rtp.length, written);
                            free(SR_state);
                            return -1;
                        }
                        SR_state->acked[current_seq % MAX_WINDOW_SIZE] = 0; // 清除状态，释放缓存
                        current_seq = next_seq(current_seq);
                    }
                    SR_state->base = current_seq;
                }

            } else {
                // 重复包
                LOG_DEBUG("Received duplicate packet (seq %d), discarding...\n",
                         packet.rtp.seq_num);
            }
        } else {
            // 窗口外包
            LOG_DEBUG("Received out-of-window packet (seq %d, expected base %d), ignoring...\n",
                     packet.rtp.seq_num, SR_state->base);
        }
        // 发送 ACK
        LOG_DEBUG("Sending ACK for seq %d\n", packet.rtp.seq_num);
        rtp_header_t ack_header;
        ack_header.flags = RTP_ACK;
        ack_header.seq_num = packet.rtp.seq_num;
        ack_header.length = 0;
        ack_header.checksum = 0;
        ack_header.checksum = compute_checksum(&ack_header, sizeof(rtp_header_t));
        sendto(socketfd, &ack_header, sizeof(rtp_header_t), 0,
              (const struct sockaddr *)cliaddr, sizeof(*cliaddr));

    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
    }
    cmd_t cmd = parse_cmd(argc, argv);
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(cmd.listen_port);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        LOG_FATAL("Socket creation failed\n");
    }
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        close(sockfd);
        LOG_FATAL("Bind failed\n");
    }
    int seq_num = 0;
    if((seq_num = ConnectSocket(sockfd, &cliaddr)) < 0){
        close(sockfd);
        LOG_FATAL("Failed to establish connection with sender\n");
    }
    FILE *fp = fopen(cmd.file_path, "wb");
    if(fp == NULL){
        LOG_FATAL("Failed to open file %s for writing\n", cmd.file_path);
        return 0; // 确保打开失败时返回
    }

    if(cmd.mode == 0){
        LOG_DEBUG("Reciver mode: GBN\n");
        seq_num = Receive_with_GBN(sockfd, &cliaddr, seq_num, cmd.window_size, fp);
    }else{
        LOG_DEBUG("Reciver mode: SR\n");
        seq_num = Receive_with_SR(sockfd, &cliaddr, seq_num, cmd.window_size, fp);
    }

    if(seq_num < 0){
        close(sockfd);
        fclose(fp);
        unlink(cmd.file_path); // 删除可能已损坏的文件
        LOG_FATAL("File transfer failed\n");
    }

    if(CloseConnection(sockfd, &cliaddr, seq_num) < 0){
        close(sockfd);
        LOG_FATAL("Failed to close connection with sender\n");
    }

    close(sockfd);
    fclose(fp);
    
    LOG_DEBUG("Receiver: exiting...\n");
    return 0;
}
