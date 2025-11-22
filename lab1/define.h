#ifndef DEFINE_H
#define DEFINE_H


#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <netinet/in.h> // For htonl
#include <string>

namespace fs = std::filesystem;

#define MAGIC_NUMBER_LENGTH 6
#define byte uint8_t
#define type uint8_t
#define status uint8_t

const unsigned char protocol[MAGIC_NUMBER_LENGTH] = {0xc1, 0xa1, 0x10, 'f', 't', 'p'};
struct MyFtpHeader {
    byte m_protocol[MAGIC_NUMBER_LENGTH]; /* protocol magic number (6 bytes) */
    type m_type;                          /* type (1 byte) */
    status m_status;                      /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
} __attribute__ ((packed));

#define HEADER_LENGTH sizeof(struct MyFtpHeader)

int Create_OPEN_CONN_REQUEST(struct MyFtpHeader* header);
int Is_OPEN_CONN_REQUEST(const struct MyFtpHeader* header);

int Create_OPEN_CONN_REPLY(struct MyFtpHeader* header);
int Is_OPEN_CONN_REPLY(const struct MyFtpHeader* header);

int Create_LIST_REQUEST(struct MyFtpHeader* header);
int Is_LIST_REQUEST(const struct MyFtpHeader* header);

int Create_LIST_REPLY(struct MyFtpHeader* header, uint32_t payload_length);
int Is_LIST_REPLY(const struct MyFtpHeader* header);

int Create_CHANGE_DIR_REQUEST(struct MyFtpHeader* header, uint32_t payload_length);
int Is_CHANGE_DIR_REQUEST(const struct MyFtpHeader* header);

int Create_CHANGE_DIR_REPLY(struct MyFtpHeader* header, int exist);
int Is_CHANGE_DIR_REPLY(const struct MyFtpHeader* header);

int Create_GET_REQUEST(struct MyFtpHeader* header, uint32_t payload_length);
int Is_GET_REQUEST(const struct MyFtpHeader* header);

int Create_GET_REPLY(struct MyFtpHeader* header, int exist);
int Is_GET_REPLY(const struct MyFtpHeader* header);

int Create_PUT_REQUEST(struct MyFtpHeader* header, uint32_t payload_length);
int Is_PUT_REQUEST(const struct MyFtpHeader* header);

int Create_PUT_REPLY(struct MyFtpHeader* header);
int Is_PUT_REPLY(const struct MyFtpHeader* header);

int Create_SHA_REQUEST(struct MyFtpHeader* header, uint32_t payload_length);
int Is_SHA_REQUEST(const struct MyFtpHeader* header);

int Create_SHA_REPLY(struct MyFtpHeader* header, int exist);
int Is_SHA_REPLY(const struct MyFtpHeader* header);

int Create_QUIT_REQUEST(struct MyFtpHeader* header);
int Is_QUIT_REQUEST(const struct MyFtpHeader* header);

int Create_QUIT_REPLY(struct MyFtpHeader* header);
int Is_QUIT_REPLY(const struct MyFtpHeader* header);

int Create_File_DATA(struct MyFtpHeader* header, uint32_t payload_length); // 如果是SHA值，则payload要加一
int Is_File_DATA(const struct MyFtpHeader* header);

int Read_Head(int sockfd, struct MyFtpHeader* header);
int Write_Head(int sockfd, const struct MyFtpHeader* header);


#endif