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
        static const uint8_t name[] = {12, 'c','o','d','e','c','r','a','f','t','e','r','s',2,'i','o',0};
        put16(response + 4, 1);
        memcpy(response + response_len, name, sizeof(name)); response_len += sizeof(name);
        put16(response + response_len, 1); response_len += 2;
        put16(response + response_len, 1); response_len += 2;
        put16(response + 6, 1);
        memcpy(response + response_len, name, sizeof(name)); response_len += sizeof(name);
        put16(response + response_len, 1); response_len += 2;
        put16(response + response_len, 1); response_len += 2;
        response[response_len + 3] = 60; response_len += 4;
        put16(response + response_len, 4); response_len += 2;
        memset(response + response_len, 8, 4); response_len += 4;
        if (sendto(fd, response, response_len, 0, (struct sockaddr *)&client, client_len) < 0)
            perror("sendto");
    }
    close(fd);
    return 0;
}
