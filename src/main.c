#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static void put16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8); out[1] = (uint8_t)value;
}

static uint16_t get16(const uint8_t *in) {
    return (uint16_t)((uint16_t)in[0] << 8 | in[1]);
}

#define MAX_QUESTIONS 16
#define MAX_NAME 255

typedef struct {
    uint8_t name[MAX_NAME];
    size_t name_len;
    uint16_t type, class_code;
} Question;

/* Decode a label sequence, advancing the caller past its on-wire bytes. */
static int read_name(const uint8_t *packet, size_t length, size_t *offset,
                     uint8_t *name, size_t *name_len) {
    size_t pos = *offset, used = 0;
    while (pos < length) {
        uint8_t label = packet[pos++];
        if (label > 63 || used + 1 + label > MAX_NAME || pos + label > length) return -1;
        name[used++] = label;
        memcpy(name + used, packet + pos, label); used += label; pos += label;
        if (label == 0) { *offset = pos; *name_len = used; return 0; }
    }
    return -1;
}

static int read_questions(const uint8_t *packet, size_t length,
                          Question *questions, uint16_t count) {
    size_t offset = 12;
    for (uint16_t i = 0; i < count; i++) {
        Question *q = &questions[i];
        if (read_name(packet, length, &offset, q->name, &q->name_len) < 0 || offset + 4 > length)
            return -1;
        q->type = get16(packet + offset); q->class_code = get16(packet + offset + 2);
        offset += 4;
    }
    return 0;
}

static size_t write_question(uint8_t *out, const Question *q) {
    memcpy(out, q->name, q->name_len);
    put16(out + q->name_len, q->type); put16(out + q->name_len + 2, q->class_code);
    return q->name_len + 4;
}

int main(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in server = {.sin_family = AF_INET, .sin_port = htons(2053),
                                 .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind"); close(fd); return 1;
    }
    for (;;) {
        uint8_t request[4096], response[4096] = {0};
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        ssize_t received = recvfrom(fd, request, sizeof(request), 0,
                                    (struct sockaddr *)&client, &client_len);
        if (received < 0) { if (errno == EINTR) continue; perror("recvfrom"); break; }
        if (received < 12) continue;
        size_t response_len = 12;
        put16(response, get16(request));
        uint16_t query_flags = get16(request + 2);
        uint16_t opcode = query_flags & 0x7800;
        put16(response + 2, 0x8000 | opcode | (query_flags & 0x0100) | (opcode ? 4 : 0));
        uint16_t count = get16(request + 4);
        Question questions[MAX_QUESTIONS];
        if (count > MAX_QUESTIONS || read_questions(request, (size_t)received, questions, count) < 0)
            continue;
        size_t required = 12;
        for (uint16_t i = 0; i < count; i++) required += 2 * questions[i].name_len + 18;
        if (required > sizeof(response)) continue;
        put16(response + 4, count); put16(response + 6, count);
        for (uint16_t i = 0; i < count; i++)
            response_len += write_question(response + response_len, &questions[i]);
        for (uint16_t i = 0; i < count; i++) {
            response_len += write_question(response + response_len, &questions[i]);
            response[response_len + 3] = 60; response_len += 4;
            put16(response + response_len, 4); response_len += 2;
            memset(response + response_len, 8, 4); response_len += 4;
        }
        if (sendto(fd, response, response_len, 0, (struct sockaddr *)&client, client_len) < 0)
            perror("sendto");
    }
    close(fd);
    return 0;
}
