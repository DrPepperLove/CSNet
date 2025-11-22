#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <arpa/inet.h>
#include "define.h"

enum CMD_TYPE{
    CMD_OPEN,
    CMD_LS,
    CMD_CD,
    CMD_GET,
    CMD_PUT,
    CMD_SHA,
    CMD_QUIT
};

struct Cmd{
    CMD_TYPE type;
    char arg1[256];
    char arg2[256];
};

int Parse_Cmd(struct Cmd* cmd, const char* buf){
    if(strncmp(buf, "open ", 5) == 0){
        cmd->type = CMD_OPEN;
        sscanf(buf + 5, "%s %s", cmd->arg1, cmd->arg2);
    }else if(strncmp(buf, "ls", 2) == 0){
        cmd->type = CMD_LS;
    }else if(strncmp(buf, "cd ", 3) == 0){
        cmd->type = CMD_CD;
        sscanf(buf + 3, "%s", cmd->arg1);
    }else if(strncmp(buf, "get ", 4) == 0){
        cmd->type = CMD_GET;
        sscanf(buf + 4, "%s", cmd->arg1);
    }else if(strncmp(buf, "put ", 4) == 0){
        cmd->type = CMD_PUT;
        sscanf(buf + 4, "%s", cmd->arg1);
    }else if(strncmp(buf, "sha ", 4) == 0){
        cmd->type = CMD_SHA;
        sscanf(buf + 4, "%s", cmd->arg1);
    }else if(strncmp(buf, "quit", 4) == 0){
        cmd->type = CMD_QUIT;
    }else{
        return -1; // Unknown command
    }
    return 0; // Success
}

int Open_Handler(struct Cmd* cmd){
    int sockfd;
    struct sockaddr_in server_addr;
    struct MyFtpHeader header;
    if(cmd == nullptr) return -1;
    if(cmd->arg1[0] == '\0' || cmd->arg2[0] == '\0') {
        printf("Invalid arguments for open command\n");
        return -1;
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        printf("Socket creation failed\n");
        return -1;
    }
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(cmd->arg2)); // Big End
    inet_pton(AF_INET, cmd->arg1, &server_addr.sin_addr);

    if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        printf("Connection to %s:%s failed\n", cmd->arg1, cmd->arg2);
        close(sockfd);
        return -1;
    }

    // Send OPEN_CONN_REQUEST
    if(Create_OPEN_CONN_REQUEST(&header) == -1){
        printf("Failed to create OPEN_CONN_REQUEST\n");
        close(sockfd);
        return -1;
    }

    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send OPEN_CONN_REQUEST\n");
        close(sockfd);
        return -1;
    }

    // Receive OPEN_CONN_REPLY
    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive OPEN_CONN_REPLY\n");
        close(sockfd);
        return -1;
    }

    if(Is_OPEN_CONN_REPLY(&header) == -1){
        printf("Invalid OPEN_CONN_REPLY\n");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int LS_Handler(struct Cmd* cmd, int sockfd){
    if(sockfd == -1 || cmd == nullptr) return -1;

    struct MyFtpHeader header;
    if(Create_LIST_REQUEST(&header) == -1){
        printf("Failed to create LIST_REQUEST\n");
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send LIST_REQUEST\n");
        return -1;
    }
    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive LIST_REPLY\n");
        return -1;
    }
    if(Is_LIST_REPLY(&header) == -1){
        printf("Invalid LIST_REPLY\n");
        return -1;
    }
    uint32_t payload_length = ntohl(header.m_length) - HEADER_LENGTH;
    char buf[2048];
    memset(buf, 0, sizeof(buf));
    uint32_t read_len = 0;
    while (payload_length > read_len)
    {
        int n = recv(sockfd, buf + (ntohl(header.m_length) - HEADER_LENGTH - payload_length), payload_length, 0);
        if(n <= 0){
            printf("Failed to receive LIST_REPLY payload\n");
            return -1;  
        }
        read_len += n;
    }

    buf[ntohl(header.m_length) - HEADER_LENGTH - 1] = '\0'; // Null-terminate the string
    printf("%s", buf);
    return 0;
}

