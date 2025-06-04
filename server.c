#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <math.h>

#define SV_LOG_STR "[SERVER %d]: %s\n"

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

int sv_id;
int rp_fd;

void log_msg(const char* msg) {
    printf(SV_LOG_STR, sv_id, msg);
}

void handle_sigterm(int sig) {
    const char msg[] = "[SERVER]: Received SIGTERM. Terminating\n";
    write(STDERR_FILENO, msg, sizeof(msg)-1);
    _exit(0);
}

int main(int argc, char* argv[]) {
    // check if the server script was called in the right way
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_id> <socket_fd>\n", argv[0]);
        return 1;
    }

    // activate sigterm signal handler
    signal(SIGTERM, handle_sigterm);

    sv_id = atoi(argv[1]);
    rp_fd = atoi(argv[2]);

    log_msg("Started");

    struct ProcessInform sv_inf = { SERVER, sv_id, getpid() };
    if (write(rp_fd, &sv_inf, sizeof(sv_inf)) != sizeof(sv_inf)) {
        perror("write to rp");
    }

    while (1) {
        struct Packet pck;

        ssize_t bytes_read = read(rp_fd, &pck, sizeof(pck));
        
        if (bytes_read < 0) {
            if (errno == EINTR) continue;  // Interrupted by signal
            perror("read");
            break;
        }
        
        if (bytes_read == 0) {
            log_msg("Reverse proxy closed connection");
            break;
        }
        
        if (bytes_read != sizeof(pck)) {
            log_msg("Incomplete packet received - discarding");
            continue;
        }

        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf), 
                "Processing client %d, value: %f", 
                pck.client_id, sqrt(pck.value));
        log_msg(log_buf);
    }
    
    return 0;
}