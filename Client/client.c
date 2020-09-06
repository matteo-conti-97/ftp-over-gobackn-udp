#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#define SERV_PORT 5192
#define MAXLINE 1024

int main(int argc, char *argv[ ]){
  int sockfd, fd, n, len;
  char recvline[MAXLINE];
  struct sockaddr_in servaddr;
  if (argc != 2) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP server>\n");
    exit(1);
  }
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(-1);
  }
  memset((void *)&servaddr, 0, sizeof(servaddr));
  /* azzera servaddr */
  servaddr.sin_family = AF_INET;
  /* assegna il tipo di indirizzo */
  servaddr.sin_port = htons(SERV_PORT); /* assegna la porta del server */
  /* assegna l'indirizzo del server prendendolo dalla riga di comando. L'indirizzo è
  una stringa da convertire in intero secondo network byte order. */
  if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
  /* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
    perror("errore in inet_pton");
    exit(-1);
  }
  len=sizeof(servaddr);
    /* Invia al server il pacchetto di richiesta*/
  if (sendto(sockfd, NULL, 0, 0, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    perror("errore in sendto");
    exit(-1);
  }
  //apro file
  fd = open("prova1.jpg", O_RDWR | O_CREAT, 0666);

   while((n = recvfrom(sockfd, recvline, MAXLINE, 0, (struct sockaddr *) &servaddr, &len))) {
        recvline[n] = 0;
        if (!(strcmp(recvline, "End"))) {
            break;
        }
        write(fd, recvline, n);
    }
  printf("Ho finito di ricevere\n");
  close(fd);
  close(sockfd);
  exit(0);
}