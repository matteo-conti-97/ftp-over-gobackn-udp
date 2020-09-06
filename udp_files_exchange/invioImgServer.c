#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096

// e.g. ->  ./a.out 8888

/*
argv[1]: #port server
*/

int main (int argc, char *argv[]) {

    int fdRecv;

	// controllo se siano stati inseriti tutti gli argv
	if (argc != 2) {
		printf("ERRORE: numero di argomenti inseriti errato.\nUso: './prog #SERV_PORT'\n");
		exit(1);
	}

	//creo socket server
	int serverSock;
	if ((serverSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("ERRORE: creazione socket fallita.\n");
		exit(1);
	}

/************************** NON FUNZ
    int a = 65535;
    if (setsockopt(serverSock, SOL_SOCKET, SO_RCVBUF, &a, sizeof(int)) == -1) {
        fprintf(stderr, "Error setting socket opts: %s\n", strerror(errno));
    }
*************************/

	//struttura sockaddr server
	struct sockaddr_in server;              // (local) server socket info
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;            // address family
    server.sin_addr.s_addr = INADDR_ANY;    // ricevo da tutti
    server.sin_port = htons(atoi(argv[1])); // server #port

    //bind informazioni server al socket
    if (bind(serverSock, (struct sockaddr *) &server, sizeof(server)) < 0) {
    	printf("ERRORE: bind fallita.\n");
		exit(1);
    }

    //struttura per dati dal client
    struct sockaddr_in client;

    //dim socket
    socklen_t sockSize = sizeof(struct sockaddr_in);

    //buffer per messaggi client
    char messaggioClient[BUFFER_SIZE];
    memset(messaggioClient, 0, BUFFER_SIZE);

    //+++START++
    char ch, source_file[20], target_file[20];
    FILE *target;
    size_t n, m;

    n=1;

    printf("\nEnter name of dest file...\n>>>\t");
    scanf("%s", target_file);
    //target_file = "provaRicev.txt";

    //int i=0;

    target = fopen(target_file, "wb");
    //target = fopen("provaRicev.txt", "wb");
    //fdRecv = open("fileRicevuto.jpg", O_WRONLY|O_CREAT|O_TRUNC,0660);
    while ((m=recvfrom(serverSock, messaggioClient, BUFFER_SIZE, 0, (struct sockaddr *) &client, &sockSize)) > 0) {

        unsigned char buff[BUFFER_SIZE];

        if (strcmp(messaggioClient, "TERMINILLO") == 0) {
            //printf("\nTERMINILLO\n");
            goto ex;
        }

        /*  NB: USANDO STRLEN QUANDO SI E' A FINE FILE, PER CUI NON VENGONO LETTI TUTTI I BUFFER_SIZE BYTES, IN QUESTO MODO FUNZIONA
        */  
        if (strlen(messaggioClient) < BUFFER_SIZE) {
            printf("Messaggio ricevuto: %s \n***lungh: %ld/%ld\n---------------------\n", messaggioClient, strlen(messaggioClient), sizeof(messaggioClient));
            m = fwrite(messaggioClient, 1, m, target);
            printf("\nScritto in target1. **m=%ld**\n\n", m);
        }
        /*  NB: USANDO SIZEOF INVECE DI STRLEN LEGGE SEMPRE 128 BYTE PER CUI NON SCRIVE CORRETTAMENTE IL FILE
        if (sizeof(messaggioClient) < BUFFER_SIZE) {
            printf("Messaggio ricevuto: %s \n***lungh: %ld\n---------------------\n", messaggioClient, sizeof(messaggioClient));
            write(fdRecv, messaggioClient, sizeof(messaggioClient));
        }
        */

        else {
            //stampo il messaggio
            printf("Messaggio ricevuto: %s \n***lungh: %ld\n---------------------\n", messaggioClient, sizeof(messaggioClient));
            //write(fdRecv, messaggioClient, sizeof(messaggioClient));
            m = fwrite(messaggioClient, 1, sizeof(messaggioClient), target);
            printf("\nScritto in target2. **m=%ld**\n\n", m);
        }

        memset(messaggioClient, 0, BUFFER_SIZE);

    }

ex: 
    fclose(target);
    printf("\nTERMINILLO\n");
    //client "non presente"
    printf("ERRORE: recvfrom fallita.\n");
	exit(1);

}


/*
    while (recvfrom(serverSock, messaggioClient, BUFFER_SIZE, 0, (struct sockaddr *) &client, &sockSize) > 0) {

        char messaggioServer[BUFFER_SIZE]; 
        
        //stampo il messaggio
        printf("Messaggio ricevuto: %s\n", messaggioClient);

        if(i=0) {
            fdRecv = open("fileRicevuto.txt", O_RDWR|O_TRUNC|O_CREAT, 0666);
            //close(fdRecv);

            //invio risposta
            memset(messaggioServer, 0, BUFFER_SIZE);
            printf("Rispondi al client:\n");
            scanf("%s", messaggioServer);
            if (sendto(serverSock, messaggioServer, BUFFER_SIZE, 0, (struct  sockaddr *) &client, sizeof(client)) < 0 ) {
                printf("ERRORE: sendto fallita.");
            exit(1);
            }
            i++;
        }

        if(i!=0) {
            memset(messaggioServer, 0, BUFFER_SIZE);
            printf("Ricevuto:\n");
            scanf("%s", messaggioServer);
            if (sendto(serverSock, messaggioServer, BUFFER_SIZE, 0, (struct  sockaddr *) &client, sizeof(client)) < 0 ) {
                printf("ERRORE: sendto fallita.");
            exit(1);
            }
            write(fdRecv, messaggioClient, BUFFER_SIZE);
        }


    }

    */