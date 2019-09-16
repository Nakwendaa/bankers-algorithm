#define _XOPEN_SOURCE 700   /* So as to allow use of `fdopen` and `getline`. */

#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include <errno.h>
#include "server_thread.h"


/* >>>>>>>>>>>>>>>>>>>>>>>>>>> VARIABLES GLOBALES <<<<<<<<<<<<<<<<<<<<<<<<<<< */


/* Configuration constants.  */
enum {
    max_wait_time = 10,
    server_backlog_size = 5
};

struct timeval timeout;

/* Socket_fd du serveur */
unsigned int server_socket_fd;

/* -------------------- Variables du client principale --------------------- */
/* Socket_fd du client principal afin de permettre l'exècution de commandes
 * globales. */
int main_client_socket_fd = -1;
/* Garder en mémoire la dernière requête exècutée par le client principal
 * afin de prévenir une erreur dans l'ordre d'exècution des requêtes. */
request *main_previous_request;
/* -------------------------------------------------------------------------- */


/* ------------------------- Variables du journal -------------------------- */
/* Nombre de requêtes acceptées immédiatement (ACK envoyé en réponse à REQ). */
pthread_mutex_t lock_count_accepted; // Lock associé à la variable.
unsigned int count_accepted = 0;

/* Nombre de requêtes acceptées après un délai (ACK après REQ, mais retardé). */
pthread_mutex_t lock_count_wait; // Lock associé à la variable.
unsigned int count_wait = 0;

/* Nombre de requête erronées (ERR envoyé en réponse à REQ). */
pthread_mutex_t lock_count_invalid; // Lock associé à la variable.
unsigned int count_invalid = 0;

/* Nombre de clients qui se sont terminés correctement (ACK envoyé en réponse à
 * CLO). */
pthread_mutex_t lock_count_dispatched; // Lock associé à la variable.
unsigned int count_dispatched = 0;

/* Nombre total de requête (REQ) traités. */
pthread_mutex_t lock_request_processed; // Lock associé à la variable.
unsigned int request_processed = 0;

/* Nombre de clients ayant envoyé le message CLO. */
pthread_mutex_t lock_clients_ended; // Lock associé à la variable.
unsigned int clients_ended = 0;
/* -------------------------------------------------------------------------- */


/* ------------------------- Variables du banquier -------------------------- */
int num_resources;
int *available = NULL;
int *total_rsc = NULL; // m définit en tout

int *running;
int running_cmpt = 0;
bool at_least_one;

pthread_mutex_t run_lock;
pthread_mutex_t cond_lock;
pthread_mutex_t banker_lock;
//pthread_cond_t cond;
/* -------------------------------------------------------------------------- */















/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> FONCTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */


/* Détruire de manière sécuritaire une structure request*/
void destroy_request(request *req) {
    free(req->args);
    req->args = NULL;
    free(req->cmd);
    req->cmd = NULL;
    free(req);
    req = NULL;
}

/* Copie de vec2 dans vec1.
 * La longueur des vecteurs doit être fournie en paramètre. */
int *vec_cpy (int *vec1, int *vec2, int len) {
    int i;
    for (i = 0; i < len; i++)
        vec1[i] = vec2[i];
    return vec1;
}

/* Soustrait deux vecteurs de int et retourne le résultat dans un nouveau
 * vecteur.
 * La longueur des vecteurs à additionner doit être fournie en paramètre.  */
int *sub (int *vec1, int *vec2, int len) {
    int *resultat = malloc(sizeof(int) * len);
    if (!resultat) return NULL;
    int i;
    for (i = 0; i < len; i++)
        resultat[i] = vec1[i] - vec2[i];
    return resultat;
}

/* Additione deux vecteurs de int et retourne le résultat dans un nouveau
 * vecteur.
 * La longueur des vecteurs à additionner doit être fournie en paramètre.  */
int *add (int *vec1, int *vec2, int len) {
    int *resultat = malloc(sizeof(int) * len);
    if (!resultat) return NULL;
    int i;
    for (i = 0; i < len; i++)
        resultat[i] = vec1[i] + vec2[i];
    return resultat;
}

/* Retourne 0 si vec est un vecteur qui contient un nombre négatif.
 * Retourne 1 si vec est un vecteur qui contient seulement des nombres positifs.
 * */
int hasNegative(int *vec, int len) {
    int i;
    for (i = 0; i < len; i++)
        if (vec[i] < 0) return 0; // VRAI: a un négatif
    return 1; // FAUX: pas de négatifs
}