int CD_Handler(struct Cmd* cmd, int sockfd){
    if(sockfd == -1 || cmd == nullptr) return -1;
    if(cmd->arg1[0] == '\0'){
        printf("Invalid arguments for cd command\n");
        return -1;
    }
    struct MyFtpHeader header;
    if(Create_CHANGE_DIR_REQUEST(&header, strlen(cmd->arg1)) == -1){
        printf("Failed to create CHANGE_DIR_REQUEST\n");
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send CHANGE_DIR_REQUEST\n");
        return -1;
    }
    // Send payload
    uint32_t send_len = strlen(cmd->arg1) + 1; // Include null terminator
    uint32_t sent = 0;
    while(send_len > sent){
        int n = send(sockfd, cmd->arg1 + sent, send_len - sent, 0);
        if(n <= 0){
            printf("Failed to send CHANGE_DIR_REQUEST payload\n");
            return -1;
        }
        sent += n;
    }

    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive CHANGE_DIR_REPLY\n");
        return -1;
    }
    if(Is_CHANGE_DIR_REPLY(&header) == -1){
        printf("Invalid CHANGE_DIR_REPLY\n");
        return -1;
    }

    if(header.m_status == 0){
        printf("Directory %s does not exist\n", cmd->arg1);
        return -1;
    }
    else{
        printf("Changed directory to %s\n", cmd->arg1);
    }
    return 0;
}

int GET_Handler(struct Cmd* cmd, int sockfd){
    if(sockfd == -1 || cmd == nullptr) return -1;
    if(cmd->arg1[0] == '\0'){
        printf("Invalid arguments for get command\n");
        return -1;
    }
    struct MyFtpHeader header;
    if(Create_GET_REQUEST(&header, strlen(cmd->arg1)) == -1){
        printf("Failed to create GET_REQUEST\n");
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send GET_REQUEST\n");
        return -1;
    }
    // Send payload
    uint32_t send_len = strlen(cmd->arg1) + 1; // Include null terminator
    uint32_t sent = 0;
    while(send_len > sent){
        int n = send(sockfd, cmd->arg1 + sent, send_len - sent, 0);
        if(n <= 0){
            printf("Failed to send GET_REQUEST payload\n");
            return -1;
        }
        sent += n;
    }

    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive GET_REPLY\n");
        return -1;
    }
    if(Is_GET_REPLY(&header) == -1){
        printf("Invalid GET_REPLY\n");
        return -1;
    }
    if(header.m_status == 0){
        printf("File %s does not exist on server\n", cmd->arg1);
        return -1;
    }

    // Receive file data
    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive File_DATA header\n");
        return -1;
    }
    if(Is_File_DATA(&header) == -1){
        printf("Invalid File_DATA header\n");
        return -1;
    }

    std::string cwd = fs::current_path();
    fs::path file_path = fs::path(cwd) /  fs::path(cmd->arg1);
    if(fs::exists(file_path.parent_path()) == false){
        fs::create_directories(file_path.parent_path());
    }

    FILE* fp = fopen(file_path.c_str(), "wb");
    if(fp == nullptr){
        printf("Failed to open file %s for writing\n", cmd->arg1);
        return -1;
    }

    uint32_t payload_length = ntohl(header.m_length) - HEADER_LENGTH;
    char* file_buf[4096]; 
    memset(file_buf, 0, sizeof(file_buf));
    uint32_t read_len = 0;

    while (payload_length > read_len)
    {
        uint32_t to_read = (payload_length - read_len) > sizeof(file_buf) ? sizeof(file_buf) : (payload_length - read_len);
        int n = recv(sockfd, (char*)file_buf, to_read, 0);
        if(n <= 0){
            printf("Failed to receive file data\n");
            fclose(fp);
            return -1;
        }
        fwrite(file_buf, 1, n, fp);
        read_len += n;
        memset(file_buf, 0, sizeof(file_buf));
    }
    fclose(fp);

    printf("File %s downloaded successfully\n", cmd->arg1);
    return 0;
}

