#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#define SERV_PORT 4444
#define MAXLINE 1024
#define PUT 1
#define GET 2
#define LIST 3
#define EXIT 4

void put(int sockfd);

void get(int sockfd);

void list(int sockfd);

void choice(int sockfd);

int main(int argc, char *argv[]){
  int sockfd, fd, n, len, new_port;
  char buff[MAXLINE];
  struct sockaddr_in servaddr, child_addr;
  if (argc != 2) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: client <indirizzo IP server>\n");
    exit(1);
  }
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(EXIT_FAILURE);
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
  memset((void *)&child_addr, 0, sizeof(child_addr));
  /* azzera servaddr */
  servaddr.sin_family = AF_INET;
  /* assegna il tipo di indirizzo */
  servaddr.sin_port = htons(SERV_PORT); /* assegna la porta del server */
  /* assegna l'indirizzo del server prendendolo dalla riga di comando. L'indirizzo è
  una stringa da convertire in intero secondo network byte order. */
  if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
  /* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
    perror("errore in inet_pton");
    exit(EXIT_FAILURE);
  }

  /* stabilisce la connessione con il server */
  if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }

 if (send(sockfd, NULL, 0, 0) < 0) {
    perror("errore in send connessione con richiesta al server");
    exit(EXIT_FAILURE);
  }

  if(recv(sockfd, &new_port, sizeof(int), 0)<0){
    perror("errore in recv porta dedicata");
    exit(EXIT_FAILURE);
  }
  /* azzera child_addr */
  child_addr.sin_family = AF_INET;
  /* assegna il tipo di indirizzo */
  child_addr.sin_port = new_port; /* assegna la porta del figlio */
  if (inet_pton(AF_INET, argv[1], &child_addr.sin_addr) <= 0) {
  /* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
    perror("errore in inet_pton");
    exit(EXIT_FAILURE);
  }

  /* stabilisce la connessione con il processo dedicato */
  if (connect(sockfd, (struct sockaddr *) &child_addr, sizeof(child_addr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }
  printf("Ti sei connesso con successo\n");

  while(1){
  choice(sockfd);
  }
  close(sockfd);
  exit(EXIT_SUCCESS);
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
      //system("clear");
      put(sockfd);
      break;
    case GET:
      //system("clear");
      get(sockfd);
      break;
    case LIST:
      //system("clear");
      list(sockfd);
      break;
    case EXIT:
      //system("clear");
      close(sockfd);
      exit(EXIT_SUCCESS);
      break;
    default:
      printf("Inserisci un numero valido\n");
      goto selectOpt;
      break;
  }
}

void list(int sockfd){
  int n, fd, command=3;
  char buff[MAXLINE];
    /* Invia al server il pacchetto di richiesta*/
  if (send(sockfd, &command, sizeof(int), 0) < 0) {
    perror("errore in send");
    exit(EXIT_FAILURE);
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

void get(int sockfd){
  int n, fd, command=2;
  char buff[MAXLINE];

    //apro file
  if((fd = open("prova1.jpg", O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere");
    exit(EXIT_FAILURE);
  }

    /* Invia al server il pacchetto di richiesta*/
  if (send(sockfd, &command, sizeof(int), 0) < 0) {
    perror("errore in send");
    exit(-1);
  }

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

void put(int sockfd){
  int fd, n, command=1;
  char buff[MAXLINE];
  memset(buff,0,sizeof(buff));
  
  // apro file
  if((fd=open("imgPiccola.jpg",O_RDONLY))<0){
    perror("errore apertura file da inviare");
    exit(EXIT_FAILURE);
  }

  /* Invia al server il pacchetto di richiesta*/
  if (send(sockfd, &command, sizeof(int), 0) < 0) {
    perror("errore in send");
    exit(EXIT_FAILURE);
  }

  while((n = read(fd, buff, MAXLINE))>0)
  {
    //usleep(1000);
    if (send(sockfd, buff, n, 0) < 0)
    {
      perror("errore in send");
      exit(EXIT_FAILURE);
    }
    memset(buff,0,sizeof(buff));
  }
  printf("\nPUT terminata\n\n");
  send(sockfd, "End", strlen("End"), 0);
  close(fd);
}