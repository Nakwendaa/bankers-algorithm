
#define _XOPEN_SOURCE 500 /* This `define` tells unistd to define usleep and random.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/errno.h>
#include <arpa/inet.h>
#include "client_thread.h"


/* --------------------------- VARIABLES GLOBALES -------------------------- */

/* Adresse de destination (serveur)*/
struct sockaddr_in serv_addr;
/* Socket du client principal */
int client_socket_fd = -1;
/* Numéro de port du socket client */
int port_number = -1;
/* Nombre de requêtes par client */
int num_request_per_client = -1;
/* Nombre de ressources différentes */
int num_resources = -1;
/* Provisionnement des ressources via un vecteur d'int */
int *provisioned_resources = NULL;


enum { NUL = '\0' };

enum {
    /* Configuration constants.  */
            max_wait_time = 30,
    server_backlog_size = 5
};


/* Variable d'initialisation des threads clients. */
unsigned int count = 0;


/* --- Variables du journal --- */
/* Nombre de requête acceptée (ACK reçus en réponse à REQ) */
pthread_mutex_t lock_count_accepted_ct;
unsigned int count_accepted = 0;
/* Nombre de requête en attente (WAIT reçus en réponse à REQ) */
pthread_mutex_t lock_count_on_wait;
unsigned int count_on_wait = 0;
/* Nombre de requête refusée (REFUSE reçus en réponse à REQ) */
pthread_mutex_t lock_count_invalid_ct;
unsigned int count_invalid = 0;
/* Nombre de client qui se sont terminés correctement (ACK reçu en réponse à CLO) */
pthread_mutex_t lock_count_dispatched_ct;
unsigned int count_dispatched = 0;
/* Nombre total de requêtes envoyées. */
pthread_mutex_t lock_request_sent;
unsigned int request_sent = 0;


/* ---------------------------------- FIN ---------------------------------- */








/* --------------------------------- FONCTIONS ------------------------------ */



void init_addr_serv() {

    /*---- Configure settings of the server address struct ----*/
    /* Mettre à 0 tous les bytes de serv_addr. */
    memset (&serv_addr, 0, sizeof (serv_addr));
    /* Address family = Internet */
    serv_addr.sin_family = AF_INET;
    /* Set port number, using htons function to use proper byte order */
    serv_addr.sin_port = htons (port_number);
    /* Set IP address to localhost */
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
}


/* Fonction ct_opensocket pour ouvrir le socket du client */
int ct_open_socket () {

    /*---- Create the socket. The three arguments are: ----*/
    /* 1) Internet domain 2) Stream socket 3) Default protocol (TCP in this case) */
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);


    if (socket_fd < 0) {
        perror ("ERROR opening client socket\n");
        return -1;
    }
    return socket_fd;
}

