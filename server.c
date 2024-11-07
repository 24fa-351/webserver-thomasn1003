#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>

#define DEFAULT_PORT 80
#define BUFFER_SIZE 4096
#define STATIC_DIR "./static"

int request_count = 0;
size_t total_received_bytes = 0;
size_t total_sent_bytes = 0;

pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;



void send_404(int client_fd) {

    const char *msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";

    send(client_fd, msg, strlen(msg), 0);

}



void send_500(int client_fd) {

    const char *msg = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 25\r\n\r\n500 Internal Server Error";

    send(client_fd, msg, strlen(msg), 0);

}



void handle_static(int client_fd, char *path) {

    char full_path[BUFFER_SIZE];

    snprintf(full_path, sizeof(full_path), "%s%s", STATIC_DIR, path + strlen("/static"));
    
    int file_fd = open(full_path, O_RDONLY);

    if (file_fd == -1) {

        send_404(client_fd);

        return;

    }

    struct stat file_stat;

    fstat(file_fd, &file_stat);

    char header[BUFFER_SIZE];

    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", file_stat.st_size);

    send(client_fd, header, strlen(header), 0);

    sendfile(client_fd, file_fd, NULL, file_stat.st_size);

    close(file_fd);

    pthread_mutex_lock(&stats_mutex);

    total_sent_bytes += file_stat.st_size;

    pthread_mutex_unlock(&stats_mutex);

}



void handle_stats(int client_fd) {

    pthread_mutex_lock(&stats_mutex);

    char response[BUFFER_SIZE];

    int response_length = snprintf(response, sizeof(response),

        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<html><body>"
        "<h1>Server Stats</h1>"
        "<p>Requests received: %d</p>"
        "<p>Total bytes received: %zu</p>"
        "<p>Total bytes sent: %zu</p>"
        "</body></html>",

        request_count, total_received_bytes, total_sent_bytes);

    pthread_mutex_unlock(&stats_mutex);

    send(client_fd, response, response_length, 0);

}



void handle_calc(int client_fd, char *query) {

    int a = 0, b = 0;

    sscanf(query, "a=%d&b=%d", &a, &b);

    int sum = a + b;

    char response[BUFFER_SIZE];

    int response_length = snprintf(response, sizeof(response),

        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<html><body>"
        "<p>Sum of %d and %d is %d</p>"
        "</body></html>", a, b, sum);
    
    send(client_fd, response, response_length, 0);

}

void handle_request(int client_fd) {

    char buffer[BUFFER_SIZE];

    int received_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (received_bytes <= 0) {

        close(client_fd);

        return;

    }

    buffer[received_bytes] = '\0';

    pthread_mutex_lock(&stats_mutex);

    request_count++;

    total_received_bytes += received_bytes;

    pthread_mutex_unlock(&stats_mutex);

    char method[BUFFER_SIZE], path[BUFFER_SIZE], query[BUFFER_SIZE];

    sscanf(buffer, "%s %s", method, path);
    
    if (strncmp(path, "/static/", 8) == 0) {

        handle_static(client_fd, path);

    } else if (strncmp(path, "/stats", 6) == 0) {

        handle_stats(client_fd);

    } else if (strncmp(path, "/calc", 5) == 0) {

        char *query_start = strchr(path, '?');

        if (query_start) {

            strcpy(query, query_start + 1);

            handle_calc(client_fd, query);

        } else {

            send_404(client_fd);

        }

    } else {

        send_404(client_fd);

    }

    close(client_fd);

}



void *client_handler(void *arg) {

    int client_fd = *(int *)arg;

    free(arg);

    handle_request(client_fd);

    return NULL;

}



int start_server(int port) {

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {

        perror("Socket creation failed");

        exit(1);

    }

    struct sockaddr_in server_addr = {

        .sin_family = AF_INET,

        .sin_port = htons(port),

        .sin_addr.s_addr = INADDR_ANY

    };

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {

        perror("Bind failed");

        close(server_fd);

        exit(1);

    }

    if (listen(server_fd, 10) == -1) {

        perror("Listen failed");

        close(server_fd);

        exit(1);

    }

    printf("Server started on port %d\n", port);

    return server_fd;

}



int main(int argc, char *argv[]) {

    int port = DEFAULT_PORT;

    if (argc == 3 && strcmp(argv[1], "-p") == 0) {

        port = atoi(argv[2]);

    }

    int server_fd = start_server(port);

    while (1) {

        struct sockaddr_in client_addr;

        socklen_t client_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (*client_fd == -1) {

            perror("Accept failed");

            free(client_fd);

            continue;

        }

        pthread_t thread;

        pthread_create(&thread, NULL, client_handler, client_fd);

        pthread_detach(thread);

    }

    close(server_fd);

    return 0;
    
}
