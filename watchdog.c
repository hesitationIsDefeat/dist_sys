#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>

#define WD_LOG_STR "[WATCHDOG]: %s\n"
#define INFORM_STR "%s %d informed their pid %d\n"
#define LOAD_BALANCER_SOCKET_PATH "/tmp/lb-wd"   // the socket path for the communication with the load balancer
#define LOAD_BALANCER_AMOUNT 1
#define REVERSE_PROXY_AMOUNT_PER_LOAD_BALANCER 2
#define SERVER_AMOUNT_PER_REVERSE_PROXY 3
#define REVERSE_PROXY_AMOUNT (LOAD_BALANCER_AMOUNT * REVERSE_PROXY_AMOUNT_PER_LOAD_BALANCER)
#define SERVER_AMOUNT (REVERSE_PROXY_AMOUNT * SERVER_AMOUNT_PER_REVERSE_PROXY)

enum ProcessType { LOAD_BALANCER, REVERSE_PROXY, SERVER };

struct ProcessInform {
    enum ProcessType type;
    int p_idx;
    pid_t p_id;
};

int lb_sockets[LOAD_BALANCER_AMOUNT] = {-1};

pid_t lb_p_ids[LOAD_BALANCER_AMOUNT] = {0};
pid_t rp_p_ids[REVERSE_PROXY_AMOUNT] = {0};
pid_t sv_p_ids[SERVER_AMOUNT] = {0};

const char* process_to_string(enum ProcessType type) {
    switch (type)
    {
        case LOAD_BALANCER:
            return "Load Balancer";
        case REVERSE_PROXY:
            return "Reverse Proxy";
        case SERVER:
            return "Server";
        default:
            return "Unknown process";
    }
}

void log_msg(const char* msg) {
    printf(WD_LOG_STR, msg);
}

// close the load balancer sockets
void cleanup() {
    for (int lb_idx = 0; lb_idx < LOAD_BALANCER_AMOUNT; lb_idx++) {
        if (lb_sockets[lb_idx] != -1) {
            close(lb_sockets[lb_idx]);
            lb_sockets[lb_idx] = -1;
        }
    }
}

void kill_process(pid_t pid) {
    if (kill(pid, SIGTERM) != 0) {
        perror("kill");
    }
}

void kill_processes() {
    // kill servers
    for (int sv_idx = 0; sv_idx < SERVER_AMOUNT; sv_idx++) {
        if (sv_p_ids[sv_idx] != 0) kill_process(sv_p_ids[sv_idx]);
    }

    // kill reverse proxies
    for (int rp_idx = 0; rp_idx < REVERSE_PROXY_AMOUNT; rp_idx++) {
        if (rp_p_ids[rp_idx] != 0) kill_process(rp_p_ids[rp_idx]);
    }

    // kill load balancers
    for (int lb_idx = 0; lb_idx < LOAD_BALANCER_AMOUNT; lb_idx++) {
        if (lb_p_ids[lb_idx] != 0) kill_process(lb_p_ids[lb_idx]);
    }

    // cleanup
    cleanup();
}

void handle_sigtstp(int sig) {
    const char msg1[] = "[WATCHDOG]: Received SIGTSTP. Terminating processes.\n";
    write(STDOUT_FILENO, msg1, sizeof(msg1)-1);
    kill_processes();
    // TODO handle cleanup
    const char msg2[] = "[WATCHDOG]: Ciao.\n";
    write(STDOUT_FILENO, msg2, sizeof(msg2)-1);
    _exit(0);
}

void start_load_balancers() {
    for (int lb_id = 0; lb_id < LOAD_BALANCER_AMOUNT; lb_id++) {
        int sv[2]; // socket pair
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
            perror("socketpair");
            exit(EXIT_FAILURE);
        }

        pid_t lb_p_id = fork();
        if (lb_p_id < 0) {
            printf("Fork error %d\n", lb_id);
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (lb_p_id == 0) {
            // Child process: exec reverse_proxy
            close(sv[0]);

            char index_str[10], fd_str[10];
            snprintf(index_str, sizeof(index_str), "%d", lb_id);
            snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);

            execl("./load_balancer", "load_balancer", index_str, fd_str, NULL);
            perror("execl");
            exit(EXIT_FAILURE);
        } else {
            // Parent process
            close(sv[1]);
            lb_sockets[lb_id] = sv[0];
            lb_p_ids[lb_id] = lb_p_id;
        }
    }
}

int main() {
    log_msg("Started");

    signal(SIGTSTP, handle_sigtstp);

    start_load_balancers();

    while (1) {
        fd_set readfds;
        int max_fd = -1;
        FD_ZERO(&readfds);
        
        // Add all LB sockets to the read set
        for (int i = 0; i < LOAD_BALANCER_AMOUNT; ++i) {
            if (lb_sockets[i] != -1) {
                FD_SET(lb_sockets[i], &readfds);
                if (lb_sockets[i] > max_fd) max_fd = lb_sockets[i];
            }
        }

        // check if there are no active sockets
        if (max_fd == -1) {
            sleep(1);
            continue;
        }

        // Wait for any of the sockets to become ready
        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }

        // Check which socket has data
        for (int i = 0; i < LOAD_BALANCER_AMOUNT; ++i) {
            if (lb_sockets[i] == -1) continue;

            if (FD_ISSET(lb_sockets[i], &readfds)) {
                struct ProcessInform inf;
                ssize_t bytes_read = read(lb_sockets[i], &inf, sizeof(inf));
                if (bytes_read == sizeof(inf)) {
                    switch (inf.type)
                    {
                    case LOAD_BALANCER:
                        if (inf.p_idx >= 0 && inf.p_idx < LOAD_BALANCER_AMOUNT) lb_p_ids[inf.p_idx] = inf.p_id;
                        break;
                    case REVERSE_PROXY:
                        if (inf.p_idx >= 0 && inf.p_idx < REVERSE_PROXY_AMOUNT) rp_p_ids[inf.p_idx] = inf.p_id;
                        break;
                    case SERVER:
                        if (inf.p_idx >= 0 && inf.p_idx < SERVER_AMOUNT) sv_p_ids[inf.p_idx] = inf.p_id;
                        break;
                    default:
                        break;
                    }
                    char msg_buf[128];
                    snprintf(msg_buf, sizeof(msg_buf), INFORM_STR, process_to_string(inf.type), inf.p_idx, inf.p_id);
                    log_msg(msg_buf);
                } else if (bytes_read == 0) {
                    char msg_buf[64];
                    snprintf(msg_buf, sizeof(msg_buf), "LB %d closed the socket\n", i);
                    log_msg(msg_buf);
                    close(lb_sockets[i]);
                    lb_sockets[i] = -1;
                    // Optional: respawn logic here
                } else {
                    perror("read");
                }
            }
        }
    }
    return 0;
}