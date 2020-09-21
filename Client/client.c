#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>

#define NORMAL_DATA 5
#define FINAL_DATA 6
#define NORMAL_ACK 7
#define FINAL_ACK 8
#define LOSS 0.05
#define WIN_SIZE 4
#define TIMER 2
#define SERV_PORT 4444
#define MAXLINE 497
#define PUT 1
#define GET 2
#define LIST 3
#define EXIT 4

struct segment_packet {
    int type;
    unsigned long seq_no;
    int length;
    char data[MAXLINE];
};

struct ack_packet {
    int type;
    unsigned long seq_no;
};

bool simulate_loss(float loss_rate);

struct ack_packet make_ack_packet (int ack_type, unsigned long base);

struct segment_packet make_request_packet(int command);

void print_error(char *error);

void sig_alrm_handler(int signum);

void put(int sockfd);

void get(int sockfd, int timer, float loss_rate);

void list(int sockfd);

void choice(int sockfd);

void new_get(int sockfd, int timer, float loss_rate);

int main(int argc, char *argv[]){
  int sockfd, fd, n, len, new_port;
  char buff[MAXLINE];
  struct sockaddr_in servaddr, child_addr;
  struct sigaction sa;

  if (argc != 2) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: client <indirizzo IP server>\n");
    exit(1);
  }

  //random seed per la perdita simulata
  srand48(2345);

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

  sa.sa_handler = sig_alrm_handler;
  /* installa il gestore del segnale */
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) < 0) {
    fprintf(stderr, "errore in sigaction");
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
      get(sockfd, TIMER, LOSS);
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

void get(int sockfd, int timer, float loss_rate){
  int n, fd,  command=2;
  struct segment_packet data;
  struct ack_packet ack;
  unsigned long expected_seq_no=0;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;
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

  while(1){
    if(recv(sockfd, &data, sizeof(data), 0)!=sizeof(data)){
      perror("Pacchetto corrotto errore recv\n");
    }
    if(!simulate_loss(loss_rate)){
      //Se arriva un pacchetto in ordine lo riscontro e aggiorno il numero di sequenza che mi aspetto
      if(data.seq_no==expected_seq_no){
        if(data.type==FINAL_DATA){
          printf("Ho ricevuto dato finale\n");
          ack=make_ack_packet(FINAL_ACK,data.seq_no);
          break;
        }
        else{
          data.data[data.length]=0;
          printf("Ho ricevuto un dato di %d byte\n", data.length);
          if((n=write(fd, data.data, data.length))!=data.length){
            perror("Non ho scritto tutto su file errore");
          }
          printf("Ho scritto %d byte sul file\n",n);
          ack=make_ack_packet(NORMAL_ACK,data.seq_no);
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine o corrotto invio l'ack della precedente iterazione

      //Invio ack
      send(sockfd, &ack, sizeof(ack), 0);

   //}
    else
      printf("PERDITA PACCHETTO SIMULATA\n");
  } 

  //Invio ack finale
  send(sockfd, &ack, sizeof(ack), 0);
  close(fd);
  printf("\nGET terminata\n\n");
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

void print_error(char *error){
  perror(error);
  exit(EXIT_FAILURE);
}

struct ack_packet make_ack_packet (int ack_type, unsigned long base){
        struct ack_packet ack;
        ack.type = ack_type;
        ack.seq_no = base;
        return ack;
}

struct segment_packet make_request_packet(int command){
  struct segment_packet packet;
  packet.type = command;
  packet.seq_no = 0;
  memset(packet.data, 0, sizeof(packet.data));
  return packet;
}

bool simulate_loss(float loss_rate){
    double rv;
    rv = drand48();
    if (rv < loss_rate)
    {
        return true;
    } else {
        return false;
    }
}

void sig_alrm_handler(int signum){
  printf("SIGALRM\n");

}