int isNeg(int *vec, int len) {
    int *null_vec = calloc(len, sizeof(int));
    if (hasNegative(sub(null_vec, vec, len), len) != 0) {
        free(null_vec);
        return 1;
    }
    free(null_vec);
    return 0;
}

/* Retourne 0 si vec est un vecteur qui contient seulement des 0.
 * Retourne 1 si vec est un vecteur qui contient au moins un nombre différent
 * de 0. */
int isNull (int *vec, int len) {
    int i;
    for (i = 0; i < len; i++)
        if (vec[i] != 0) return 1; // FAUX : vec a un nombre différent de 0.
    return 0; // VRAI: vecteur contient seulement des 0
}

/* Si une chaîne de caractères est considéré comme une requête valide, on peut
 * appeler cette fonction qui permet d"extraire la commande dans une chaîne
 * de caractère de longueur 4. */
char *extract_cmd (char *string) {
    int i = 0, j = 0;
    char *cmd = calloc(4, sizeof(char));
    if (!cmd) return NULL;

    while (*(string + i)  == ' ')
        i++;
    for (j = 0; j < 3; j++)
        cmd[j] = string[i + j];
    cmd[3] = '\0';

    return cmd;
}

/* Si requête a eu sa syntaxe validée, on peut appeler cette fonction qui permet
 * d' en extraire les arguments. */
int *extract_args (char *string) {
    int *arguments = NULL;
    char *temp;
    int begin = 0, end = 0, i = 0;

    /* Tant qu'il y a des espaces, on incrémente begin pour avancer
     * dans string. */
    while(string[begin] == ' ')
        begin++;

    /* Il n'y a plus d'espaces. On incrémente end pour ignorer la commande
     * rencontré et arriver à l'espace qui sépare la commande des arguments.*/
    end = begin;
    while(string[end] >= 'A' && string[end] <= 'Z')
        end++;

    /* On récupère la commande lue dans temp */
    temp = calloc((size_t ) end - begin + 1, sizeof(char));
    if (!temp) return NULL;
    for (int j = 0; j < end - begin; j++)
        temp[j] = string[begin + j];
    temp[end - begin] = '\0';

    for (i = 0; string[end] != '\n'; i++) {
        /* On place begin sur l'espace situé juste après la commande */
        begin = end;
        /* Tant que l'on rencontre des espace, on avance dans la chaîne string*/
        while(string[begin] == ' ')
            begin++;
        end = begin;

        /* Si on rencontre le signe "-", on peut incrémenter end. On vient de
         * rencontre un nombre négatif dans string.*/
        if (string[end] == '-')
            end++;
        /* On avance dans un string pour arriver à la fin du nombre lu. */
        while(string[end] <= '9' && string[end] >= '0')
            end++;

        /* On alloue de la mémoire pour récupérer sous forme de chaîne de char
         * l'argument rencontré */
        temp = calloc((size_t ) end - begin + 1, sizeof(char));
        if (!temp) {
            free(arguments);
            return NULL;
        }

        /* On récupère le nombre lu dans temp. */
        for (int j = 0; j < end - begin; j++)
            temp[j] = string[begin + j];
        temp[end - begin] = '\0';

        /* Conversion char* -> int pour l'argument lu*/
        int val = atoi(temp);
        free(temp);

        /* On augmente l'allocation mémoire du vecteur de int pour accueillir
         * le nouvel argument lu*/
        arguments = realloc(arguments, sizeof(int) * (i + 1));
        if (!arguments) {
            free(temp);
            return NULL;
        }
        arguments[i] = val;
    }
    return arguments;
}

/* Si un message à passer toutes les vérifications d'erreurs de syntaxe, alors
 * il est possible d'en extraire une structure request qui distinguera
 * commande et arguments. */
request *getRequest(char *msg) {
    request *requete= calloc(1, sizeof(request));
    if (!requete)return NULL;
    requete->cmd = extract_cmd(msg);
    if (!requete->cmd) {
        free(requete);
        return NULL;
    }

    if (strcmp(requete->cmd, "CLO") == 0 || strcmp(requete->cmd, "END") == 0)
        requete->args = NULL;
    else {
        requete->args = extract_args(msg);
        if (!requete->args) {
            free(requete->cmd);
            free(requete);
            return NULL;
        }
    }
    return requete;
}

void destroy_reg (regex_t *reg1, regex_t *reg2, regex_t *reg3, regex_t *reg4) {
    regfree(reg1);
    regfree(reg2);
    regfree(reg3);
    regfree(reg4);
}


