#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define LB_LOG_STR "[LOAD BALANCER]: %s\n"
#define CLIENT_SOCKET_PATH "/tmp/cl-lb"   // the socket path for the communication with the client
//#define MAX_RP 10
#define INIT_RP 2

enum ProcessType { LOAD_BALANCER, REVERSE_PROXY, SERVER };

struct ProcessInform {
    enum ProcessType type;
    int p_idx;
    pid_t p_id;
};

struct Packet {
    int client_id;
    float value;
};

int lb_id; // id of the loadbalancer
int wd_fd; // socket for watchdog
int rp_sockets[INIT_RP] = {-1}; // socket for each reverse proxy
pid_t rp_p_ids[INIT_RP] = {0}; // an array for the process ids for each reverse proxy

// a function to choose between the available reverse proxies when a client request arrives
int choose_rp(int client_id) {
    return client_id % INIT_RP;
}

void log_msg(const char* msg) {
    printf(LB_LOG_STR, msg);
}

// close the reverse proxy sockets
void cleanup() {
    for (int rp_idx = 0; rp_idx < INIT_RP; rp_idx++) {
        if (rp_sockets[rp_idx] != -1) {
            close(rp_sockets[rp_idx]);
            rp_sockets[rp_idx] = -1;
        }
    }
}


void handle_sigterm(int sig) {
    cleanup();
    const char msg[] = "[LOAD BALANCER]: Received SIGTERM. Terminating\n";
    write(STDERR_FILENO, msg, sizeof(msg)-1);
    _exit(0);
}

void start_reverse_proxies() {
    for (int rp_id = 0; rp_id < INIT_RP; rp_id++) {
        int sv[2]; // socket pair
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
            printf("socketpair error %d\n", rp_id);
            perror("socketpair");
            exit(EXIT_FAILURE);
        }

        pid_t p_id = fork();
        if (p_id < 0) {
            printf("Fork error %d\n", rp_id);
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (p_id == 0) {
            // Child process: exec reverse_proxy
            close(sv[0]);

            char index_str[10], fd_str[10];
            snprintf(index_str, sizeof(index_str), "%d", rp_id + lb_id * INIT_RP);
            snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);

            execl("./reverse_proxy", "reverse_proxy", index_str, fd_str, NULL);
            perror("execl");
            _exit(EXIT_FAILURE);
        } else {
            // Parent process
            close(sv[1]);
            rp_sockets[rp_id] = sv[0];
            rp_p_ids[rp_id] = p_id;
        }
    }
}

int main(int argc, char* argv[]) {
    // check if the load balancer script was called in the right way
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <load_balancer_id> <socket_fd>\n", argv[0]);
        return 1;
    }

    // activate sigterm signal handler
    signal(SIGTERM, handle_sigterm);

    lb_id = atoi(argv[1]); // extract load balancer id
    wd_fd = atoi(argv[2]); // extract the socket to communicate with the watchdog

    int lb_fd; // socket
    struct sockaddr_un addr;

    // clear the socket
    unlink(CLIENT_SOCKET_PATH);

    lb_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lb_fd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLIENT_SOCKET_PATH, sizeof(addr.sun_path)-1);

    if (bind(lb_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(lb_fd, 5) < 0) {
        perror("listen");
        exit(1);
    }

    log_msg("Started");

    struct ProcessInform lb_inf = {LOAD_BALANCER, lb_id, getpid()};
    if (write(wd_fd, &lb_inf, sizeof(lb_inf)) != sizeof(lb_inf)) {
        perror("write to wd");
    }

    start_reverse_proxies();

    while (1) {
        fd_set read_fds;
        int max_fd = lb_fd;
        FD_ZERO(&read_fds);
        // Add client socket to the set
        FD_SET(lb_fd, &read_fds);

        // Add all reverse proxy sockets to the set
        for (int i = 0; i < INIT_RP; i++) {
            if (rp_sockets[i] != -1) {
                FD_SET(rp_sockets[i], &read_fds);
                if (rp_sockets[i] > max_fd) max_fd = rp_sockets[i];
            }
            
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }

        // check for client message
        if (FD_ISSET(lb_fd, &read_fds)) {
            int cl_sc = accept(lb_fd, NULL, NULL);
            if (cl_sc < 0) {
                perror("accept");
                continue;
            }

            struct Packet pck;
            ssize_t bytes_read;
            while ((bytes_read = read(cl_sc, &pck, sizeof(pck)))) {
                if (bytes_read == sizeof(pck)) break;
                if (bytes_read < 0 && errno != EINTR) {
                    perror("read");
                    break;
                }
            }

            if (bytes_read != sizeof(pck)) {
                close(cl_sc);
                continue;
            }

            int rp_idx = choose_rp(pck.client_id);
            if (rp_sockets[rp_idx] == -1) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "RP %d socket closed, cannot forward", rp_idx);
                log_msg(err_buf);
                close(cl_sc);
                continue;
            }

            ssize_t bytes_written = write(rp_sockets[rp_idx], &pck, sizeof(pck));
            if (bytes_written != sizeof(pck)) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Failed to forward to RP %d (%zd/%zu)", 
                         rp_idx, bytes_written, sizeof(pck));
                log_msg(err_buf);
            } else {
                char bf[128];
                snprintf(bf, sizeof(bf), "Request from Client %d. Forwarding to Reverse Proxy %d", 
                         pck.client_id, rp_idx);
                log_msg(bf);
            }
            // cleanup
            close(cl_sc);
        }

        // check for reverse proxy message
        for (int rp_idx = 0; rp_idx < INIT_RP; rp_idx++) {
            if (rp_sockets[rp_idx] == -1) continue;
            
            if (FD_ISSET(rp_sockets[rp_idx], &read_fds)) {
                struct ProcessInform inf;
                ssize_t bytes_read = read(rp_sockets[rp_idx], &inf, sizeof(inf));
                
                if (bytes_read == 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Reverse Proxy %d disconnected", rp_idx);
                    log_msg(msg);
                    close(rp_sockets[rp_idx]);
                    rp_sockets[rp_idx] = -1;
                } 
                else if (bytes_read == sizeof(inf)) {
                    ssize_t bytes_written = write(wd_fd, &inf, sizeof(inf));
                    if (bytes_written != sizeof(inf)) {
                        perror("write to wd");
                    }
                }
                else if (bytes_read < 0 && errno != EINTR) {
                    perror("read from rp");
                }
                else {
                    fprintf(stderr, "Incomplete message from RP %d (%zd/%zu)\n", 
                            rp_idx, bytes_read, sizeof(inf));
                }
            }
        }
    }

    return 0;
}