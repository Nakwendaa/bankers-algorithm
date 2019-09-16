
#include <sys/fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "client_thread.h"

int
main (int argc, char *argv[argc + 1])
{
    if (argc < 5)
    {
        fprintf (stderr, "Usage: %s <port-nb> <nb-clients> <nb-requests> <resources>...\n",
                 argv[0]);
        exit (1);
    }

    /* Port sur lequel on va créer notre socket client..  */
    port_number = atoi (argv[1]);

    /* Nombre de threads clients */
    int num_clients = atoi (argv[2]);

    /* Nombre de requêtes que chaque client doit envoyer.  */
    num_request_per_client = atoi (argv[3]);

    /* Nombre de resources différentes. */
    num_resources = argc - 4;

    /* Provisionnement des ressources */
    provisioned_resources = malloc (num_resources * sizeof (int));


    for (unsigned int i = 0; i < num_resources; i++)
        provisioned_resources[i] = atoi (argv[i + 4]);

    client_thread *client_threads
            = malloc (num_clients * sizeof (client_thread));

    init_addr_serv();
    int err = begin();
    if (err == 1) {
        printf("Connexion refusée: impossible d'initialiser le nombre "
                       "de ressources.\n");
        goto journal;
    }
    else if (err == 2) {
        printf("Le serveur ne répond plus.\n");
        goto journal;
    }
    else if (err == 3) {
        printf("Impossible d'ouvrir le socket du client principal.\n");
        goto journal;
    }

    err = pro();
    if (err == 2) {
        printf("Le serveur ne répond plus.\n");
        goto journal;
    }

    for (unsigned int i = 0; i < num_clients; i++)
        ct_init (&(client_threads[i]));

    for (unsigned int i = 0; i < num_clients; i++)
    {
        ct_create_and_start (&(client_threads[i]));
    }

    ct_wait_server (num_clients, client_threads);

    journal:
    free (client_threads);
    free (provisioned_resources);
    // Affiche le journal.
    st_print_results (stdout, 1);
    FILE *fp = fopen("client.log", "w");
    if (fp == NULL)
    {
        fprintf(stderr, "Could not print log");
        return EXIT_FAILURE;
    }
    st_print_results (fp, false);
    fclose(fp);


    return EXIT_SUCCESS;
}
