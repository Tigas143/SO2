#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "api.h"

#define BUFFER_SIZE 300
#define PIPE_PATH_SIZE 41

char *req_pipe_path_g = NULL;
char *resp_pipe_path_g = NULL;
int req_pipe_fd = -1;
int resp_pipe_fd = -1;
int session_id;

int read_response(int* response) {
    if (resp_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
        return 1;
    }

    ssize_t bytes_read = read(resp_pipe_fd, response, sizeof(int));

    if (bytes_read == -1) {
        fprintf(stderr, "[ERR]: read response failed: %s\n", strerror(errno));
        return 1;
    }


    return 0;
}




int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
    int rx = open(server_pipe_path, O_WRONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return 1;
    }

    // Prepare setup message
    char setup_message[BUFFER_SIZE];
    char OP_CODE = 1;  // Change this according to your needs
    char formatted_req_pipe_path[PIPE_PATH_SIZE];
    char formatted_resp_pipe_path[PIPE_PATH_SIZE];
    snprintf(formatted_req_pipe_path, PIPE_PATH_SIZE, "%-40s", req_pipe_path);
    snprintf(formatted_resp_pipe_path, PIPE_PATH_SIZE, "%-40s", resp_pipe_path);
    
    // Add null terminator to the formatted names
    formatted_req_pipe_path[PIPE_PATH_SIZE - 1] = '\0';
    formatted_resp_pipe_path[PIPE_PATH_SIZE - 1] = '\0';

    req_pipe_path_g = malloc(PIPE_PATH_SIZE);
    resp_pipe_path_g = malloc(PIPE_PATH_SIZE);

    if (req_pipe_path_g == NULL || resp_pipe_path_g == NULL) {
        fprintf(stderr, "[ERR]: Memory allocation failed\n");
        return 1;
    }

    // Copy formatted names to dynamically allocated memory
    strcpy(req_pipe_path_g, formatted_req_pipe_path);
    strcpy(resp_pipe_path_g, formatted_resp_pipe_path);
    // Create request pipe
    if (unlink(req_pipe_path_g) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", resp_pipe_path, strerror(errno));
    }
    if (mkfifo(req_pipe_path_g, 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        return 1;
    }

    // Create response pipe
    if (access(resp_pipe_path_g, F_OK) == -1) {
        if (mkfifo(resp_pipe_path_g, 0640) != 0) {
            fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
            return 1;
        }
    }

    snprintf(setup_message, BUFFER_SIZE, "%c%s%s", OP_CODE, formatted_req_pipe_path, formatted_resp_pipe_path);

    // Send setup message to the server
    ssize_t bytes_written = write(rx, setup_message, sizeof(setup_message));
    if (bytes_written == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return 1;
    }
    resp_pipe_fd = open(resp_pipe_path_g, O_RDONLY);
    read(resp_pipe_fd, &session_id, sizeof(int));
    printf("Session id: %d\n", session_id);
    req_pipe_fd = open(req_pipe_path_g, O_WRONLY);
    printf("Sent setup message\n");
    close(rx);
    return 0;
}

int ems_quit(void) {
    printf("reached quit\n");
    // Send quit request
    if (req_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
        return 1;
    }

    char quit_message = 2;  // Some unique code for quit request
    ssize_t bytes_written = write(req_pipe_fd, &quit_message, sizeof(quit_message));

    if (bytes_written == -1) {
        fprintf(stderr, "[ERR]: write quit request failed: %s\n", strerror(errno));
        return 1;
    }
    close(req_pipe_fd);
    unlink(req_pipe_path_g);
    unlink(resp_pipe_path_g);
    free((void*)req_pipe_path_g);
    free((void*)resp_pipe_path_g);
    return 0;
}


