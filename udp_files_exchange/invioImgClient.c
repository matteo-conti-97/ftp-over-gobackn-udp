#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// mettendo lo sleep(1) prima della chiamata send il trasferimento avviene correttamente, 
// questo significa che alcuni pacchetti/byte inviati vengono sovrascritti perché il buffer in ingresso va in overflow. Quindi va sincronizzata la trasmissione, magari inviando dei messaggi di "ack" dopo ogni arrivo

#define BUFFER_SIZE 4096

// e.g. ->  ./a.out 127.0.0.1 8888

/*
argv[1]: ip_server
argv[2]: #port server
*/

int main (int argc, char *argv[]) {

	int fdSorg;
	int condizione = 1;
	char buffer[BUFFER_SIZE];
	int sizeR;

	// controllo se siano stati inseriti tutti gli argv
	if (argc != 3) {
		printf("ERRORE: numero di argomenti inseriti errato.\nUso: './prog IP_SERV #SERV_PORT'\n");
		exit(1);
	}

	//creo socket client
	int clientSock;
	if ((clientSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("ERRORE: creazione socket fallita.\n");
		exit(1);
	}

	/************************** NON FUNZ
    int a = 65535;
    if (setsockopt(clientSock, SOL_SOCKET, SO_RCVBUF, &a, sizeof(int)) == -1) {
        fprintf(stderr, "Error setting socket opts: %s\n", strerror(errno));
    }
	*************************/

	//struttura sockaddr per il server remoto
	struct sockaddr_in server;              // (remote) server socket info
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;            // address family
    server.sin_addr.s_addr = inet_addr(argv[1]);    // ricevo dal server remoto indicato in argv
    server.sin_port = htons(atoi(argv[2])); // server #port

    //while (1) {
        
        char messaggio[BUFFER_SIZE];
        memset(messaggio, 0, BUFFER_SIZE);

    	printf("Invio:\n");
    	
    	// +START+
    	char ch, source_file[20], target_file[20];
		   
	    FILE *source, *target;
	    size_t n, m;

	    printf("Enter name of file to copy\n");
	    //gets(source_file);
	    scanf("%s", source_file);

	    source = fopen(source_file, "rb");

	    if (source == NULL)
	    {
	       printf("Press any key to exit...\n");
	       exit(EXIT_FAILURE);
	    }
	    // +END+

	    // -START-
	    unsigned char buff[BUFFER_SIZE];
   		while ((n = fread(buff, 1, sizeof(buff), source)) > 0) {
	        printf("\n...ho letto %ld bytes - ", n);
			printf("sending...\n");

			//memset(messaggio, 0, BUFFER_SIZE);
		    //sprintf(messaggio, "%s", buff);
	    	//invio messaggio al server

			//*************************************SLEEP***************************************
			sleep(1);

	    	if(n < BUFFER_SIZE) {	// se non ho letto BUFFER_SIZE bytes
		    	if (sendto(clientSock, buff, n, 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
	 				printf("ERRORE: sendto fallita.\n");
			    	exit(1);
	    		}
	    		goto closing;
		    }
		    else {
	    		if (sendto(clientSock, buff, n, 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
	 				printf("ERRORE: sendto fallita.\n");
			    	exit(1);
	    		}
	    	}
	    	memset(buff, 0, BUFFER_SIZE);
	    }

	    /*

        while ((sizeR = read(fdSorg, buffer, BUFFER_SIZE)) > 0) {
		    printf("\n...ho letto %d bytes - ", sizeR);
		    printf("sending...\n");

		    memset(messaggio, 0, BUFFER_SIZE);
		    sprintf(messaggio, "%s", buffer);
	    	//invio messaggio al server
	    	if(sizeR < BUFFER_SIZE) {	// se non ho letto BUFFER_SIZE bytes
		    	if (sendto(clientSock, buffer, sizeof(buffer), 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
	 				printf("ERRORE: sendto fallita.\n");
			    	exit(1);
	    		}
	    		goto closing;
		    }
		    else {
	    		if (sendto(clientSock, buffer, sizeof(buffer), 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
	 				printf("ERRORE: sendto fallita.\n");
			    	exit(1);
	    		}
	    	}
	    	memset(buffer, 0, BUFFER_SIZE);
	    }
	    */

closing:
	fclose(source);
	printf("\n...finish! Cura lavastoviglie\n");
	char *exitMsg = "TERMINILLO";
	memset(messaggio, 0, BUFFER_SIZE);
	sprintf(messaggio, "%s", exitMsg);
	if (sendto(clientSock, messaggio, BUFFER_SIZE, 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
	 			printf("ERRORE: sendto fallita.\n");
			    exit(1);
	}

	//}

    exit(0);

}


/*
    while (1) {
        
        char messaggio[BUFFER_SIZE];
        memset(messaggio, 0, BUFFER_SIZE);

    	printf("File da inviare:\n");
        scanf("%s", messaggio);	//scrivo messaggio

    	//invio messaggio al server
    	if (sendto(clientSock, messaggio, BUFFER_SIZE, 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
 			printf("ERRORE: sendto fallita.\n");
		    exit(1);
    	}

    	fdSorg = open("prova.txt", O_RDONLY);

    	//ricevo risposta
        memset(messaggio, 0, BUFFER_SIZE);
        if (recvfrom(clientSock, messaggio, BUFFER_SIZE, 0, NULL, NULL) < 0) {
            printf("ERRORE: recvfrom fallita.\n");
            exit(1);
        }
        printf("***Risposta server*** %s\n", messaggio);


    	while (condizione){
		    sizeR = read(fdSorg, buffer, BUFFER_SIZE);
		    if(sizeR==0) goto closing;
		    printf("...ho letto %d bytes\n", sizeR);
		    printf("\nsending...\n");
		    sprintf(messaggio, "%s", buffer);

		    memset(messaggio, 0, BUFFER_SIZE);
	    	//invio messaggio al server
	    	if (sendto(clientSock, messaggio, BUFFER_SIZE, 0, (struct  sockaddr *) &server, sizeof(server)) < 0 ) {
	 			printf("ERRORE: sendto fallita.\n");
			    exit(1);
	    	}
	    }

closing:
	close(fdSorg);
	printf("\n...finish! Cura lavastoviglie\n");
	break;
    }

*/