/* Le but de cette fonction est de pouvoir trouver une erreur de syntaxe dans
 * une requête. On utilisera les expressions régulières de l'API Posix. */
int find_error(char *msg) {

    /* Expressions régulières */
    regex_t regex_empty_lines;
    regex_t regex_unknown_commands;
    regex_t regex_wrong_args_simple;
    regex_t regex_wrong_args_multi;

    /* Compilation des expressions régulières */
    int err, err1, err2, err3, err4;
    char msgbuf[100];

    /* CLO et END peuvent avoir plusieurs espaces ou rien comme argument. Doit
     * se terminer avec */
    err1 = regcomp(&regex_empty_lines, "^ *\n$", REG_EXTENDED);
    /* Format autorisé afin de pouvoir reconnaître une commande */
    err2 = regcomp(&regex_unknown_commands,
                   "^( *(BEG|PRO|INI|REQ) +| *(CLO|END) *)", REG_EXTENDED);
    /* Format un argument simple. */
    err3 = regcomp(&regex_wrong_args_simple, "^ +-?[0-9]+ *\n$", REG_EXTENDED);

    /* Pattern d'arguments multiples. Le nombre d'arguments doit être
     * égal à num_resources. */
    char reg[38] = "";
    char temp[5] = "";
    strcat(reg, "^( +-?[0-9]+ *){");
    sprintf(temp, "%d", num_resources);
    strcat(temp, "}\n$");
    strcat (reg, temp);
    err4 = regcomp(&regex_wrong_args_multi, reg, REG_EXTENDED);
    if (err1 || err2 || err3 || err4) {
        fprintf(stderr, "Could not compile regex in find_error\n");
        goto out;
    }



    /* Exécution des expressions régulières sur la requête. On vérifie
     * d'abord que la commande est autorisée syntaxiquement */
    err = regexec(&regex_unknown_commands, msg, 0, NULL, 0);
    if (!err) {
        char *cmd; int i = 0;
        /* On peut extraire la commande en toute sécurité puisque la syntaxe est
         * correcte. */
        cmd = extract_cmd(msg);
        if (!cmd) goto out; // Out of memory

        if (strcmp("BEG", cmd) == 0) {
            free(cmd);
            while (*(msg + i++)  == ' ');

            /* Si on voit un BEG, il doit y avoir qu'un seul argument. */
            err = regexec(&regex_wrong_args_simple, msg + i + 2, 0, NULL, 0);
            if (!err)
                goto success;
            else if (err == REG_NOMATCH)
                goto wrong_arg_simple; // Wrong_args pour BEG
            else {
                destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                            &regex_wrong_args_simple, &regex_wrong_args_multi);
                regerror(err, &regex_wrong_args_simple, msgbuf, sizeof(msgbuf));
                fprintf(stderr, "Regex match failed: %s\n", msgbuf);
                return -2; // Regexec fail
            }

        }
        /* Un END ou un CLO ne doivent pas avoir d'arguments. Sinon c'est une
         * erreur syntaxique. */
        else if (strcmp("END", cmd) == 0 || strcmp("CLO", cmd) == 0) {
            free(cmd);
            while (*(msg + i++)  == ' ');

            err = regexec(&regex_empty_lines, msg + i + 2, 0, NULL, 0);
            if (!err)
                goto success;

            else if (err == REG_NOMATCH)
                goto empty_line; // Wrong_args pour CLO, END
            else {
                destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                            &regex_wrong_args_simple, &regex_wrong_args_multi);
                regerror(err, &regex_empty_lines, msgbuf, sizeof(msgbuf));
                fprintf(stderr, "Regex match failed: %s\n", msgbuf);
                return -2; // Reg exec fail;
            }

        }
        else {
            /* Les autres commandes doivent avoir en argument des vecteurs
             * de int de la taille de num_resources. Sinon c'est une erreur.*/
            free(cmd);
            while (*(msg + i++)  == ' ');

            err = regexec(&regex_wrong_args_multi,  msg + i + 2, 0, NULL, 0);
            if (!err)
                goto success;
            else if (err == REG_NOMATCH)
                goto wrong_arg_multi;
            else {
                destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                            &regex_wrong_args_simple, &regex_wrong_args_multi);
                regerror(err, &regex_wrong_args_multi, msgbuf, sizeof(msgbuf));
                fprintf(stderr, "Regex match failed: %s\n", msgbuf);
                return -2; // Regexec fail
            }

        }
    }
    else if (err == REG_NOMATCH)
        goto unknow_command;

    else {
        destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                    &regex_wrong_args_simple, &regex_wrong_args_multi);
        regerror(err, &regex_unknown_commands, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
    }



    out:
    destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                &regex_wrong_args_simple, &regex_wrong_args_multi);
    return -1; // Out of memory

    success:
    destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                &regex_wrong_args_simple, &regex_wrong_args_multi);
    return 0; // SUCCESS !

    unknow_command:
    destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                &regex_wrong_args_simple, &regex_wrong_args_multi);
    return 1; // unknown command

    wrong_arg_simple:
    destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                &regex_wrong_args_simple, &regex_wrong_args_multi);
    return 2; // Mauvais argument pour BEG

    empty_line:
    destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                &regex_wrong_args_simple, &regex_wrong_args_multi);
    return 3; // Mauvais argument pour CLO ou END

    wrong_arg_multi:
    destroy_reg(&regex_empty_lines, &regex_unknown_commands,
                &regex_wrong_args_simple, &regex_wrong_args_multi);
    return 4; // Mauvais arguments pour les autres commandes.

}