int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
    // Send create request
    if (req_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
        return 1;
    }
    char create_message[BUFFER_SIZE];
    char OP_CODE = 3;  // Change this according to your needs
    size_t message_size = sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t) +
                      sizeof(size_t);
    memcpy(create_message, &OP_CODE, sizeof(char));
    memcpy(create_message + sizeof(char), &session_id, sizeof(int));  // Include session_id in the create message
    memcpy(create_message + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));
    memcpy(create_message + sizeof(char) + sizeof(int) + sizeof(unsigned int), &num_rows, sizeof(size_t));
    memcpy(create_message + sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t), &num_cols, sizeof(size_t));

    ssize_t bytes_written = write(req_pipe_fd, create_message, message_size);
    if (bytes_written == -1) {
        fprintf(stderr, "[ERR]: write create request failed: %s\n", strerror(errno));
        return 1;
    }
    // Wait for and handle the response
    int response = 0;
    if (read_response(&response) != 0) {
        return 1;
    }
    return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
    // Send reserve request
    if (req_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
        return 1;
    }
    

     size_t message_size = sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t) +
                      num_seats * sizeof(size_t) + num_seats * sizeof(size_t);
    char reserve_message[message_size];
    char OP_CODE = 4;  // Change this according to your needs
    memcpy(reserve_message, &OP_CODE, sizeof(char));
    memcpy(reserve_message + sizeof(char), &session_id, sizeof(int));  // Include session_id in the reserve message
    memcpy(reserve_message + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));
    memcpy(reserve_message + sizeof(char) + sizeof(int) + sizeof(unsigned int), &num_seats, sizeof(size_t));
    memcpy(reserve_message + sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t), xs, num_seats * sizeof(size_t));
    memcpy(reserve_message + sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t) + num_seats * sizeof(size_t), ys, num_seats * sizeof(size_t));

    ssize_t bytes_written = write(req_pipe_fd, reserve_message, message_size);
    if (bytes_written == -1) {
        fprintf(stderr, "[ERR]: write reserve request failed: %s\n", strerror(errno));
        return 1;
    }
    int response = 0;
    if (read_response(&response) != 0) {
        return 1;
    }
    return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
    // Send show request
    printf("%s\n", req_pipe_path_g);
    if (req_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
        return 1;
    }
    char show_message[BUFFER_SIZE];
    char OP_CODE = 5;  // Change this according to your needs
    size_t message_size = sizeof(char) + sizeof(int) + sizeof(unsigned int);
    memcpy(show_message, &OP_CODE, sizeof(char));
    memcpy(show_message + sizeof(char), &session_id, sizeof(int));  // Include session_id in the show message
    memcpy(show_message + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));


    ssize_t bytes_written = write(req_pipe_fd, show_message, message_size);
    if (bytes_written == -1) {
        fprintf(stderr, "[ERR]: write show request failed: %s\n", strerror(errno));
        return 1;
    }
    if (resp_pipe_fd == -1) {
        fprintf(stderr, "[ERR]:  response pipe failed: %s\n", strerror(errno));
        return 1;
    }
    int result;
    size_t rows,cols;
    unsigned int *seats;
    ssize_t bytes_read = read(resp_pipe_fd, &result, sizeof(int));
    if(result == 1){
        return 1;
    }
    bytes_read = read(resp_pipe_fd, &rows, sizeof(size_t));
    bytes_read = read(resp_pipe_fd, &cols, sizeof(size_t));
    if(bytes_read == -1){
        fprintf(stderr, "[ERR]: read show request failed: %s\n", strerror(errno));
        return 1;
    }
    printf("rows: %ld\n", rows);
    printf("cols: %ld\n", cols);
    seats = (unsigned int*)malloc(rows*cols * sizeof(unsigned int));
    bytes_read = read(resp_pipe_fd, seats, rows*cols * sizeof(unsigned int));

    for (size_t i = 0; i < rows; i++) {
        for (size_t j = 0; j < cols; j++) {
            char buffer[16];
            int written = snprintf(buffer, sizeof(buffer), "%u ", seats[i * cols + j]);
            if (written < 0 || (size_t)written >= sizeof(buffer)) {
                // Handle error in snprintf
                // Optionally, you can choose to skip this line or terminate the program
                perror("Error in snprintf");
                free(seats);
                return 1;
            }
            write(out_fd, buffer, (size_t)written);
        }
        // Write newline character after each row
        write(out_fd, "\n", 1);
    }
    free(seats);
    return 0;
}

int ems_list_events(int out_fd) {
    // Send list events request
    if (req_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
        return 1;
    }

    char list_events_message[BUFFER_SIZE];
    char OP_CODE = 6;  // Change this according to your needs
    size_t message_size = sizeof(char) + sizeof(int);
    memcpy(list_events_message, &OP_CODE, sizeof(char));
    memcpy(list_events_message + sizeof(char), &session_id, sizeof(int));  // Include session_id in the list events message


    ssize_t bytes_written = write(req_pipe_fd, list_events_message, message_size);

    if (bytes_written == -1) {
        fprintf(stderr, "[ERR]: write list events request failed: %s\n", strerror(errno));
        return 1;
    }
    int num_events, result;
    if (resp_pipe_fd == -1) {
        fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
        return 1;
    }
    ssize_t bytes_read = read(resp_pipe_fd, &result, sizeof(int));
    if(result == 1){
        return 1;
    }
    bytes_read = read(resp_pipe_fd, &num_events, sizeof(int));
    if (bytes_read == -1) {
        fprintf(stderr, "[ERR]: read list events request failed: %s\n", strerror(errno));
        return 1;
    }
    unsigned int* ids = malloc((unsigned int)num_events * sizeof(unsigned int));
    bytes_read = read(resp_pipe_fd, ids, (unsigned int)num_events * sizeof(unsigned int));
    // Write event IDs to out_fd in the specified format
    if (num_events == 0) {
        write(out_fd, "No events\n", 10);
    }
    for (int i = 0; i < num_events; i++) {
        char buffer[32];  // Adjust the buffer size as needed
        int written = snprintf(buffer, sizeof(buffer), "Event: %u\n", ids[i]);
        if (written < 0 || (size_t)written >= sizeof(buffer)) {
            // Handle error in snprintf
            perror("Error in snprintf");
            free(ids);
            return 1;
        }
        write(out_fd, buffer, (size_t)written);
    }

    // Free dynamically allocated memory
    free(ids);

    return 0;
}
