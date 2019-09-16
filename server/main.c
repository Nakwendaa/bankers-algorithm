
#include <stdlib.h>
#include <pthread.h>
#include "server_thread.h"

bool accepting_connections = true;
int num_server_threads = 0;
server_thread *threads = NULL;


int
main (int argc, char *argv[argc + 1])
{
    if (argc < 3)
    {
        fprintf (stderr, "Usage: %s [port-nb] [nb-threads]\n", argv[0]);
        exit(1);
    }

    int port_number = atoi (argv[1]);
    num_server_threads = atoi (argv[2]);
    threads = malloc (num_server_threads * sizeof (server_thread));

    // Ouvre un socket
    st_open_socket (port_number);

    // Initialise le serveur.
    int err = st_init ();
    if (err == 1) {
        printf("tp2_server: Out of memory for init\n");
        free (threads);
        exit(1);
    }
    else if (err == 2) {
        printf("Time out on main thread server\n");
        goto journal;
    }

    // Part les fils d'exécution.
    for (unsigned int i = 0; i < num_server_threads; i++)
    {
        threads[i].id = i;
        pthread_attr_init (&(threads[i].pt_attr));
        pthread_create (&(threads[i].pt_tid), &(threads[i].pt_attr), &st_code, &(threads[i]));
    }

    for (unsigned int i = 0; i < num_server_threads; i++)
        pthread_join (threads[i].pt_tid, NULL);

    // Signale aux clients de se terminer.
    st_signal ();
    for (int i = 0; i < num_server_threads; i++) {
        free(threads[i].allocated);
        free(threads[i].max_rsc);
    }

    journal:

    free (threads);
    // Affiche le journal.
    st_print_results (stdout, true);
    FILE *fp = fopen("server.log", "w");
    if (fp == NULL)
    {
        fprintf(stderr, "Could not print log");
        return EXIT_FAILURE;
    }
    st_print_results (fp, false);
    fclose(fp);

    return EXIT_SUCCESS;
}
