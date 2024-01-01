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
#include <pthread.h>
#include <signal.h>
#include <bits/types/sigset_t.h>

#define BUFFER_SIZE 500
#define MAX_SESSIONS 10  // Defina o número máximo de sessões conforme necessário

typedef struct Session_Data{
    int session_id;
    char req_pipe_name[41];  // Assuming 40 characters for the name + 1 for null terminator
    char resp_pipe_name[41];
}Session_Data;



volatile sig_atomic_t show_state_flag = 0;
pthread_mutex_t signal_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t worker_threads[MAX_SESSIONS];
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;

int in = 0, out = 0, active_threads = 0;

void sigusr1_handler(int signo) {
    pthread_mutex_lock(&signal_mutex);
    show_state_flag = 1;
    pthread_mutex_unlock(&signal_mutex);
}

void* handle_session(void* arg) {
    struct Session_Data *session_data = (struct Session_Data*)arg;
    printf("Here:%d\n", session_data->session_id);
    printf("Here:%s\n", session_data->req_pipe_name);
    printf("Here:%s\n", session_data->resp_pipe_name);
    char req_pipe_name_l[41];
    char resp_pipe_name_l[41];
    int req_pipe_fd;
    int rs;
    unsigned int event_id;
    strcpy(req_pipe_name_l, session_data->req_pipe_name);
    req_pipe_name_l[40] = '\0';  // Ensure null termination
    strcpy(resp_pipe_name_l, session_data->resp_pipe_name);
    resp_pipe_name_l[40] = '\0';  // Ensure null termination
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);
    while (1) {
        char buffer[BUFFER_SIZE];
        printf("%s\n", req_pipe_name_l);
        req_pipe_fd = open(req_pipe_name_l, O_RDONLY);
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
                return NULL;
            case 3:
                // Extract additional data
                size_t num_rows, num_cols;
                memcpy(&event_id, buffer + sizeof(char), sizeof(unsigned int));
                memcpy(&num_rows, buffer + sizeof(char) + sizeof(unsigned int), sizeof(size_t));
                memcpy(&num_cols, buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t), sizeof(size_t));
                int result = ems_create(event_id, num_rows, num_cols);
                rs = open(resp_pipe_name_l, O_WRONLY);
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
                rs = open(resp_pipe_name_l, O_WRONLY);
                if (rs == -1) {
                    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                write(rs, &result, sizeof(result));
                close(rs);
                fprintf(stderr, "[INFO]: Received OP_CODE 4\n");
                break;

            case 5:
                unsigned int *seats;
                memcpy(&event_id, buffer + sizeof(char), sizeof(unsigned int));
                size_t rows = get_event_rows(event_id);
                size_t cols = get_event_cols(event_id);
                
                seats = (unsigned int*)malloc(rows*cols * sizeof(unsigned int));
                rs = open(resp_pipe_name_l, O_WRONLY);
                write(rs, &rows, sizeof(rows));
                write(rs, &cols, sizeof(cols));
                ems_show(event_id, seats);
                write(rs, seats, rows*cols * sizeof(unsigned int));
                close(rs);
                fprintf(stderr, "[INFO]: Received OP_CODE 5\n");


                break;
            case 6:
                int num_events = get_num_events();  
                rs = open(resp_pipe_name_l, O_WRONLY);
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
    close(req_pipe_fd);
    // Clean up resources if needed

    // Update the active threads count
    pthread_mutex_lock(&buffer_mutex);
    active_threads--;
    pthread_mutex_unlock(&buffer_mutex);

    return NULL;
}




void enqueue(Session_Data request) {
    pthread_mutex_lock(&buffer_mutex);
    while (active_threads == MAX_SESSIONS) {
        pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
    }

    in = (in + 1) % MAX_SESSIONS;
    active_threads++;
    
    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_mutex);

    if (pthread_create(&worker_threads[in], NULL, handle_session, (void*)&request) != 0) {
        fprintf(stderr, "[ERR]: pthread_create failed\n");
        exit(EXIT_FAILURE);
    }
    pthread_join(worker_threads[in], NULL);
}

void dequeue() {
    pthread_mutex_lock(&buffer_mutex);

    while (in == out && active_threads > 0) {
        pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
    }

    if (active_threads > 0) {
        out = (out + 1) % MAX_SESSIONS;
        active_threads--;
        pthread_cond_signal(&buffer_not_empty);
    }

    pthread_mutex_unlock(&buffer_mutex);
}



int main(int argc, char* argv[]) {
    int server_pipe_fd;
    int id;
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
    // Set up the SIGUSR1 signal handler
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    while (1) {
        ssize_t ret = 0;
        // Read from the server pipe or request pipe based on the session
        char buffer[BUFFER_SIZE];
        ret = read(server_pipe_fd, buffer, BUFFER_SIZE - 1);
        pthread_mutex_lock(&signal_mutex);
        if (show_state_flag) {
            print_event_state();
            show_state_flag = 0;  // Reset the flag
        }
        pthread_mutex_unlock(&signal_mutex);
        if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            break;
        }

        char OP_CODE = buffer[0];
        switch (OP_CODE) {
            case 1: {
                // Handle OP_CODE 1
                char req_pipe_path[40];
                fprintf(stderr, "[INFO]: Received OP_CODE 1\n");

                Session_Data session_data;
                session_data.session_id = id;
                id++;
                strncpy(req_pipe_path, buffer + 1, 40);
                req_pipe_path[40] = '\0';  // Ensure null termination
                strcpy(session_data.req_pipe_name, req_pipe_path);
                strncpy(session_data.resp_pipe_name, buffer + 41, 40);
                session_data.resp_pipe_name[40] = '\0';  // Ensure null termination
                fprintf(stderr, "[INFO]: Request Pipe: %s\n", session_data.req_pipe_name);
                fprintf(stderr, "[INFO]: Response Pipe: %s\n", session_data.resp_pipe_name);
                enqueue(session_data);
                break;
            }

            default:
                break;
        }

        buffer[ret] = '\0';
    }

    // Close file descriptors and unlink pipe
    close(server_pipe_fd);
    unlink(pipe_path);

    ems_terminate();
    return 0;
}