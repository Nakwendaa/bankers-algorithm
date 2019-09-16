#ifndef SERVER_THREAD_H
#define SERVER_THREAD_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

//POSIX library for threads
#include <pthread.h>
#include <unistd.h>

extern bool accepting_connections;
extern int num_server_threads;


typedef struct server_thread server_thread;
typedef struct request request;

struct server_thread
{
    unsigned int id;
    pthread_t pt_tid;
    pthread_attr_t pt_attr;
    int *max_rsc;
    int *allocated;
    request *previous_request;
};

extern server_thread *threads;

struct request {
    char *cmd;
    int *args;
};

void destroy_request(request *req);
int *vec_cpy (int *vec1, int *vec2, int len);
int *sub (int *vec1, int *vec2, int len);
int *add (int *vec1, int *vec2, int len);
int hasNegative(int *vec, int len);
int isNeg(int *vec, int len);
int isNull (int *vec, int len);

int *extract_args (char *string);
char *extract_cmd (char *string);
request *getRequest(char *msg);
int find_error(char *msg);
char *process_error(char *msg, server_thread *st);

void st_open_socket (int port_number);
int st_init (void);
bool banker_algo(request *request, server_thread *st);
void st_process_requests(server_thread * st, int socket_fd);
void st_signal (void);
void *st_code (void *);
void st_print_results (FILE *, bool);
#endif
