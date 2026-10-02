/* 
 * ============================================================ 
 * tcp-chat | client.c 
 * Multi-threaded TCP chat client — IPv4 / IPv6 
 * ============================================================ 
 * 
 * Features: 
 * • Dual-stack via getaddrinfo (AF_UNSPEC) 
 * • Separate pthreads for send and receive 
 * • Colored local echo so your own messages stand out 
 * • Clean disconnect on EOF (Ctrl-D) 
 * 
 * Build: 
 * gcc client.c -o client -lpthread 
 * 
 * Run: 
 * ./client 
 * ============================================================ 
 */

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <unistd.h>

#include <pthread.h>

#include <arpa/inet.h>

#include <sys/socket.h>

#include <netdb.h>

/* ── ANSI color codes ────────────────────────────────────── */

#define CLR_RESET "\033[0m"
#define CLR_BOLD "\033[1m"
#define CLR_GREEN "\033[32m"
#define CLR_YELLOW "\033[33m"
#define CLR_CYAN "\033[36m"
#define CLR_RED "\033[31m"

/* ── Configuration ───────────────────────────────────────── */

#define HOST "localhost"
#define PORT "8080"
#define BUF_SIZE 1024

/* The username sent to the server on connect */
static
const char * USERNAME = "Rahul";

/* ── Receive thread ──────────────────────────────────────── */

/* 
 * Continuously reads from the socket and prints incoming 
 * messages. The server already formats them as 
 * "<color>[Name]: message<reset>", so we print as-is. 
 */
static void * receive_messages(void * arg) {
    int fd = * (int * ) arg;
    char buffer[BUF_SIZE];
    int bytes;

    while (1) {
        bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes == 0) {
            printf(CLR_YELLOW "\n Server closed the connection.\n"
                CLR_RESET);
            break;
        }

        if (bytes < 0) {
            perror("recv");
            break;
        }

        buffer[bytes] = '\0';
        printf("%s\n", buffer);
    }

    return NULL;
}

/* ── Send thread ─────────────────────────────────────────── */

/* 
 * Reads lines from stdin and ships them to the server. 
 * Echoes the message locally so the sender gets visual feedback. 
 * Exits cleanly on EOF (Ctrl-D). 
 */
static void * send_messages(void * arg) {
    int fd = * (int * ) arg;
    char buffer[BUF_SIZE];

    while (1) {
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            /* EOF — user pressed Ctrl-D */
            printf(CLR_YELLOW "\n Disconnecting...\n"
                CLR_RESET);
            close(fd);
            break;
        }

        /* Strip trailing newline for the wire, echo with formatting */
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
            buffer[--len] = '\0';

        if (len == 0) continue; /* ignore blank lines */

        send(fd, buffer, len, 0);

        /* Local echo: cyan bold name + white message */
        printf(CLR_CYAN CLR_BOLD "[%s]"
            CLR_RESET CLR_CYAN " %s\n"
            CLR_RESET,
            USERNAME, buffer);
    }

    return NULL;
}

/* ── main ────────────────────────────────────────────────── */

int main(void) {
    /* ── Resolve host / create socket ── */
    struct addrinfo hints, * addr;
    memset( & hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(HOST, PORT, & hints, & addr) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    int fd = socket(addr -> ai_family,
        addr -> ai_socktype,
        addr -> ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(addr);
        return 1;
    }

    /* ── Connect ── */
    if (connect(fd, addr -> ai_addr, addr -> ai_addrlen) < 0) {
        perror("connect");
        freeaddrinfo(addr);
        close(fd);
        return 1;
    }

    freeaddrinfo(addr);

    printf(CLR_GREEN CLR_BOLD "\n tcp-chat client\n"
        CLR_RESET CLR_GREEN " Connected to %s:%s • logged in as %s\n\n"
        CLR_RESET, HOST, PORT, USERNAME);

    /* ── Register with the server by sending our name first ── */
    send(fd, USERNAME, strlen(USERNAME), 0);

    /* ── Spawn receive / send threads ── */
    pthread_t recv_thread, send_thread;

    pthread_create( & recv_thread, NULL, receive_messages, & fd);
    pthread_create( & send_thread, NULL, send_messages, & fd);

    pthread_join(recv_thread, NULL);
    pthread_join(send_thread, NULL);

    close(fd);
    return 0;
}