int connexion(int thread_socket_fd) {
    if (connect (thread_socket_fd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
        perror("Error connecting"); // Connexion refusée.
        return 1;
    }
    return 0;
}


int begin() {

    /* Obtenir un socket pour le thread */
    client_socket_fd = ct_open_socket();
    if (client_socket_fd == -1) {
        return 3; // Impossible d'ouvrir socket.
    }
    int err = connexion(client_socket_fd);
    if (err) {
        return 1; // Connexion refusée
    }
    char req[100] = {0};
    char rep[200] = {0};
    char temp[100] = {0};

    strcat (req, "BEG ");
    sprintf(temp, "%d", num_resources);
    strcat (req, temp);
    strcat (req, "\n");

    ssize_t error;
    error = send(client_socket_fd, req, strlen(req) + 1, 0);
    if (error == 0) {
        close(client_socket_fd);
        return 2; // Time out serveur;
    }
    printf ("Client (sent) : %s\n", req);

    /* Lire la réponse dans rep */
    error = recv (client_socket_fd, rep, sizeof(rep), 0);
    if (error == 0) {
        close(client_socket_fd);
        return 2; // Time out serveur;
    }
    printf("Client (received) : %s\n", rep);
    return 0;
}


int pro() {
    char req[200] = {0};
    char rep[200] = {0};

    strcat (req, "PRO ");
    int i;
    for (i = 0; i < num_resources; i++) {
        char temp[200] = {0};
        sprintf(temp, "%d ", provisioned_resources[i]);
        strcat(req, temp);
    }
    strcat (req, "\n");
    ssize_t error;
    error = send(client_socket_fd, req, strlen(req) + 1, 0);
    if (error == -1) {
        close(client_socket_fd);
        return 2; // Time out serveur;
    }
    printf ("Client (sent) : %s\n", req);
    /* Lire la réponse dans rep */
    error = recv (client_socket_fd, rep, sizeof(rep), 0);
    if (error == 0) {
        close(client_socket_fd);
        return 2; // Time out serveur;
    }
    printf("Client (received) : %s\n", rep);
    return 0;
}


int *
generate_req(int *allocated, int *max_rsc, int request_id) {
    int *req = malloc (num_resources * sizeof(int));
    int flip[2] = {-1, 1};
    int cmpt_zero = 0;
    int r = -1;

    // Si c'est la dernière requête du client, on libère toutes les ressources
    if (request_id == num_request_per_client - 1) {
        for (int i = 0; i < num_resources; i++)
            req[i] = (-1) * allocated[i];
    }

        // Sinon on génère une requête aléatoire
    else {
        for (int i = 0; i < num_resources; i++) {
            r = -1;
            // Si rien est alloué on fait une demande strictement positive.
            // Sinon on fait une demande positive ou négative avec p = 1/2
            if (allocated[i] != 0 && allocated[i] != max_rsc[i]) {
                r = flip[rand() % 2];
            }
            if (allocated[i] == 0)
                r = 1;
            if (r > 0)
                r = rand() % (max_rsc[i] - allocated[i] + 1);
            else
                r *= rand() % (allocated[i] + 1);
            if (r == 0)
                cmpt_zero++;
            req[i] = r;
        }
        if (cmpt_zero == num_resources) {
            for (int i = 0; i < num_resources; i++) {
                if (max_rsc[i] != 0) {
                    r = -1;
                    if (allocated[i] != 0 && allocated[i] != max_rsc[i] ) {
                        r = flip[rand() % 2];
                    }
                    if (allocated[i] == 0)
                        r = 1;
                    if (r > 0)
                        r = (rand() % (max_rsc[i] - allocated[i])) + 1;
                    else
                        r *= ((rand() % (allocated[i])) + 1);
                    req[i] = r;
                    break;
                }
            }
        }
    }
    return (req);
}


// Vous devez modifier cette fonction pour faire l'envoie des requêtes
// Les ressources demandées par la requête doivent être choisies aléatoirement
// (sans dépasser le maximum pour le client). Elles peuvent être positives
// ou négatives.
// Assurez-vous que la dernière requête d'un client libère toute les ressources
// qu'il a jusqu'alors accumulées.
void
send_request (int client_id, int request_id, int *req_rsc, int socket_fd)
{
    char req[256] = {0};
    char temp[100] = {0};
    char rep[256] = {0};

    strcat (req, "REQ");
    for (int i = 0; i < num_resources; i++) {
        memset(temp, 0, 100);
        sprintf(temp, " %d", req_rsc[i]);
        strcat(req, temp);
    }
    strcat (req, "\n");

    printf ("Client %d (sending) request %d : %s", client_id, request_id, req);
    send (socket_fd, req, strlen(req) + 1, 0);

    pthread_mutex_lock(&lock_request_sent);
    request_sent++;
    pthread_mutex_unlock(&lock_request_sent);

    /* Lire la réponse dans rep */
    recv (socket_fd, rep, sizeof(rep), 0);
    printf ("Client %d (receive) : %s\n", client_id, rep);

    if (strcmp(rep, "ACK\n") == 0) {
        pthread_mutex_lock(&lock_count_accepted_ct);
        count_accepted++;
        pthread_mutex_unlock(&lock_count_accepted_ct);
    }
    else {
        pthread_mutex_lock(&lock_count_invalid_ct);
        count_invalid++;
        pthread_mutex_unlock(&lock_count_invalid_ct);
    }
}


void send_ini (int socket_fd, int client_id, int *max_rsc) {
    char req[256] = {0};
    char temp[100];
    char rep[256] = {0};

    strcat (req, "INI");
    for (int i = 0; i < num_resources; i++) {
        memset(temp, 0, 100);
        sprintf(temp, " %d", max_rsc[i]);
        strcat(req, temp);
    }
    strcat (req, "\n");

    printf("Client %d (sending) : %s\n", client_id, req);
    send(socket_fd, req, strlen(req) + 1, 0);

    /* Lire la réponse dans rep */
    recv (socket_fd, rep, sizeof(rep), 0);
    printf("Client %d (receive) : %s\n", client_id, rep);

}


void close_connection (int socket_fd, int client_id) {
    char req[5] = {0};
    char rep[256] = {0};

    strcat (req, "CLO\n");

    /* Envoyer la requête */
    printf ("Client %d (sending) : CLO\n", client_id);
    send (socket_fd, req, strlen(req) + 1, 0);

    /* Lire la réponse dans rep */
    recv (socket_fd, rep, sizeof(rep), 0);
    printf("Client %d (receive) : %s\n", client_id, rep);

    if (strcmp(rep, "ACK\n") == 0) {
        pthread_mutex_lock(&lock_count_dispatched_ct);
        count_dispatched++;
        pthread_mutex_unlock(&lock_count_dispatched_ct);
    }
}


void *
ct_code (void *param)
{
    /* Variables globales du petit client */
    int *max_rsc = malloc (num_resources * sizeof (int));
    int *allocated = calloc (num_resources, sizeof(int));
    int *req_rsc = NULL;
    int socket_fd = -1;
    client_thread *ct = (client_thread *) param;

    // Generer aléatoirement la commande INI rsc0 rsc1 ... pour initialiser les petits clients.
    for (int i = 0; i < num_resources; i++) {
        max_rsc[i] = rand() % (provisioned_resources[i] + 1);
    }

    // Ouvrir un socket pour le petit client
    socket_fd = ct_open_socket();

    // Se connecter à un thread server
    connexion(socket_fd);

    // Envoyer la cmd INI
    send_ini(socket_fd, ct->id, max_rsc);

    // Boucle d'envoie des requetes REQ
    for (unsigned int request_id = 0; request_id < num_request_per_client;
         request_id++)
    {
        // Générer un REQ aléatoire
        req_rsc = generate_req(allocated, max_rsc, request_id);

        // Envoyer la requête au server
        send_request (ct->id, request_id, req_rsc, socket_fd);

        // Mettre à jour allocated si réception de ACK.
        for (int i = 0; i < num_resources; i++)
            allocated[i] += req_rsc[i];

        free(req_rsc);
        req_rsc = NULL;


        /* Attendre un petit peu (0s-0.1s) pour simuler le calcul.  */
        usleep (random () % (100 * 1000));
        /* struct timespec delay;
         * delay.tv_nsec = random () % (100 * 1000000);
         * delay.tv_sec = 0;
         * nanosleep (&delay, NULL); */
    }

    // Fermer la connexion avec le server en envoyant la cmd CLO.
    close_connection (socket_fd, ct->id);

    // Fermer le socket et libérer la mémoire.
    close(socket_fd);
    free(allocated);
    free(max_rsc);
    pthread_exit (NULL);
}


//
// Vous devez changer le contenu de cette fonction afin de régler le
// problème de synchronisation de la terminaison.
// Le client doit attendre que le serveur termine le traitement de chacune
// de ses requêtes avant de terminer l'exécution.
// Le serveur doit signaler aux clients qu'il a finit toutes les requêtes et donc qu'il peut se terminer
void
ct_wait_server (int num_clients, client_thread *client_threads)
{
    char rep[100] = {0};
    // TP2 TODO
    printf("\n\n************ Client waiting SIG from server ***************\n\n");
    ssize_t error = recv (client_socket_fd, rep, sizeof(rep), 0);
    if (error == 0) {
        close(client_socket_fd);
        printf("Le serveur ne répond plus");
        return;
    }
    printf ("Client (receive) : %s", rep);

    char req[5] = "END\n";
    send (client_socket_fd, req, 5, 0);
    printf ("Client (sending) : END\n");
    close(client_socket_fd);
    // TP2 TODO:END
}


void
ct_init (client_thread * ct)
{
    ct->id = count++;
}

void
ct_create_and_start (client_thread * ct)
{
    pthread_attr_init (&(ct->pt_attr));
    pthread_create (&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
    pthread_detach (ct->pt_tid);
}

//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void
st_print_results (FILE * fd, int verbose)
{
    if (fd == NULL)
        fd = stdout;
    if (verbose)
    {
        fprintf (fd, "\n---- Résultat du client ----\n");
        fprintf (fd, "Requêtes acceptées (ACK reçu pour REQ): %d\n", count_accepted);
        fprintf (fd, "Requêtes en attente: %d\n", count_on_wait);
        fprintf (fd, "Requêtes invalides (ERR reçu pour REQ): %d\n", count_invalid);
        fprintf (fd, "Clients terminés correctement (CLO reçu pour ACK): %d\n", count_dispatched);
        fprintf (fd, "Requêtes envoyées (REQ): %d\n", request_sent);
    }
    else
    {
        fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
                 count_invalid, count_dispatched, request_sent);
    }
}
