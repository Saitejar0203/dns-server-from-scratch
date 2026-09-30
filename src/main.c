#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
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
    size_t pos = *offset, used = 0, consumed = 0;
    int jumped = 0;
    /* At most one step per packet byte prevents cyclic pointer chains. */
    for (size_t steps = 0; steps < length && pos < length; steps++) {
        uint8_t label = packet[pos++];
        if ((label & 0xc0) == 0xc0) {
            if (pos >= length) return -1;
            size_t target = ((size_t)(label & 0x3f) << 8) | packet[pos++];
            if (target >= length) return -1;
            if (!jumped) consumed = pos;
            jumped = 1; pos = target; continue;
        }
        if (label > 63 || used + 1 + label > MAX_NAME || pos + label > length) return -1;
        name[used++] = label;
        memcpy(name + used, packet + pos, label); used += label; pos += label;
        if (label == 0) {
            *offset = jumped ? consumed : pos; *name_len = used; return 0;
        }
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

/* A connected UDP socket accepts datagrams only from the chosen resolver. */
static int forward_question(const struct sockaddr_in *resolver, uint16_t id, uint16_t flags,
                            const Question *question, uint8_t *out, size_t capacity,
                            size_t *written, uint16_t *answer_count) {
    uint8_t query[512] = {0}, reply[4096];
    put16(query, id); put16(query + 2, flags & 0x7900); put16(query + 4, 1);
    size_t query_len = 12 + write_question(query + 12, question);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 2};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        connect(fd, (const struct sockaddr *)resolver, sizeof(*resolver)) < 0 ||
        send(fd, query, query_len, 0) != (ssize_t)query_len) { close(fd); return -1; }
    ssize_t received;
    do { received = recv(fd, reply, sizeof(reply), 0); } while (received < 0 && errno == EINTR);
    close(fd);
    if (received < 12 || get16(reply) != id || !(get16(reply + 2) & 0x8000) ||
        (get16(reply + 2) & 0x0200) || get16(reply + 4) != 1) return -1;
    unsigned rcode = get16(reply + 2) & 15;
    if (rcode) return (int)rcode;
    size_t length = (size_t)received, offset = 12, used = 0;
    uint8_t name[MAX_NAME]; size_t name_len;
    if (read_name(reply, length, &offset, name, &name_len) < 0 || offset + 4 > length ||
        name_len != question->name_len || memcmp(name, question->name, name_len) != 0 ||
        get16(reply + offset) != question->type || get16(reply + offset + 2) != question->class_code)
        return -1;
    offset += 4;
    uint16_t count = get16(reply + 6);
    for (uint16_t i = 0; i < count; i++) {
        if (read_name(reply, length, &offset, name, &name_len) < 0 || offset + 10 > length)
            return -1;
        uint16_t data_len = get16(reply + offset + 8);
        /* This exercise forwards IPv4 A records; RDATA has no name pointers. */
        if (get16(reply + offset) != 1 || data_len != 4 || offset + 10 + data_len > length ||
            used + name_len + 10 + data_len > capacity) return -1;
        memcpy(out + used, name, name_len); used += name_len;
        memcpy(out + used, reply + offset, 10 + data_len); used += 10 + data_len;
        offset += 10 + data_len;
    }
    *written = used; *answer_count = count;
    return 0;
}

static int parse_resolver(const char *text, struct sockaddr_in *resolver) {
    const char *colon = strrchr(text, ':');
    if (!colon || colon == text || (size_t)(colon - text) >= INET_ADDRSTRLEN) return -1;
    char address[INET_ADDRSTRLEN];
    memcpy(address, text, (size_t)(colon - text)); address[colon - text] = 0;
    char *end; errno = 0;
    long port = strtol(colon + 1, &end, 10);
    if (errno || end == colon + 1 || *end || port < 1 || port > 65535) return -1;
    memset(resolver, 0, sizeof(*resolver)); resolver->sin_family = AF_INET;
    resolver->sin_port = htons((uint16_t)port);
    return inet_pton(AF_INET, address, &resolver->sin_addr) == 1 ? 0 : -1;
}

int main(int argc, char **argv) {
    struct sockaddr_in resolver;
    int forwarding = 0;
    if (argc != 1) {
        if (argc != 3 || strcmp(argv[1], "--resolver") != 0 || parse_resolver(argv[2], &resolver) < 0) {
            fprintf(stderr, "Usage: %s [--resolver IPv4:port]\n", argv[0]); return 1;
        }
        forwarding = 1;
    }
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
        size_t question_end = response_len;
        uint16_t total_answers = 0;
        for (uint16_t i = 0; i < count; i++) {
            if (opcode) break; /* Unsupported operations retain NOTIMP without forwarding. */
            if (forwarding) {
                size_t written = 0; uint16_t answers = 0;
                int result = forward_question(&resolver, get16(request), query_flags, &questions[i],
                                              response + response_len, sizeof(response) - response_len,
                                              &written, &answers);
                if (result != 0 || (unsigned)total_answers + answers > UINT16_MAX) {
                    uint16_t error = (uint16_t)(result > 0 ? result : 2);
                    put16(response + 2, (get16(response + 2) & 0xfff0) | error);
                    response_len = question_end; total_answers = 0; break;
                }
                response_len += written; total_answers += answers;
            } else {
                response_len += write_question(response + response_len, &questions[i]);
                response[response_len + 3] = 60; response_len += 4;
                put16(response + response_len, 4); response_len += 2;
                memset(response + response_len, 8, 4); response_len += 4;
                total_answers++;
            }
        }
        put16(response + 6, total_answers);
        if (sendto(fd, response, response_len, 0, (struct sockaddr *)&client, client_len) < 0)
            perror("sendto");
    }
    close(fd);
    return 0;
}
