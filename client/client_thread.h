#ifndef CLIENTTHREAD_H
#define CLIENTTHREAD_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>

/* Port TCP sur lequel le serveur attend des connections.  */
extern int port_number;

/* Nombre de requêtes que chaque client doit envoyer.  */
extern int num_request_per_client;

/* Nombre de resources différentes.  */
extern int num_resources;

/* Quantité disponible pour chaque resource.  */
extern int *provisioned_resources;


typedef struct client_thread client_thread;
struct client_thread
{
    unsigned int id;
    pthread_t pt_tid;
    pthread_attr_t pt_attr;
};


void ct_init (client_thread *);
void ct_create_and_start (client_thread *);
void ct_wait_server (int num_clients, client_thread *client_threads);
int begin();
int pro();
int connexion(int thread_socket_fd);
void close_connection (int socket_fd, int client_id);
int *generate_req (int *allocated, int *max_rsc,  int request_id);
void send_ini (int socket_fd, int client_id, int *max_rsc);
int ct_open_socket ();
void init_addr_serv();
void st_print_results (FILE *, int);

#endif // CLIENTTHREAD_H