/* Procède à la vérification d'erreur à partir d'un message msg.
 * Passer la structure st afin de pouvoir vérifier par exemple que INI
 * ne dépasse pas le total de rsc disponible, ou que REQ n'est pas erronée */
char *process_error(char *msg, server_thread *st) {

    char *err = malloc(sizeof(char));
    if (!err)
        return  NULL;

    request *requete;
    err[0] = '\0';

    int error = find_error(msg);
    if (error == -1) return NULL; // Out of memory.
    switch (error) {
        case 1:
            free(err);
            err = NULL;
            err = calloc(21,sizeof(char));
            if (!err) return  NULL;
            strcat(err, "ERR unknown command\n");
            return err;

        case 2:
            free(err);
            err = NULL;
            err = calloc(47,sizeof(char));
            if (!err) return  NULL;
            strcat(err ,"ERR wrong args : must be \"BEG nb_resources\" \n");
            return err;

        case 3:
            free(err);
            err = NULL;
            err = calloc(42,sizeof(char));
            if (!err) return  NULL;
            strcat(err , "ERR wrong args : must be \"CLO\" or \"END\" \n");
            return err;

        case 4:
            free(err);
            err = NULL;
            err = calloc(116,sizeof(char));
            if (!err) return  NULL;
            strcat(err ,  "ERR wrong args : must be \"INI rs0 rs1 ...\" or "
                    "\"REQ rs0 rs1\" and the number of rsI must be equal to "
                    "num_resources \n");
            return err;
        default:
            /* Arriver ICI, il est clair que la requête envoyée est
             * syntaxiquement correct. Nous pouvons procéder maintenant
             * à l'analyse des arguments de la requête, et au contrôle
             * de l'ordre des requêtes. */
            requete = getRequest(msg);
            if (!requete) { free(err); return NULL; }

            /* Si on appelle la fonction process_error avec st à NULL, alors
             * seul le client principal est connecté */
            if (st == NULL) {
                /* Le flot de requêtes principales doit respecter l'ordre
                 * BEG -> PRO -> END */
                if (   (main_previous_request == NULL
                        && strcmp(requete->cmd, "BEG") == 0)
                       || (strcmp(main_previous_request->cmd, "BEG") == 0
                           && strcmp(requete->cmd, "PRO") == 0)
                       || (strcmp(main_previous_request->cmd, "PRO") == 0
                           && strcmp(requete->cmd, "END") == 0)
                       || (strcmp(main_previous_request->cmd, "BEG") == 0
                              && strcmp(requete->cmd, "END") == 0))
                    break;

                else {
                    free(err);
                    err = NULL;
                    destroy_request(requete);
                    err = calloc(21,sizeof(char));
                    if (!err) return  NULL;
                    strcat(err ,  "ERR bad flow order\n");
                    return err;
                }
            }
            /* Requête locale, donc st non NULL. */
            else {
                /* On veut que le flot de requête ait l'ordre INI -> CLO ou
                 * INI -> REQ -> CLO (On vérifie plus loin si les ressources ont
                 * été libérées)*/
                if (   ((st->previous_request == NULL)
                        && (strcmp(requete->cmd, "INI") == 0))
                       || ((strcmp(st->previous_request->cmd, "INI") == 0)
                           && (strcmp(requete->cmd, "CLO") == 0))
                       || ((strcmp(st->previous_request->cmd, "INI") == 0)
                           && (strcmp(requete->cmd, "REQ") == 0))
                       || ((strcmp(st->previous_request->cmd, "REQ") == 0)
                           && (strcmp(requete->cmd, "REQ") == 0))
                       || ((strcmp(st->previous_request->cmd, "REQ") == 0)
                           && (strcmp(requete->cmd, "CLO") == 0)) ) {
                    break;
                }
                else {
                    free(err);
                    err = NULL;
                    destroy_request(requete);
                    err = calloc(21,sizeof(char));
                    if (!err) return  NULL;
                    strcat(err ,  "ERR bad flow order\n");
                    return err;
                }
            }

    }
    /* Vérification ultime d'erreur. Ici, la syntaxe de la requête est déjà
     * validée et la requête est arrivé dans le bon ordre d'exècution. */

    /* Si la commande est INI, on vérifie  que la requête n'excède pas
     * le nombre total de ressources disponibles pour le système */
    if (strcmp(requete->cmd, "INI") == 0) {
        int *resultat = sub(total_rsc, requete->args, num_resources);
        if (!resultat) {
            free(err);
            destroy_request(requete);
            return NULL;
        }
        if (hasNegative(resultat, num_resources) == 0) {
            free(err);
            free(resultat);
            err = NULL;
            destroy_request(requete);
            err = calloc(60, sizeof(char));
            if (!err) return  NULL;
            strcat(err ,  "ERR you cannot init for more resources than total_rsc\n");
            return err;
        }
        free(resultat);
    }
    /*  si c'est une REQ et que l'on a appelé process_error
     *  à partir d'un thread client (le thread client global ne peut pas
     *  effectuer de REQ), vérifier que sa requête ses besoins maximaux. Sinon
     *  c'est une erreur. */
    else if (strcmp(requete->cmd, "REQ") == 0 && st != NULL) {
        int *need = sub(st->max_rsc, st->allocated, num_resources);
        if (!need) {
            free(err);
            destroy_request(requete);
            return NULL;
        }
        int *resultat = sub(need, requete->args, num_resources);
        if (!resultat) {
            free(err);
            free(need);
            destroy_request(requete);
            return NULL;
        }
        if (hasNegative(resultat, num_resources) == 0) {
            free(err);
            free(resultat);
            free(need);
            destroy_request(requete);
            err = NULL;
            err = calloc(80, sizeof(char));
            if (!err) return  NULL;
            strcat(err ,  "ERR you cannot request or free for more resources that" //54
                    " you declared.\n"); //16
            return err;
        }
        free(need);
        free(resultat);
    }
    else if (strcmp(requete->cmd, "CLO") == 0) { // Il reste des rsc allouées.

        if (st != NULL && isNull(st->allocated, num_resources) == 1) {
            free(err);
            err = NULL;
            destroy_request(requete);
            err = calloc(42, sizeof(char));
            if (!err) return  NULL;
            strcat(err ,  "ERR all memory must to be free on CLO.\n");
            return err;
        }
    }

    /* Comme il n'y a pas eu d'erreurs déclaré (on est arrivé dans cette
     * dernière portion de code sans jamais retourné. Il n'y a donc pas d'erreur)
     * Toutes les possibilités d'ERR ont été écartées. Le requête devra donc
     * forcément être traitée. On garde donc trace de la dernière requête
     * exécutée. */
    if (st == NULL) {
        if (main_previous_request == NULL) {
            main_previous_request = requete;
        }
        else {
            destroy_request(main_previous_request);
            main_previous_request = requete;
        }
    }
    else {
        if (st->previous_request == NULL) {
            st->previous_request = requete;
        }
        else {
            destroy_request(st->previous_request);
            st->previous_request = requete;
        }
    }
    return err;
}