int PUT_Handler(struct Cmd* cmd, int sockfd){
    if(sockfd == -1 || cmd == nullptr) return -1;
    struct MyFtpHeader header;

    if(cmd->arg1[0] == '\0'){
        printf("Invalid arguments for put command\n");
        return -1;
    }
    std::string cwd = fs::current_path();
    fs::path file_path = fs::path(cwd) / fs::path(cmd->arg1);
    if(fs::exists(file_path) == false || fs::is_regular_file(file_path) == false){
        printf("File %s does not exist\n", cmd->arg1);
        return -1;
    }
    FILE* fp = fopen(file_path.c_str(), "rb");
    if(fp == nullptr){
        printf("Failed to open file %s\n", cmd->arg1);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    uint32_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if(Create_PUT_REQUEST(&header, strlen(cmd->arg1)) == -1){
        printf("Failed to create PUT_REQUEST\n");
        fclose(fp);
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send PUT_REQUEST\n");
        fclose(fp);
        return -1;
    }
    // Send FileName payload
    uint32_t send_len = strlen(cmd->arg1) + 1; // Include null terminator
    uint32_t filename_sent = 0;
    while(send_len > filename_sent){
        int n = send(sockfd, cmd->arg1 + filename_sent, send_len - filename_sent, 0);
        if(n <= 0){
            printf("Failed to send PUT_REQUEST payload\n");
            fclose(fp);
            return -1;
        }
        filename_sent += n;
    }

    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive PUT_REPLY\n");
        fclose(fp);
        return -1;
    }
    if(Is_PUT_REPLY(&header) == -1){
        printf("Invalid PUT_REPLY\n");
        fclose(fp);
        return -1;
    }
    if(Create_File_DATA(&header, file_size) == -1){
        printf("Failed to create File_DATA header\n");
        fclose(fp);
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send File_DATA header\n");
        fclose(fp);
        return -1;
    }
    // Send file data
    uint32_t sent = 0;
    char buf[4096];
    while(sent < file_size){
        uint32_t to_read = (file_size - sent) > sizeof(buf) ? sizeof(buf) : (file_size - sent);
        size_t n = fread(buf, 1, to_read, fp);
        if(n <= 0){
            printf("Failed to read file %s\n", file_path.c_str());
            fclose(fp);
            return -1;
        }
        uint32_t written = 0;
        while(written < n){
            int m = send(sockfd, buf + written, n - written, 0);
            if(m <= 0){
                printf("Failed to send file %s data\n", file_path.c_str()); 
                fclose(fp);
                return -1;
            }
            written += m;
        }
        sent += n;
        memset(buf, 0, sizeof(buf));
    }
    fclose(fp);
    printf("File %s uploaded successfully\n", cmd->arg1);
    return 0;
}

int SHA_Handler(struct Cmd* cmd, int sockfd){
    if(sockfd == -1 || cmd == nullptr) return -1;
    struct MyFtpHeader header;
    if(cmd->arg1[0] == '\0'){
        printf("Invalid arguments for sha command\n");
        return -1;
    }
    if(Create_SHA_REQUEST(&header, strlen(cmd->arg1)) == -1){
        printf("Failed to create SHA_REQUEST\n");
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send SHA_REQUEST\n");
        return -1;
    }

    uint32_t send_len = strlen(cmd->arg1) + 1; // Include null terminator
    uint32_t sent = 0;
    while(send_len > sent){
        int n = send(sockfd, cmd->arg1 + sent, send_len - sent, 0);
        if(n <= 0){
            printf("Failed to send SHA_REQUEST payload\n");
            return -1;
        }
        sent += n;
    }

    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive SHA_REPLY\n");
        return -1;
    }
    if(Is_SHA_REPLY(&header) == -1){
        printf("Invalid SHA_REPLY\n");
        return -1;
    }
    if(header.m_status == 0){
        printf("File %s does not exist on server\n", cmd->arg1);
        return -1;
    }
    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive SHA_REPLY payload header\n");
        return -1;
    }
    if(Is_File_DATA(&header) == -1){
        printf("Invalid SHA_REPLY payload header\n");
        return -1;
    }

    // Receive payload
    uint32_t payload_length = ntohl(header.m_length) - HEADER_LENGTH - 1;
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    uint32_t read_len = 0;
    while (payload_length > read_len)
    {
        int n = recv(sockfd, buf + read_len, payload_length - read_len, 0);
        if(n <= 0){
            printf("Failed to receive SHA_REPLY payload\n");
            return -1;  
        }
        read_len += n;
    }
    buf[payload_length] = '\0'; // Null-terminate the string
    printf("SHA-256(%s) = %s\n", cmd->arg1, buf);

    return 0;
}

