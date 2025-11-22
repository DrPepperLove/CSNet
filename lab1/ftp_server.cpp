#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "define.h"
#include <cstdlib>



int Open_Handler(int conn_fd, struct MyFtpHeader* recv_header){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    if(Is_OPEN_CONN_REQUEST(recv_header) == -1){
        printf("Invalid OPEN_CONN_REQUEST\n");
        return -1;
    }
    if(Create_OPEN_CONN_REPLY(&header) == -1){
        printf("Failed to create OPEN_CONN_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send OPEN_CONN_REPLY\n");
        return -1;
    }
    return 0;
}

int LS_Handler(int conn_fd, struct MyFtpHeader* recv_header, const std::string& cwd){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    if(Is_LIST_REQUEST(recv_header) == -1){
        printf("Invalid LIST_REQUEST\n");
        return -1;
    }
    // Execute ls command and get output
    if(fs::exists(fs::path(cwd)) == false || fs::is_directory(fs::path(cwd)) == false){
        printf("Current directory does not exist or is not a directory\n");
        return -1;
    }

    std::string command = "ls " + cwd;
    FILE* fp = popen(command.c_str() , "r");
    if(fp == nullptr){
        printf("Failed to execute ls command\n");
        return -1;
    }
    char buf[2048];
    memset(buf, 0, sizeof(buf));

    uint32_t total_length = 0;
    uint32_t n;
    while((n = fread(buf + total_length, 1, sizeof(buf) - total_length - 1, fp)) > 0){
        total_length += n;
        if(total_length >= sizeof(buf) - 1){
            break; // Prevent overflow
        }
    }
    buf[total_length] = '\0'; // Null-terminate the string
    pclose(fp);

    if(Create_LIST_REPLY(&header, total_length) == -1){
        printf("Failed to create LIST_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send LIST_REPLY header\n");
        return -1;
    }
    // Send payload
    uint32_t send_len = total_length + 1; // Include null terminator
    uint32_t sent = 0;
    while(send_len > sent){
        int m = send(conn_fd, buf + sent, send_len - sent, 0);
        if(m <= 0){
            printf("Failed to send LIST_REPLY payload\n");
            return -1;
        }
        sent += m;
    }   

    return 0;
}

int CD_Handler(int conn_fd, struct MyFtpHeader* recv_header, std::string& cwd){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    if(Is_CHANGE_DIR_REQUEST(recv_header) == -1){
        printf("Invalid CHANGE_DIR_REQUEST\n");
        return -1;
    }
    uint32_t payload_length = ntohl(recv_header->m_length) - HEADER_LENGTH;
    if(payload_length == 0 || payload_length > 1024){
        printf("Invalid CHANGE_DIR_REQUEST payload length\n");
        return -1;
    }
    char dir[1024];
    memset(dir, 0, sizeof(dir));
    uint32_t recv_len = 0;
    while(payload_length > recv_len){
        int n = recv(conn_fd, dir + recv_len, payload_length - recv_len, 0);
        if(n <= 0){
            printf("Failed to receive CHANGE_DIR_REQUEST payload\n");
            return -1;
        }
        recv_len += n;
    }
    dir[payload_length - 1] = '\0'; // Null-terminate the string

    int exist = 0;
    fs::path new_path = fs::path(cwd) / fs::path(dir);
    if(fs::exists(new_path) && fs::is_directory(new_path)){
        exist = 1;
        cwd = fs::canonical(new_path).string(); // Update current working directory
    }

    if(Create_CHANGE_DIR_REPLY(&header, exist) == -1){
        printf("Failed to create CHANGE_DIR_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send CHANGE_DIR_REPLY\n");
        return -1;
    }
    if(exist){
        printf("Changed directory to %s\n", cwd.c_str());
    }
    else{
        printf("Directory %s does not exist\n", dir);
    }
    return 0;
}

