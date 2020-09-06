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
#define PUT 1
#define GET 2
#define LIST 3
#define EXIT 4

void put(int sockfd){
  int fd, n, command=1;
  char buff[MAXLINE];
  memset(buff,0,sizeof(buff));
  /* Invia al server il pacchetto di richiesta*/
  if (send(sockfd, &command, sizeof(int), 0) < 0) {
    perror("errore in send");
    exit(-1);
  }
  fd=open("imgPiccola.jpg",O_RDONLY);
  while((n = read(fd, buff, MAXLINE))>0)
  {
    //usleep(1000);
    if (send(sockfd, buff, n, 0) < 0)
    {
      perror("errore in send");
      exit(-1);
    }
    memset(buff,0,sizeof(buff));
  }
  printf("\nPUT terminata\n\n");
  send(sockfd, "End", strlen("End"), 0);
  close(fd);
}

void get(int sockfd){
  int n, fd, command=2;
  char buff[MAXLINE];
    /* Invia al server il pacchetto di richiesta*/
  if (send(sockfd, &command, sizeof(int), 0) < 0) {
    perror("errore in send");
    exit(-1);
  }
  //apro file
  fd = open("prova1.jpg", O_RDWR | O_CREAT| O_TRUNC, 0666);

   while((n = recv(sockfd, buff, MAXLINE, 0))) {
        buff[n] = 0;
        if (!(strcmp(buff, "End"))) {
            break;
        }
        write(fd, buff, n);
    }
  printf("\nGET terminata\n\n");
  close(fd);
}

void list(int sockfd){
  int n, fd, command=3;
  char buff[MAXLINE];
    /* Invia al server il pacchetto di richiesta*/
  if (send(sockfd, &command, sizeof(int), 0) < 0) {
    perror("errore in send");
    exit(-1);
  }
  printf("Lista dei file su server:\n\n");
  //listo file
   while((n = recv(sockfd, buff, MAXLINE, 0))) {
        buff[n] = 0;
        if (!(strcmp(buff, "End"))) {
            break;
        }
        printf("%s\n", buff);
    }
  printf("\nLIST terminata\n\n");
}

void choice(int sockfd){
  int n;
  printf("Cosa posso fare per te?\n");
  printf("1)PUT\n");
  printf("2)GET\n");
  printf("3)LIST\n");
  printf("4)EXIT\n");
  printf("Inserisci il numero dell'operazione da eseguire\n");
  selectOpt:
  if(scanf("%d",&n)!=1){
    perror("errore acquisizione operazione da eseguire");
    exit(1);
  }
  switch(n){
    case PUT:
      system("clear");
      put(sockfd);
      break;
    case GET:
      system("clear");
      get(sockfd);
      break;
    case LIST:
      system("clear");
      list(sockfd);
      break;
    case EXIT:
      system("clear");
      close(sockfd);
      exit(0);
    default:
      printf("Inserisci un numero valido\n");
      goto selectOpt;
      break;
  }
}

int main(int argc, char *argv[ ]){
  int sockfd, fd, n, len;
  char buff[MAXLINE];
  struct sockaddr_in servaddr;
  if (argc != 2) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP server>\n");
    exit(1);
  }
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(-1);
  }
  //Buffer extension
  /*int a = 65535;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &a, sizeof(int)) == -1) {
    perror("errore resize buffer ricezione");
  }
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &a, sizeof(int)) == -1) {
    perror("errore resize buffer ricezione");
  }*/

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
  /* stabilisce la connessione con il server */
  if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    perror("errore in connect");
    exit(-1);
  }
  printf("Ti sei connesso con successo al server\n");
  while(1){
  choice(sockfd);
  }
  close(sockfd);
  exit(0);
}