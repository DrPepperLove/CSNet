#include "define.h"


int Create_OPEN_CONN_REQUEST(struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA1; // OPEN_CONN_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_OPEN_CONN_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA1){ // OPEN_CONN_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_OPEN_CONN_REPLY(struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA2; // OPEN_CONN_REPLY
    header->m_status = 1; // status = 1
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_OPEN_CONN_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA2){ // OPEN_CONN_REPLY
        return -1; // Not match
    }
    if(header->m_status != 1){ // status = 1
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_LIST_REQUEST(struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA3; // LIST_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_LIST_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA3){ // LIST_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_LIST_REPLY(struct MyFtpHeader* header, uint32_t payload_length){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA4; // LIST_REPLY
    header->m_status = 0; // Unused
    header->m_length = htonl(12 + payload_length + 1); // Big End 12 + payload_length
    return 0;
}

int Is_LIST_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA4){ // LIST_REPLY
        return -1; // Not match
    }
    if(ntohl(header->m_length) < 13){ // Big End 12 + payload_length + 1 >= 13
        return -1; // Not match
    }
    return 0; // Match
}

int Create_CHANGE_DIR_REQUEST(struct MyFtpHeader* header, uint32_t payload_length){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA5; // CHANGE_DIR_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12 + payload_length + 1); // Big End 12 + payload_length
    return 0;
}

int Is_CHANGE_DIR_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA5){ // CHANGE_DIR_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) < 13){ // Big End 12 + payload_length + 1 >= 13
        return -1; // Not match
    }
    return 0; // Match
}

int Create_CHANGE_DIR_REPLY(struct MyFtpHeader* header, int exist){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA6; // CHANGE_DIR_REPLY
    header->m_status = exist ? 1 : 0; // status = 1 if exist else 0
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_CHANGE_DIR_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA6){ // CHANGE_DIR_REPLY
        return -1; // Not match
    }
    if(header->m_status != 0 && header->m_status != 1){ // status = 0 or 1
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_GET_REQUEST(struct MyFtpHeader* header, uint32_t payload_length){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA7; // GET_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12 + payload_length + 1); // Big End 12 + payload_length
    return 0;
}

int Is_GET_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA7){ // GET_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) < 13){ // Big End 12 + payload_length + 1 >= 13
        return -1; // Not match
    }
    return 0; // Match
}

int Create_GET_REPLY(struct MyFtpHeader* header, int exist){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA8; // GET_REPLY
    header->m_status = exist ? 1 : 0; // status = 1 if exist else 0
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_GET_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA8){ // GET_REPLY
        return -1; // Not match
    }
    if(header->m_status != 0 && header->m_status != 1){ // status = 0 or 1
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_PUT_REQUEST(struct MyFtpHeader* header, uint32_t payload_length){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xA9; // PUT_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12 + payload_length + 1); // Big End 12 + payload_length
    return 0;
}

int Is_PUT_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xA9){ // PUT_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) < 13){ // Big End 12 + payload_length + 1 >= 13
        return -1; // Not match
    }
    return 0; // Match
}

int Create_PUT_REPLY(struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xAA; // PUT_REPLY
    header->m_status = 0; // Unused
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_PUT_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xAA){ // PUT_REPLY
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_SHA_REQUEST(struct MyFtpHeader* header, uint32_t payload_length){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xAB; // SHA_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12 + payload_length + 1); // Big End 12 + payload_length
    return 0;
}

int Is_SHA_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xAB){ // SHA_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) < 13){ // Big End 12 + payload_length + 1 >= 13
        return -1; // Not match
    }
    return 0; // Match
}

int Create_SHA_REPLY(struct MyFtpHeader* header, int exist){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xAC; // SHA_REPLY
    header->m_status = exist ? 1 : 0; // status = 1 if exist else 0
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_SHA_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xAC){ // SHA_REPLY
        return -1; // Not match
    }
    if(header->m_status != 0 && header->m_status != 1){ // status = 0 or 1
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_QUIT_REQUEST(struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xAD; // QUIT_REQUEST
    header->m_status = 0; // Unused
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_QUIT_REQUEST(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xAD){ // QUIT_REQUEST
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_QUIT_REPLY(struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xAE; // QUIT_REPLY
    header->m_status = 0; // Unused
    header->m_length = htonl(12); // Big End 12
    return 0;
}

int Is_QUIT_REPLY(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xAE){ // QUIT_REPLY
        return -1; // Not match
    }
    if(ntohl(header->m_length) != 12){ // Big End 12
        return -1; // Not match
    }
    return 0; // Match
}

int Create_File_DATA(struct MyFtpHeader* header, uint32_t payload_length){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        header->m_protocol[i] = protocol[i];
    }
    header->m_type = 0xFF; // File_DATA
    header->m_status = 0; // Unused
    header->m_length = htonl(12 + payload_length); // Big End 12 + payload_length
    return 0;
}

int Is_File_DATA(const struct MyFtpHeader* header){
    if(header == nullptr) return -1;

    for(int i = 0; i < MAGIC_NUMBER_LENGTH; i++){
        if(header->m_protocol[i] != protocol[i]){
            return -1; // Not match
        }
    }
    if(header->m_type != 0xFF){ // File_DATA
        return -1; // Not match
    }
    if(ntohl(header->m_length) < 12){ // Big End 12 + payload_length >= 12
        return -1; // Not match
    }
    return 0; // Match
}

int Write_Head(int sockfd, const struct MyFtpHeader* header){
    if(sockfd < 0 || header == nullptr) return -1;

    int send_len = HEADER_LENGTH;
    while(send_len > 0){
        int n = send(sockfd, ((char*)header) + (HEADER_LENGTH - send_len), send_len, 0);
        if(n <= 0){
            return -1;
        }
        send_len -= n;
    }
    return 0;
}

int Read_Head(int sockfd, struct MyFtpHeader* header){
    if(sockfd < 0 || header == nullptr) return -1;

    int recv_len = HEADER_LENGTH;
    while(recv_len > 0){
        int n = recv(sockfd, ((char*)header) + (HEADER_LENGTH - recv_len), recv_len, 0);
        if(n <= 0){
            return -1;
        }
        recv_len -= n;
    }
    return 0;
}