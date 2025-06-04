#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define RP_LOG_STR "[REVERSE PROXY %d]: %s\n"
//#define MAX_SV 10
#define INIT_SV 3

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

int rp_id; // id for the reverse proxy

int lb_fd; // socket for load balancer
int sv_sockets[INIT_SV]; // socket for each server

pid_t sv_p_ids[INIT_SV]; // process id for each server

void log_msg(const char* msg) {
    printf(RP_LOG_STR, rp_id, msg);
}

// close the open server sockets
void cleanup() {
    for (int sv_idx = 0; sv_idx < INIT_SV; sv_idx++) {
        if (sv_sockets[sv_idx] != -1) {
            close(sv_sockets[sv_idx]);
            sv_sockets[sv_idx] = -1;
        }
    }
}

void handle_sigterm(int sig) {
    cleanup();
    const char msg[] = "[REVERSE PROXY]: Received SIGTERM. Terminating\n";
    write(STDERR_FILENO, msg, sizeof(msg)-1);
    _exit(0);
}
 
void start_servers() {
    for (int sv_id = 0; sv_id < INIT_SV; sv_id++) {
        int sv[2]; // socket pair
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
            printf("socketpair error %d\n", sv_id); 
            perror("socketpair");
            exit(EXIT_FAILURE);
        }

        pid_t p_id = fork();
        if (p_id < 0) {
            printf("Fork error %d\n", sv_id);
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (p_id == 0) {
            // Child process: exec server
            close(sv[0]);

            char index_str[10], fd_str[10];
            snprintf(index_str, sizeof(index_str), "%d", sv_id + rp_id * INIT_SV);
            snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);

            execl("./server", "server", index_str, fd_str, NULL);
            perror("execl");
            _exit(EXIT_FAILURE);
        } else {
            // Parent process
            close(sv[1]);
            sv_sockets[sv_id] = sv[0];
            sv_p_ids[sv_id] = p_id;
        }
    }
}

int main(int argc, char* argv[]) {
    // check if the reverse proxy script was called in the right way
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <reverse_proxy_id> <socket_fd>\n", argv[0]);
        return 1;
    }

    // activate sigterm signal handler
    signal(SIGTERM, handle_sigterm);

    rp_id = atoi(argv[1]);
    lb_fd = atoi(argv[2]);

    int rp_fd; // socket

    rp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (rp_fd < 0) {
        perror("socket");
        exit(1);
    }

    log_msg("Started");

    struct ProcessInform rp_inf = { REVERSE_PROXY, rp_id, getpid() };
    if (write(lb_fd, &rp_inf, sizeof(rp_inf)) != sizeof(rp_inf)) {
        perror("write to lb");
    }

    start_servers();

    // round-rubin index for server selection
    int next_sv_idx = 0;

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        // add load balancer socket to the set
        FD_SET(lb_fd, &read_fds);
        int max_fd = lb_fd;

        // add server sockets to the set
        for (int i = 0; i < INIT_SV; i++) {
            if (sv_sockets[i] != -1) {
                FD_SET(sv_sockets[i], &read_fds);
                if (sv_sockets[i] > max_fd) max_fd = sv_sockets[i];
            }
        }
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select");
            continue;
        }

        // check for load balancer message
        if (FD_ISSET(lb_fd, &read_fds)) {
            struct Packet pck;
            ssize_t bytes_read = read(lb_fd, &pck, sizeof(pck));
            
            if (bytes_read == 0) {
                log_msg("Load balancer disconnected");
                break;
            } else if (bytes_read != sizeof(pck)) {
                perror("read from lb");
                continue;
            }
            
            // Find next available server (round-robin)

            if (sv_sockets[next_sv_idx] != -1) {
                if (write(sv_sockets[next_sv_idx], &pck, sizeof(pck)) == sizeof(pck)) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), 
                                "Forwarded client %d to server %d", 
                                pck.client_id, next_sv_idx);
                    log_msg(msg);
                } else {
                    // Write failed - server disconnected
                    close(sv_sockets[next_sv_idx]);
                    sv_sockets[next_sv_idx] = -1;
                }
            }
            next_sv_idx = (next_sv_idx + 1) % INIT_SV;  
        }

        // check for server message
        for (int sv_idx = 0; sv_idx < INIT_SV; sv_idx++) {
            if (sv_sockets[sv_idx] == -1) continue;
            
            if (FD_ISSET(sv_sockets[sv_idx], &read_fds)) {
                struct ProcessInform inf;
                ssize_t bytes_read = read(sv_sockets[sv_idx], &inf, sizeof(inf));
                
                if (bytes_read == 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Server %d disconnected", sv_idx);
                    log_msg(msg);
                    close(sv_sockets[sv_idx]);
                    sv_sockets[sv_idx] = -1;
                } 
                else if (bytes_read == sizeof(inf)) {
                    ssize_t bytes_written = write(lb_fd, &inf, sizeof(inf));
                    if (bytes_written != sizeof(inf)) {
                        perror("write to lb");
                    }
                }
                else if (bytes_read < 0 && errno != EINTR) {
                    perror("read from sv");
                }
                else {
                    fprintf(stderr, "Incomplete message from SV %d (%zd/%zu)\n", 
                            sv_idx, bytes_read, sizeof(inf));
                }
            }
        }
    }

    return 0;
}