int QUIT_Handler(struct Cmd* cmd, int sockfd){
    if(sockfd == -1 || cmd == nullptr) return -1;
    struct MyFtpHeader header;
    char buf[1024];
    if(Create_QUIT_REQUEST(&header) == -1){
        printf("Failed to create QUIT_REQUEST\n");
        return -1;
    }
    if(Write_Head(sockfd, &header) == -1){
        printf("Failed to send QUIT_REQUEST\n");
        return -1;
    }
    if(Read_Head(sockfd, &header) == -1){
        printf("Failed to receive QUIT_REPLY\n");
        return -1;
    }
    if(Is_QUIT_REPLY(&header) == -1){
        printf("Invalid QUIT_REPLY\n");
        return -1;
    }
    if(close(sockfd) == -1){
        printf("Failed to close socket\n");
        return -1;
    }
    return 0;
}


int main() {
    char server_ip[256] = "none";
    int sockfd = -1;

    while(1){
        printf("Client(%s)> ", server_ip);
        char buf[1024];
        struct Cmd cmd;
        memset(&cmd, 0, sizeof(cmd));
        memset(buf, 0, sizeof(buf));

        if(fgets(buf, sizeof(buf), stdin) == NULL){
            break;
        }
        buf[strlen(buf) - 1] = '\0'; // Remove newline character

        if(Parse_Cmd(&cmd, buf) == -1){
            printf("Invalid command\n");
            continue;
        }

        switch(cmd.type){
            case CMD_OPEN:
                if(strcmp(server_ip, "none") != 0){
                    printf("Already connected to %s\n", server_ip);
                    break;
                }
                sockfd = Open_Handler(&cmd);
                if(sockfd == -1){
                    printf("Failed to open connection to %s:%s\n", cmd.arg1, cmd.arg2);
                    break;
                }
                printf("Connected to %s\n", cmd.arg1);
                memcpy(server_ip, cmd.arg1, sizeof(cmd.arg1));
                break;
            case CMD_LS:
                if(LS_Handler(&cmd, sockfd) == -1){
                    printf("Failed to list directory\n");
                }
                break;
            case CMD_CD:
                if(CD_Handler(&cmd, sockfd) == -1){
                    printf("Failed to change directory to %s\n", cmd.arg1);
                }
                break;
            case CMD_GET:
                if(GET_Handler(&cmd, sockfd) == -1){
                    printf("Failed to get file %s\n", cmd.arg1);
                }
                break;
            case CMD_PUT:
                if(PUT_Handler(&cmd, sockfd) == -1){
                    printf("Failed to put file %s\n", cmd.arg1);
                }
                break;
            case CMD_SHA:
                if(SHA_Handler(&cmd, sockfd) == -1){
                    printf("Failed to get sha of file %s\n", cmd.arg1);
                }
                break;
            case CMD_QUIT:
                if(strcmp(server_ip, "none") == 0){
                    exit(0);
                }
                if(QUIT_Handler(&cmd, sockfd) == -1){
                    printf("Failed to quit connection to %s\n", server_ip);
                    break;
                }
                printf("Disconnected from %s\n", server_ip);
                memcpy(server_ip, "none", 5);
                break;
            default:
                printf("Unknown command\n");
                break;
        }
    
    }
}
