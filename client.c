#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CLIENT_SOCKET_PATH "/tmp/cl-lb"   // the socket path for the communication with the server

struct Packet {
    int client_id;
    float value;
};

int main(int argc, char *argv[]) {
    // check if the client script was called in the right way
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <client_id>\n", argv[0]);
        return 1;
    }

    // obtain the client id
    int client_id = atoi(argv[1]);
    printf("Client id: %d\n", client_id);

    // prepare the values
    char input[100]; // char array to read the input to
    float value; // converted value of the user input
    char *endptr; // 

    // ask for a floating point number
    printf("Please enter a non-negative floating point number: ");

    // read the input
    if (fgets(input, sizeof(input), stdin) == NULL) {
        // read returned NULL
        fprintf(stderr, "Read unsuccessful\n");
        return 1;
    }

    // convert str input to float
    value = strtof(input, &endptr);

    // check if the conversion was successful
    if (endptr == input || (*endptr != '\n' && *endptr != '\0')) {
        // conversion unsuccessful
        fprintf(stderr, "Invalid floating point number\n");
        return 1;
    }

    struct Packet pckt = { client_id, value };

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CLIENT_SOCKET_PATH);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    write(sock, &pckt, sizeof(pckt));
    printf("[CLIENT %d] Sent %.3f to server.\n", client_id, value);

    close(sock);
    return 0;
}

// int get_load_balancer_pid() {
//     // execute the command
//     FILE *fp = popen("ps -ef | grep './load_balancer' | grep -v grep", "r"); // create a pipe that includes the command output

//     // pipe check
//     if (!fp) {
//         fprintf(stderr, "Error executing command");
//         return -1;
//     }

//     // read the output of the pipe 
//     char line[512]; // the output line

//     // command output check
//     if (fgets(line, sizeof(line), fp) == NULL) {
//         pclose(fp); // close the pipe
//         fprintf(stderr, "Error reading command output");
//         return -1;
//     }

//     pclose(fp); // close the pipe

//     char user[100]; // value to store the user, redundant
//     int pid; // value of the process id
    
//     sscanf(line, "%s %d", user, &pid); // extract the values from the command
    
//     return pid;
// }

// int main(int argc, char *argv[]) {
//     // check if the client script was called in the right way
//     if (argc != 2) {
//         fprintf(stderr, "Usage: %s <client_id>\n", argv[0]);
//         return 1;
//     }

//     // obtain the client id
//     int client_id = atoi(argv[1]);
//     printf("Client id: %d\n", client_id);

//     // prepare the values
//     char input[100]; // char array to read the input to
//     float number; // converted value of the user input
//     char *endptr; // 

//     // ask for a floating point number
//     printf("Please enter a non-negative floating point number: ");

//     // read the input
//     if (fgets(input, sizeof(input), stdin) == NULL) {
//         // read returned NULL
//         fprintf(stderr, "Read unsuccessful\n");
//         return 1;
//     }

//     // convert str input to float
//     number = strtof(input, &endptr);

//     // check if the conversion was successful
//     if (endptr == input || (*endptr != '\n' && *endptr != '\0')) {
//         // conversion unsuccessful
//         fprintf(stderr, "Invalid floating point number\n");
//         return 1;
//     }

//     // non-negativity check
//     if (number < 0.0f) {
//         // negative number
//         fprintf(stderr, "Invalid non-negative floating point number\n");
//         return 1;
//     }

//     printf("You entered: %f\n", number);

//     int pid = get_load_balancer_pid();
//     if (pid < 0) {
//         fprintf(stderr, "Server process couldn't be found \n");
//         return 1;
//     }
    
//     printf("Server process id is: %d \n", pid);

//     return 0;
// }

