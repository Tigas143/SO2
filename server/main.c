#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include <unistd.h>
#include <fcntl.h>

#define BUFFER_SIZE 500

char req_pipe_name_g[41];  // Assuming 40 characters for the name + 1 for null terminator
char resp_pipe_name_g[41];

void handle_session() {
    int req_pipe_fd;
    int rs;
    unsigned int event_id;
    while (1) {
        char buffer[BUFFER_SIZE];
        printf("%s\n", req_pipe_name_g);
        req_pipe_fd = open(req_pipe_name_g, O_RDONLY);
        ssize_t ret = read(req_pipe_fd, buffer, BUFFER_SIZE - 1);
        if (ret == 0) {
            // ret == 0 indicates EOF
            fprintf(stderr, "[INFO]: request pipe closed\n");
            break;
        } else if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
            break;
        }
        char OP_CODE;
        memcpy(&OP_CODE, buffer, sizeof(char));
        printf("OP_CODE: %d\n", OP_CODE);
        switch (OP_CODE) {
            case 2:
                close(req_pipe_fd);
                return;
            case 3:
                // Extract additional data
                size_t num_rows, num_cols;
                memcpy(&event_id, buffer + sizeof(char), sizeof(unsigned int));
                memcpy(&num_rows, buffer + sizeof(char) + sizeof(unsigned int), sizeof(size_t));
                memcpy(&num_cols, buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t), sizeof(size_t));
                int result = ems_create(event_id, num_rows, num_cols);
                rs = open(resp_pipe_name_g, O_WRONLY);
                if (rs == -1) {
                    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                write(rs, &result, sizeof(result));
                close(rs);
                
                fprintf(stderr, "[INFO]: Received OP_CODE 3\n");

                break;

            case 4:
                size_t num_seats;
                memcpy(&event_id, buffer + sizeof(char), sizeof(unsigned int));
                memcpy(&num_seats, buffer + sizeof(char) + sizeof(unsigned int), sizeof(size_t));
                size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
                memcpy(xs, buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t), num_seats * sizeof(size_t));
                memcpy(ys, buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t) + num_seats * sizeof(size_t), num_seats * sizeof(size_t));
                result = ems_reserve(event_id, num_seats, xs, ys);
                fprintf(stderr, "[INFO]: Received OP_CODE 4\n");
                rs = open(resp_pipe_name_g, O_WRONLY);
                if (rs == -1) {
                    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                write(rs, &result, sizeof(result));
                close(rs);
                break;

            case 5:
                unsigned int *seats;
                memcpy(&event_id, buffer + sizeof(char), sizeof(unsigned int));
                size_t rows = get_event_rows(event_id);
                size_t cols = get_event_cols(event_id);
                
                seats = (unsigned int*)malloc(rows*cols * sizeof(unsigned int));
                rs = open(resp_pipe_name_g, O_WRONLY);
                write(rs, &rows, sizeof(rows));
                write(rs, &cols, sizeof(cols));
                ems_show(event_id, seats);
                write(rs, seats, rows*cols * sizeof(unsigned int));
                close(rs);
                fprintf(stderr, "[INFO]: Received OP_CODE 5\n");


                break;
            case 6:
                int num_events = get_num_events();  
                rs = open(resp_pipe_name_g, O_WRONLY);
                if (rs == -1) {
                    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                write(rs, &num_events, sizeof(num_events));
                // Allocate memory for ids dynamically
                unsigned int* ids = malloc((unsigned int)num_events * sizeof(unsigned int));
                if (ids == NULL) {
                    fprintf(stderr, "[ERR]: Memory allocation failed\n");
                    exit(EXIT_FAILURE);
                }
                ems_list_events(ids, num_events);
                write(rs, ids, (unsigned int)num_events * sizeof(unsigned int));
                fprintf(stderr, "[INFO]: Received OP_CODE 6\n");
                free(ids);
                close(rs);
                break;


            default:
                fprintf(stderr, "[ERR]: Unknown OP_CODE: %c\n", OP_CODE);
                // TODO: Handle unknown OP_CODE if needed
                break;
        }
        fprintf(stderr, "[INFO]: received %zd B from request pipe\n", ret);
        buffer[ret] = '\0';
        fputs(buffer, stdout);
        // TODO: Perform actions based on the received data
    }
}

int main(int argc, char* argv[]) {
    int session_id = 0;
    int server_pipe_fd;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <pipe_path> [delay]\n", argv[0]);
        return 1;
    }

    char* endptr;
    unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
    if (argc == 3) {
        unsigned long int delay = strtoul(argv[2], &endptr, 10);

        if (*endptr != '\0' || delay > UINT_MAX) {
            fprintf(stderr, "Invalid delay value or value too large\n");
            return 1;
        }

        state_access_delay_us = (unsigned int)delay;
    }

    if (ems_init(state_access_delay_us)) {
        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    char* pipe_path = argv[1];

    if (mkfifo(pipe_path, 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    server_pipe_fd = open(pipe_path, O_RDONLY);

    while (1) {
        ssize_t ret = 0;
        // Read from the server pipe or request pipe based on the session
        char buffer[BUFFER_SIZE];
        ret = read(server_pipe_fd, buffer, BUFFER_SIZE - 1);
        if (ret == 0) {
            // ret == 0 indicates EOF
            fprintf(stderr, "[INFO]: pipe closed\n");
            break;
        } else if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            break;
        }

        char OP_CODE = buffer[0];
        switch (OP_CODE) {
            case 1: {
                // Handle OP_CODE 1
                fprintf(stderr, "[INFO]: Received OP_CODE 1\n");

                // Extract pipe names
                strncpy(req_pipe_name_g, buffer + 1, 40);
                req_pipe_name_g[40] = '\0';  // Null-terminate the string

                strncpy(resp_pipe_name_g, buffer + 41, 40);
                resp_pipe_name_g[40] = '\0';  // Null-terminate the string
                printf("resp_pipe_name_g: %s\n", resp_pipe_name_g);
                fprintf(stderr, "[INFO]: Request Pipe: %s\n", req_pipe_name_g);
                fprintf(stderr, "[INFO]: Response Pipe: %s\n", resp_pipe_name_g);

                // TODO: Save the pipe names or perform further actions
                session_id = 1;
                break;
            }

            default:
                fprintf(stderr, "[ERR]: Unknown OP_CODE: %c\n", OP_CODE);
                break;
        }

        fprintf(stderr, "[INFO]: received %zd B\n", ret);
        buffer[ret] = '\0';
        fputs(buffer, stdout);

        if (session_id == 1) {
            // Handle the session by reading from the request pipe
            printf("Handling session\n");
            handle_session();
            break;
        }

        // TODO: Write new client to the producer-consumer buffer
    }

    // Close file descriptors and unlink pipe
    close(server_pipe_fd);
    unlink(pipe_path);

    ems_terminate();
    return 0;
}