int GET_Handler(int conn_fd, struct MyFtpHeader* recv_header, const std::string& cwd){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    char file_path[1024];
    memset(file_path, 0, sizeof(file_path));
    if(Is_GET_REQUEST(recv_header) == -1){
        printf("Invalid GET_REQUEST\n");
        return -1;
    }
    uint32_t payload_length = ntohl(recv_header->m_length) - HEADER_LENGTH;

    uint32_t recv_len = 0;
    while(payload_length > recv_len){
        int n = recv(conn_fd, file_path + recv_len, payload_length - recv_len, 0);
        if(n <= 0){
            printf("Failed to receive GET_REQUEST payload\n");
            return -1;
        }
        recv_len += n;
    }
    file_path[payload_length - 1] = '\0'; // Null-terminate the string

    fs::path full_path = fs::path(cwd) / fs::path(file_path);
    int exist = 0;
    if(fs::exists(full_path) && fs::is_regular_file(full_path)){
        exist = 1;
    }
    if(Create_GET_REPLY(&header, exist) == -1){
        printf("Failed to create GET_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send GET_REPLY\n");
        return -1;
    }
    if(exist == 0){
        printf("File %s does not exist\n", file_path);
        return 0;
    }
    else{
        printf("File %s exists, ready to send\n", file_path);
    }
    // Send file data
    FILE* fp = fopen(full_path.c_str(), "rb");
    if(fp == nullptr){
        printf("Failed to open file %s\n", full_path.c_str());
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    uint32_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if(Create_File_DATA(&header, file_size) == -1){
        printf("Failed to create File_DATA header\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send File_DATA header\n");
        return -1;
    }

    uint32_t sent = 0;
    char buf[4096];
    while(sent < file_size){
        uint32_t to_read = (file_size - sent) > sizeof(buf) ? sizeof(buf) : (file_size - sent);
        size_t n = fread(buf, 1, to_read, fp);
        if(n <= 0){
            printf("Failed to read file %s\n", full_path.c_str());
            fclose(fp);
            return -1;
        }
        uint32_t written = 0;
        while(written < n){
            int m = send(conn_fd, buf + written, n - written, 0);
            if(m <= 0){
                printf("Failed to send file %s data\n", full_path.c_str());
                fclose(fp);
                return -1;
            }
            written += m;
        }
        sent += n;
        memset(buf, 0, sizeof(buf));
    }
    fclose(fp);
    printf("File %s sent successfully\n", file_path);
    return 0;
}

int PUT_Handler(int conn_fd, struct MyFtpHeader* recv_header, const std::string& cwd){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    char file_path[1024];
    memset(file_path, 0, sizeof(file_path));
    if(Is_PUT_REQUEST(recv_header) == -1){
        printf("Invalid PUT_REQUEST\n");
        return -1;
    }
    uint32_t FileName_length = ntohl(recv_header->m_length) - HEADER_LENGTH;
    uint32_t filename_recv_len = 0;
    while(FileName_length > filename_recv_len){
        int n = recv(conn_fd, file_path + filename_recv_len, FileName_length - filename_recv_len, 0);
        if(n <= 0){
            printf("Failed to receive PUT_REQUEST payload\n");
            return -1;
        }
        filename_recv_len += n;
    }
    file_path[FileName_length - 1] = '\0'; // Null-terminate the string

    fs::path file_name = fs::path(cwd) / fs::path(file_path);

    if(Create_PUT_REPLY(&header) == -1){
        printf("Failed to create PUT_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send PUT_REPLY\n");
        return -1;
    }
    if(Read_Head(conn_fd, &header) == -1){
        printf("Failed to receive File_DATA header\n");
        return -1;
    }
    if(Is_File_DATA(&header) == -1){
        printf("Invalid File_DATA header\n");
        return -1;
    }
    uint32_t payload_length = ntohl(header.m_length) - HEADER_LENGTH;
    uint32_t recv_len = 0;
    FILE* fp = fopen(file_name.c_str(), "wb");
    if(fp == nullptr){
        printf("Failed to open file %s for writing\n", file_name.c_str());
        return -1;
    }
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    while(payload_length > recv_len){
        uint32_t to_read = (payload_length - recv_len) > sizeof(buf) ? sizeof(buf) : (payload_length - recv_len);
        int n = recv(conn_fd, buf, to_read, 0);
        if(n <= 0){
            printf("Failed to receive file data\n");
            fclose(fp);
            return -1;
        }
        fwrite(buf, 1, n, fp);
        recv_len += n;
        memset(buf, 0, sizeof(buf));
    }
    fclose(fp);
    return 0;
}

int SHA_Handler(int conn_fd, struct MyFtpHeader* recv_header, const std::string& cwd){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    char file_path[1024];
    memset(file_path, 0, sizeof(file_path));
    if(Is_SHA_REQUEST(recv_header) == -1){
        printf("Invalid SHA_REQUEST\n");
        return -1;
    }
    uint32_t filename_length = ntohl(recv_header->m_length) - HEADER_LENGTH;
    uint32_t recv_len = 0;
    while(filename_length > recv_len){
        int n = recv(conn_fd, file_path + recv_len, filename_length - recv_len, 0);
        if(n <= 0){
            printf("Failed to receive SHA_REQUEST payload\n");
            return -1;
        }
        recv_len += n;
    }
    file_path[filename_length - 1] = '\0'; // Null-terminate the string

    fs::path full_path = fs::path(cwd) / fs::path(file_path);
    int exist = 0;
    if(fs::exists(full_path) && fs::is_regular_file(full_path)){
        exist = 1;
    }
    if(Create_SHA_REPLY(&header, exist) == -1){
        printf("Failed to create SHA_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send SHA_REPLY\n");
        return -1;
    }
    if(exist == 0){
        printf("File %s does not exist\n", file_path);
        return 0;
    }
    else{
        printf("File %s exists, ready to send SHA-256\n", file_path);
    }

    // computr sha value
    std::string command = "sha256sum \"" + full_path.string() + "\"";

    FILE* fp = popen(command.c_str() , "r");
    if(fp == nullptr){
        printf("Failed to execute sha256sum command\n");
        return -1;
    }
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    uint32_t total_length = 0;
    uint32_t n;
    while((n = fread(buf + total_length, 1, sizeof(buf) - total_length - 1, fp)) > 0){
        total_length += n;
        if(total_length >= sizeof(buf) - 1){
            break; // Prevent overflow
        }
    }
    buf[total_length] = '\0'; // Null-terminate the string
    pclose(fp);

    // Send sha value
    if(Create_File_DATA(&header, total_length + 1) == -1){
        printf("Failed to create File_DATA header for SHA value\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send File_DATA header for SHA value\n");
        return -1;
    }
    uint32_t send_len = total_length + 1; // Include null terminator
    uint32_t sent = 0;
    while(send_len > sent){
        int m = send(conn_fd, buf + sent, send_len - sent, 0);
        if(m <= 0){
            printf("Failed to send SHA value payload\n");
            return -1;
        }
        sent += m;
    }
    
    return 0;
}

int QUIT_Handler(int conn_fd, struct MyFtpHeader* recv_header){
    if(conn_fd == -1 || recv_header == nullptr) return -1;
    struct MyFtpHeader header;
    if(Is_QUIT_REQUEST(recv_header) == -1){
        printf("Invalid QUIT_REQUEST\n");
        return -1;
    }
    if(Create_QUIT_REPLY(&header) == -1){
        printf("Failed to create QUIT_REPLY\n");
        return -1;
    }
    if(Write_Head(conn_fd, &header) == -1){
        printf("Failed to send QUIT_REPLY\n");
        return -1;
    }
    close(conn_fd);
    return 0;
}

int Handler(int conn_fd){

    if(conn_fd == -1) return -1;

    std::string cwd = fs::current_path(); // Save current working directory

    struct MyFtpHeader header;
    while(1){
        memset(&header, 0, sizeof(header));
        if(Read_Head(conn_fd, &header) == -1){
            printf("Failed to read header\n");
            close(conn_fd);
            return -1;
        }
        switch(header.m_type){
            case 0xA1: // OPEN_CONN_REQUEST
                if(Open_Handler(conn_fd, &header) == -1){
                    printf("Failed to handle OPEN_CONN_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A connection established\n");
                break;
            case 0xA3: // LIST_REQUEST
                if(LS_Handler(conn_fd, &header, cwd) == -1){
                    printf("Failed to handle LIST_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A LIST_REQUEST handled\n");
                break;
            case 0xA5: // CHANGE_DIR_REQUEST
                if(CD_Handler(conn_fd, &header, cwd) == -1){           
                    printf("Failed to handle CHANGE_DIR_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A CHANGE_DIR_REQUEST handled\n");
                break;
            case 0xA7: // GET_REQUEST
                if(GET_Handler(conn_fd, &header, cwd) == -1){
                    printf("Failed to handle GET_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A GET_REQUEST handled\n");
                break;
            case 0xA9: // PUT_REQUEST
                if(PUT_Handler(conn_fd, &header, cwd) == -1){
                    printf("Failed to handle PUT_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A PUT_REQUEST handled\n");
                break;  
            case 0xAB: // SHA_REQUEST
                if(SHA_Handler(conn_fd, &header, cwd) == -1){
                    printf("Failed to handle SHA_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A SHA_REQUEST handled\n");
                break;
            case 0xAD: // QUIT_REQUEST
                if(QUIT_Handler(conn_fd, &header) == -1){
                    printf("Failed to handle QUIT_REQUEST\n");
                    close(conn_fd);
                    return -1;
                }
                printf("A connection closed\n");
                return 0;
            default:
                printf("Unknown request type: 0x%02X\n", header.m_type);
                close(conn_fd);
                return -1;
        }
    }
    return 0;
}

void *Thread_Handler(void* arg){
    int conn_fd = *(int*)arg;
    free((int *)arg);
    if(Handler(conn_fd) == -1){
        printf("Fail to hander events\n");
    }
    return nullptr;
}

int main(int argc, char* argv[]){
    if(argc != 3){
        printf("Usage: %s <IP> <PORT>\n", argv[0]);
        return -1;
    }
    
    int listen_fd, conn_fd;
    struct sockaddr_in server_addr, client_addr;

    if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        printf("Server Listen Socket creation failed\n");
        return -1;
    }
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2])); // Big End
    if(bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        printf("Server Socket bind failed\n");
        close(listen_fd);
        return -1;
    }
    if(listen(listen_fd, 10) == -1){
        printf("Server Socket listen failed\n");
        close(listen_fd);
        return -1;
    }
    printf("Server listening on %s:%s\n", argv[1], argv[2]);

    while(1){
        socklen_t client_len = sizeof(client_addr);
        if((conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len)) == -1){
            printf("Server accept failed\n");
            continue;
        }
        int* pconn_fd = (int*)malloc(sizeof(int));
        *pconn_fd = conn_fd;
        pthread_t tid;
        if(pthread_create(&tid, nullptr, Thread_Handler, (void *)pconn_fd) != 0){
            printf("Failed to create thread\n");
            close(conn_fd);
            free(pconn_fd);
            continue;
        }
    }

    return 0;
}