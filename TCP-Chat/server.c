/* 
 * ============================================================ 
 * tcp-chat | server.c 
 * Multi-threaded TCP chat server — IPv4 / IPv6 
 * ============================================================ 
 * 
 * Features: 
 * • Dual-stack via getaddrinfo (AF_UNSPEC) 
 * • One pthread per connected client 
 * • Mutex-guarded shared client table 
 * • First message treated as the client's display name 
 * • Broadcasts formatted messages to every other client 
 * 
 * Build: 
 * gcc server.c -o server -lpthread 
 * 
 * Run: 
 * ./server 
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
#define CLR_RED "\033[31m"
#define CLR_GREEN "\033[32m"
#define CLR_YELLOW "\033[33m"
#define CLR_BLUE "\033[34m"
#define CLR_MAGENTA "\033[35m"
#define CLR_CYAN "\033[36m"
#define CLR_WHITE "\033[97m"

/* Cycle through these colors for each connecting client */
static
const char * CLIENT_COLORS[] = {
    CLR_CYAN,
    CLR_MAGENTA,
    CLR_YELLOW,
    CLR_GREEN,
    CLR_BLUE,
    CLR_RED,
    CLR_WHITE
};
#define NUM_COLORS (sizeof(CLIENT_COLORS) / sizeof(CLIENT_COLORS[0]))

/* ── Server configuration ────────────────────────────────── */

#define PORT "8080"
#define MAX_CLIENTS 100
#define BUF_SIZE 1024
#define NAME_SIZE 32

/* ── Client registry ─────────────────────────────────────── */

typedef struct {
    int socket_fd;
    char name[NAME_SIZE];
    const char * color; /* ANSI color assigned at connect time */
}
Client;

static Client clients[MAX_CLIENTS];
static int client_count = 0;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Helpers ─────────────────────────────────────────────── */

/* Return the sockaddr pointer regardless of address family */
static void * get_addr_ptr(struct sockaddr * sa) {
    if (sa -> sa_family == AF_INET)
        return & ((struct sockaddr_in * ) sa) -> sin_addr;
    return & ((struct sockaddr_in6 * ) sa) -> sin6_addr;
}

/* ── Client-table operations (all require the mutex) ──────── */

/* Add a new client; returns 0 on success, -1 when table is full */
static int add_client(Client c) {
    if (client_count >= MAX_CLIENTS)
        return -1;

    /* Assign a color based on current slot position */
    c.color = CLIENT_COLORS[client_count % NUM_COLORS];
    clients[client_count++] = c;
    return 0;
}

/* Remove client by fd using swap-with-last trick */
static void remove_client(int fd) {
    pthread_mutex_lock( & client_mutex);

    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket_fd == fd) {
            clients[i] = clients[--client_count];
            break;
        }
    }

    pthread_mutex_unlock( & client_mutex);
}

/* Find client by fd — caller must hold the mutex */
static Client * find_client(int fd) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket_fd == fd)
            return & clients[i];
    }
    return NULL;
}

/* ── Broadcast ───────────────────────────────────────────── */

/* 
 * Send `msg` to every client except the sender. 
 * Format: <color>[Name]: message<reset> 
 */
static void broadcast(int sender_fd,
    const char * msg) {
    pthread_mutex_lock( & client_mutex);

    Client * sender = find_client(sender_fd);
    if (!sender) {
        pthread_mutex_unlock( & client_mutex);
        return;
    }

    char packet[BUF_SIZE + NAME_SIZE + 32];
    snprintf(packet, sizeof(packet),
        "%s"
        CLR_BOLD "[%s]"
        CLR_RESET "%s %s"
        CLR_RESET,
        sender -> color, sender -> name,
        sender -> color, msg);

    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket_fd != sender_fd)
            send(clients[i].socket_fd, packet, strlen(packet), 0);
    }

    pthread_mutex_unlock( & client_mutex);
}

/* ── Per-client thread ───────────────────────────────────── */

static void * client_handler(void * arg) {
    int fd = * (int * ) arg;
    free(arg);

    char buffer[BUF_SIZE];
    int bytes;

    while (1) {
        bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes == 0) {
            /* Graceful disconnect */
            pthread_mutex_lock( & client_mutex);
            Client * c = find_client(fd);
            if (c) {
                printf(CLR_YELLOW " ✗ %s disconnected.\n"
                    CLR_RESET, c -> name);
            }
            pthread_mutex_unlock( & client_mutex);
            break;
        }

        if (bytes < 0) {
            perror("recv");
            break;
        }

        buffer[bytes] = '\0';

        /* Strip trailing newline for cleaner display */
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
            buffer[--len] = '\0';

        broadcast(fd, buffer);
    }

    remove_client(fd);
    close(fd);
    return NULL;
}

/* ── main ────────────────────────────────────────────────── */

int main(void) {
    /* ── Resolve address / create socket ── */
    struct addrinfo hints, * result;
    memset( & hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, & hints, & result) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    int server_fd = socket(result -> ai_family,
        result -> ai_socktype,
        result -> ai_protocol);
    if (server_fd < 0) {
        perror("socket");
        freeaddrinfo(result);
        return 1;
    }

    /* Allow fast restart after SIGINT */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, & opt, sizeof(opt));

    if (bind(server_fd, result -> ai_addr, result -> ai_addrlen) < 0) {
        perror("bind");
        freeaddrinfo(result);
        close(server_fd);
        return 1;
    }

    freeaddrinfo(result);

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf(CLR_GREEN CLR_BOLD "\n tcp-chat server\n"
        CLR_RESET CLR_GREEN " Listening on port %s • max %d clients\n\n"
        CLR_RESET, PORT, MAX_CLIENTS);

    /* ── Accept loop ── */
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        memset( & client_addr, 0, sizeof(client_addr));

        int * fd = malloc(sizeof(int));
        if (!fd) continue;

        * fd = accept(server_fd, (struct sockaddr * ) & client_addr, & addr_len);
        if ( * fd < 0) {
            free(fd);
            continue;
        }

        /* ── Read the client's name (first message) ── */
        char name_buf[NAME_SIZE];
        int bytes = recv( * fd, name_buf, sizeof(name_buf) - 1, 0);
        if (bytes <= 0) {
            close( * fd);
            free(fd);
            continue;
        }
        name_buf[bytes] = '\0';

        /* Strip trailing newline if present */
        int len = strlen(name_buf);
        if (len > 0 && name_buf[len - 1] == '\n')
            name_buf[--len] = '\0';

        /* ── Register client ── */
        Client nc;
        nc.socket_fd = * fd;
        nc.color = NULL; /* assigned inside add_client */
        strncpy(nc.name, name_buf, sizeof(nc.name) - 1);
        nc.name[sizeof(nc.name) - 1] = '\0';

        pthread_mutex_lock( & client_mutex);
        if (add_client(nc) < 0) {
            pthread_mutex_unlock( & client_mutex);
            printf(CLR_RED " Server full — rejected connection.\n"
                CLR_RESET);
            close( * fd);
            free(fd);
            continue;
        }
        pthread_mutex_unlock( & client_mutex);

        /* ── Log the connection ── */
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family,
            get_addr_ptr((struct sockaddr * ) & client_addr),
            ip, sizeof(ip));

        printf(CLR_GREEN " ✓ %s joined from %s\n"
            CLR_RESET, nc.name, ip);

        /* ── Spawn handler thread ── */
        pthread_t tid;
        pthread_create( & tid, NULL, client_handler, fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}