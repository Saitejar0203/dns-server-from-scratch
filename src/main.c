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
        size_t response_len = 12;
        put16(response, 1234);
        put16(response + 2, 0x8000);
        if (sendto(fd, response, response_len, 0, (struct sockaddr *)&client, client_len) < 0)
            perror("sendto");
    }
    close(fd);
    return 0;
}
