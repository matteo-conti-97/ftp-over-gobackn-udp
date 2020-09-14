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

#define LOSS 0.3
#define WIN_SIZE 4
#define TIMER 10
#define SERV_PORT 4444
#define MAXLINE 512
#define PUT 11
#define GET 12
#define LIST 13
#define EXIT 14

struct segment_packet {
    int type;
    int seq_no;
    int length;
    char data[MAXLINE];
};

struct ack_packet {
    int type;
    int ack_no;
};

bool lost_packet(float loss_rate);

struct ack_packet make_ack_packet (int ack_type, int base);

struct segment_packet make_request_packet(int command);

void print_error(char *error);

void sig_alrm_handler(int signum);

void put(int sockfd);

void get(int sockfd);

void list(int sockfd);

void choice(int sockfd);

int main(int argc, char *argv[]){
  int sockfd, fd, n, len, new_port;
  char buff[MAXLINE];
  struct sockaddr_in servaddr, child_addr;
  struct sigaction sa;

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

  sa.sa_handler = sig_alrm_handler; /* installa il gestore del segnale */
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) < 0) 
    print_error("Errore sigaction");

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

//void new_get(int sockfd, float loss_rate, int timer){
void new_get(int sockfd){
  struct segment_packet data_packet;
  struct ack_packet ack;          
  int recv_msg_size, trial_counter, fd, base=-2, seq_number=0;

  //Apro file
  if((fd = open("prova1.jpg", O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere");
    exit(EXIT_FAILURE);
  }

  data_packet=make_request_packet(GET);
  // Invio richiesta get
  get_request:
  if (send(sockfd, &data_packet, sizeof(data_packet), 0) != sizeof(data_packet))
        print_error("Comportamento inaspettato send");
   //alarm(timer);
   alarm(TIMER);

  //Attendo ack richiesta
  while (recv(sockfd, &ack, sizeof(ack), 0) < 0){
    //Timeout richiesta
    if (errno == EINTR){ 
      //Controllo quante volte e' fallito eventualmente evito di perdere ancora tempo
      if(trial_counter >= 10){
          printf("Impossibile eseguire operazione, tentativi di richiesta esauriti\n");
          exit(EXIT_FAILURE);
      }
      //Ritento
      else{
        alarm(0);
        trial_counter++;
        goto get_request;
      }
    }
  }

  while(1){
    // aspetto dati dal server
    if ((recv_msg_size = recv(sockfd, &data_packet, sizeof(data_packet), 0)) < 0)
      print_error("recv() failed");
    
    seq_number = data_packet.seq_no;

    
    //if(!lost_packet(loss_rate)){
    if(!lost_packet(LOSS)){
        //Segmento iniziale
        if(data_packet.seq_no == 0 && data_packet.type == 1){
            printf("Ricevuto segmento iniziale\n");
            write(fd, data_packet.data, sizeof(data_packet.data));
            base = 0;
            ack = make_ack_packet(2, base);
        } 

        //E' arrivato un segmento in ordine con numero di sequenza maggiore della base
        else if (data_packet.seq_no == base + 1){ 
            printf("Ricevuto segmento successivo alla base, numero:%d\n", data_packet.seq_no);
            write(fd, data_packet.data, sizeof(data_packet.data));
            base = data_packet.seq_no;
            ack = make_ack_packet(2, base);
        }

        else if (data_packet.type == 1 && data_packet.seq_no != base + 1){
            //E' arrivato un segmento fuori ordine
            printf("Ricevuto segmento fuori ordine, numero:%d\n", data_packet.seq_no);
            //Invio ack base
            ack = make_ack_packet(2, base);
        }

        //Ho ricevuto il pacchetto finale (tipo 4)
        if(data_packet.type == 4 && seq_number == base ){
            base = -1;
            //Crea ack finale (tipo 8)
            ack = make_ack_packet(8, base);
        }

        //Invio ack per i pacchetti ricevuti
        if(base >= 0){
            printf("Invio ACK numero:%d\n", base);
            if (send(sockfd, &ack, sizeof(ack), 0) != sizeof(ack))
                print_error("Comportamento inaspettato send");
        } else if (base == -1) {
            printf("Ricevuto segmento finale\n");
            printf("Invio ack finale, numero:%d\n", base);
            if (send(sockfd, &ack, sizeof(ack), 0) != sizeof(ack))
                print_error("Comportamento inaspettato send");
        }

    } else {
        printf("Perdita simulata\n");
    }
  }
  close(fd);
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
    system("rm prova1.jpg");
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

void print_error(char *error){
  perror(error);
  exit(EXIT_FAILURE);
}

struct ack_packet make_ack_packet (int ack_type, int base){
        struct ack_packet ack;
        ack.type = ack_type;
        ack.ack_no = base;
        return ack;
}

struct segment_packet make_request_packet(int command){
  struct segment_packet packet;
  packet.type = command;
  packet.seq_no = 0;
  memset(packet.data, 0, sizeof(packet.data));
  return packet;
}

bool lost_packet(float loss_rate){
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
}