/* Ouverture d'un socket pour le serveur */
void
st_open_socket (int port_number)
{
    server_socket_fd = (unsigned int) socket (AF_INET, SOCK_STREAM, 0);
    fcntl(server_socket_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in serv_addr;
    memset (&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons (port_number);


    int i = 1;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    /* Pour éviter les erreurs de type address already use. */
    if (setsockopt(server_socket_fd, SOL_SOCKET,
                         SO_REUSEADDR, &i, sizeof(i)) < 0 ) {
        perror("setsockopt failed");
    }

    if (bind
                (server_socket_fd, (struct sockaddr *) &serv_addr,
                 sizeof (serv_addr)) < 0)
        perror ("ERROR on binding");

    listen (server_socket_fd, server_backlog_size);
}

/* Gestion des commandes globales BEG et PRO */
int
st_init () {

    int cmpt = 0;
    struct sockaddr_in thread_addr;
    socklen_t socket_len = sizeof(thread_addr);
    int end_time = time (NULL) + max_wait_time;

    /* Attendre pour la connexion du client global.*/
    while (main_client_socket_fd < 0) {
        main_client_socket_fd = accept(server_socket_fd,
                                       (struct sockaddr *) &thread_addr,
                                       &socket_len);
        if (time(NULL) >= end_time) {
            close(main_client_socket_fd);
            return 2; // Time out;
        }
    }

    /* Ne pas supposer que l'on reçoit dans un accept un socket bloquant.
     * Mettre explicitemment les attributs du socket à bloquant. */
    int flags = fcntl(main_client_socket_fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(main_client_socket_fd, F_SETFL, flags);

    /* Pour mettre un time out au socket. */
    if (setsockopt (main_client_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                    sizeof(timeout)) < 0)
        perror("setsockopt failed");

    if (setsockopt (main_client_socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                    sizeof(timeout)) < 0)
        perror("setsockopt failed");

    /* Initialisation pour le contrôle du flot d'exècution global */
    main_previous_request = NULL;

    /* On effectue l,initialisation des structures partagées pour l'algorithme
     * du banquier à cet endroit là. */
    while(cmpt < 2) {
        char req[100] = "";
        // On a pas reçu de réponse au bout de 30 secondes.
        ssize_t err = recv(main_client_socket_fd, req, sizeof(req), 0);
        if (err == -1) {
            close(main_client_socket_fd); // Timeout
            return 2;
        }
        printf("Server (received) : %s", req);

        // Procède à une vérification du message.
        // Si *rep == '\0', alors, il n'y pas d'error.
        char *rep = process_error(req, NULL);
        if (!rep) return 1; // Out of memory
        if (*rep == '\0') {
            request *requete = getRequest(req);
            if (!requete) {
                free(rep);
                return 1;
            }
            if (strcmp(requete->cmd, "BEG") == 0) {
                num_resources = requete->args[0];
                total_rsc = malloc(num_resources * sizeof(int));
                if (!total_rsc) {
                    free(rep);
                    destroy_request(requete);
                    return 1;
                }
                available = calloc((size_t ) num_resources, sizeof(int));
                if (!available) {
                    free(rep);
                    free(total_rsc);
                    destroy_request(requete);
                    return 1;
                }
            } else {
                for (int i = 0; i < num_resources; i++) {
                    total_rsc[i] = requete->args[i];
                    available[i] = requete->args[i];
                }
            }
            destroy_request(requete);
            free(rep);
            rep = calloc(5, sizeof(char));
            if (!rep) {
                free(total_rsc);
                free(available);
                return 1;
            }
            strcat(rep, "ACK\n");
        }


        err = send(main_client_socket_fd, rep, strlen(rep) + 1, 0);
        // Le client ne peut pas reçevoir notre réponse de retour.
        if (err == -1) {
            printf("Client doesn't answer\n");
            free(rep);
            close(main_client_socket_fd);
            return 2;
        }
        printf("Server (sending) : %s\n", rep);
        if (strcmp(rep, "ACK\n") == 0) {
            cmpt++;
        }
        free(rep);
    }

    running = calloc( (size_t) num_server_threads, sizeof(int));
    pthread_mutex_init(&banker_lock, NULL);
    pthread_mutex_init(&cond_lock, NULL);
    pthread_mutex_init(&run_lock, NULL);

    pthread_mutex_init(&lock_count_accepted, NULL);
    pthread_mutex_init(&lock_count_wait, NULL);
    pthread_mutex_init(&lock_count_invalid, NULL);
    pthread_mutex_init(&lock_count_dispatched, NULL);
    pthread_mutex_init(&lock_request_processed, NULL);
    pthread_mutex_init(&lock_clients_ended, NULL);
    return 0;
}

/* Code à exécuter pour chaque thread serveur qui reçoit la connexion d'un
 * serveur. */
void *
st_code (void *param)
{
    server_thread *st = (server_thread *) param;


    struct sockaddr_in thread_addr;
    socklen_t socket_len = sizeof (thread_addr);
    int thread_socket_fd = -1;
    int end_time = (int) (time (NULL) + max_wait_time);

    // Boucle jusqu'à ce que `accept` reçoive la première connexion.
    while (thread_socket_fd < 0)
    {
        // Comme server_socket_fd est non bloquant, l'appel à accept est
        // non-bloquant
        thread_socket_fd =
                accept (server_socket_fd, (struct sockaddr *) &thread_addr,
                        &socket_len);

        if (time (NULL) >= end_time)
        {
            // Dépassement du temps d'attente de requête de connexion. Dans
            // ce cas quitter la boucle d'attente
            pthread_exit(NULL);
        }
    }

    /* On fait passer en mode bloquant le socket retourné par accept */
    int flags = fcntl(thread_socket_fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(thread_socket_fd,F_SETFL, flags);

    // Boucle de traitement des requêtes.
    // CLO quitte la boucle de traitement des requêtes.


    st_process_requests (st, thread_socket_fd);

    pthread_mutex_lock(&lock_clients_ended);
    clients_ended++;
    pthread_mutex_unlock(&lock_clients_ended);
    close (thread_socket_fd);
    destroy_request(threads[st->id].previous_request);
    pthread_exit (NULL);
}

bool banker_algo(request *requete, server_thread *st) {
    bool safe = false;
    // Traiter chaque requete de maniere "atomique".
    pthread_mutex_lock(&banker_lock);
    pthread_mutex_lock(&run_lock);
    int *running_threads = calloc(num_server_threads, sizeof(int));
    vec_cpy(running_threads, running, num_server_threads);
    int num_threads_restant = running_cmpt;


    printf("***************  Thread %d obtient la banker_lock  *******************\n", st->id);

    if (isNeg(requete->args, num_resources)) {
        safe = true;
        available = sub(available, requete->args, num_resources);
        threads[st->id].allocated = add(threads[st->id].allocated, requete->args, num_resources);
    }
        // Si la requete est grantable tester si l'état hypothétique est safe.
    else if (hasNegative(sub(available, requete->args, num_resources), num_resources) != 0) {
        int *work;
        work = calloc(num_resources, sizeof(int));
        int *flag = calloc((size_t) num_server_threads, sizeof(int));

        vec_cpy(work, available, num_resources);
        threads[st->id].allocated = add(threads[st->id].allocated, requete->args, num_resources);
        work = sub(work, requete->args, num_resources);
        safe = true;
        while (safe && num_threads_restant != 0) {
            safe = false;
            for (int i = 0; i < num_server_threads; i++) {
                if (flag[i] == 1 || running_threads[i] == 0)
                    continue;
                if (hasNegative(sub(work, sub(threads[i].max_rsc, threads[i].allocated, num_resources),
                                    num_resources), num_resources) != 0) {
                    num_threads_restant--;
                    flag[i] = 1;
                    work = add(work, threads[i].allocated, num_resources);
                    safe = true;
                }
            }
        }

        if(!safe)
            threads[st->id].allocated = sub(threads[st->id].allocated, requete->args, num_resources);
        else
            available = sub(available, requete->args, num_resources);

        free(flag);
        free(work);
        work = NULL;
    }

    printf("***************  Thread %d relache la banker_lock  *******************\n", st->id);
    pthread_mutex_unlock(&run_lock);
    pthread_mutex_unlock(&banker_lock);
    return safe;
}
/* Exécuter toutes les requêtes tant qu'il en reste à lire ou qu'un CLO
 * n'est pas lu. */
void
st_process_requests (server_thread * st, int socket_fd)
{
    st->max_rsc = calloc((size_t ) num_resources, sizeof(int));
    st->allocated = calloc ((size_t ) num_resources, sizeof(int));
    st->previous_request = NULL;

    /* INI des petits clients */
    char ini[100] = "";
    recv (socket_fd, ini, sizeof(ini), 0);
    printf("Thread %d (receive) : %s", st->id, ini);

    // Procède à une vérification du message.
    // Si rep null, alors, il n'y pas d'error.
    char *rep = process_error(ini, st);
    if (*rep == '\0') {
        request *requete = getRequest(ini);
        if (strcmp(requete->cmd, "INI") == 0) {
            for (int i = 0; i < num_resources; i++) {
                st->max_rsc[i] = requete->args[i];
            }
            pthread_mutex_lock(&run_lock);
            running[st->id] = 1;
            running_cmpt++;
            pthread_mutex_unlock(&run_lock);
            destroy_request(requete);
        }
        free(rep);
        rep = calloc(5, sizeof(char));
        strcat(rep, "ACK\n");
    }
    printf ("Thread %d (sending) : %s\n", st->id, rep);
    send(socket_fd, rep, strlen(rep) + 1, 0);
    free(rep);
    rep = NULL;
    while(1) {
        int timer = time(NULL);

        char req[100] = "";
        recv (socket_fd, req, sizeof(req), 0);
        printf("Thread %d (receive) : %s", st->id, req);

        // Procède à une vérification du message.
        // Si rep null, alors, il n'y pas d'error.
        rep = process_error(req, st);
        if (*rep == '\0') {
            request *requete = getRequest(req);
            if (strcmp(requete->cmd, "CLO") == 0) {
                free(rep);
                rep = calloc(5, sizeof(char));
                strcat(rep, "ACK\n");
                printf ("Thread %d (sending) : %s\n", st->id, rep);
                printf("Thread %d close la connexion.\n", st->id);

                send(socket_fd, rep, strlen(rep) + 1, 0);
                destroy_request(requete);
                pthread_mutex_lock(&lock_count_dispatched);
                count_dispatched++;
                pthread_mutex_unlock(&lock_count_dispatched);
                pthread_mutex_lock(&banker_lock);
                pthread_mutex_lock(&run_lock);
                running_cmpt--;
                running[st->id] = 0;
                pthread_mutex_unlock(&run_lock);
                pthread_mutex_unlock(&banker_lock);
                break;
            }
            if (strcmp(requete->cmd, "REQ") == 0) {
                bool safe = false;
                while(! safe) {
                    safe = banker_algo(requete, st);
                    if (!safe) {
                        while (!at_least_one);
                        pthread_mutex_lock(&cond_lock);
                        at_least_one = false;
                        pthread_mutex_unlock(&cond_lock);
                    }
                    else
                        printf(" *************************** La requete de Thread %d EST SAFE\n\n", st->id);
                }
                pthread_mutex_lock(&cond_lock);
                at_least_one = true;
                pthread_mutex_unlock(&cond_lock);
                timer = time(NULL) - timer;
            }

            free(rep);
            rep = calloc(5, sizeof(char));
            strcat(rep, "ACK\n");

            send(socket_fd, rep, strlen(rep) + 1, 0);

            if (strcmp(requete->cmd, "REQ") == 0) {
                if(timer < max_wait_time) {
                    pthread_mutex_lock(&lock_count_accepted);
                    count_accepted++;
                    pthread_mutex_unlock(&lock_count_accepted);
                }
                else {
                    pthread_mutex_lock(&lock_count_wait);
                    count_wait++;
                    pthread_mutex_unlock(&lock_count_wait);
                }

                pthread_mutex_lock(&lock_request_processed);
                request_processed++;
                pthread_mutex_unlock(&lock_request_processed);
            }
            destroy_request(requete);
        }
        printf ("Thread %d (sending) : %s\n", st->id, rep);


        if (strcmp(rep, "ERR you cannot request or free for more resources that you declared.\n") == 0) {
            pthread_mutex_lock(&lock_count_invalid);
            count_invalid++;
            pthread_mutex_unlock(&lock_count_invalid);

            pthread_mutex_lock(&lock_request_processed);
            request_processed++;
            pthread_mutex_unlock(&lock_request_processed);
        }

        if (strcmp(req, "CLO\n") == 0) {
            free(rep);
            printf("Thread %d close la connexion.\n", st->id);
            break;
        }
        free(rep);
    }
    return;
}

/* Signaler au thread client global que tous les threads clients ont terminés*/
void
st_signal ()
{
    // TODO: Remplacer le contenu de cette fonction

    char req[100];
    printf ("Server (sending) : SIG\n");
    ssize_t err = send(main_client_socket_fd, "SIG\n", 5, 0);
    if (err == -1) {
        close(main_client_socket_fd);
        return;
    }

    err = recv (main_client_socket_fd, req, sizeof(req), 0);
    if (err == -1) {
        printf("Client doesn't answer\n");
        close(main_client_socket_fd);
        return;
    }
    printf ("Server (receive) : %s", req);
    close(main_client_socket_fd);
    return;
    // TODO end
}


/* Affiche les données recueillies lors de l'exécution du */
void
st_print_results (FILE * fd, bool verbose)
{
    if (fd == NULL) fd = stdout;
    if (verbose)
    {
        fprintf (fd, "\n---- Résultat du serveur ----\n");
        fprintf (fd, "Requêtes acceptées (ACK envoyé en réponse à REQ): %d\n", count_accepted);
        fprintf (fd, "Requêtes retardées (ACK après REQ, mais retardé): %d\n", count_wait);
        fprintf (fd, "Requêtes invalides (ERR en réponse à REQ): %d\n", count_invalid);
        fprintf (fd, "Clients terminés correctement (ACK en réponse à CLO): %d\n", count_dispatched);
        fprintf (fd, "Requêtes traitées: %d\n", request_processed);
        fprintf (fd, "Nombre de clients ayant envoyé le message CLO: %d\n", clients_ended);

    }
    else
    {
        fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_wait,
                 count_invalid, count_dispatched, request_processed);
    }
}
