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
#include <time.h>
#include <sys/time.h>

#define NORMAL10
#define FIN 11
#define SYN 14
#define MAXLINE 497
#define PUT 1
#define GET 2
#define LIST 3

bool timeout_event=false;

struct segment_packet {
    int type;
    bool is_ack;
    unsigned long seq_no;
    int length;
    char data[MAXLINE];
};

struct ack_packet {
    int type;
    unsigned long seq_no;
};

bool simulate_loss(float loss_rate);

void print_error(char *error);

void sig_alrm_handler(int signum);

void put(int sockfd);

void get(int sockfd, long timer, float loss_rate);

void list(int sockfd);


int main(int argc, char *argv[]){
  int sockfd,n, serv_port, new_port, window_size;
  struct sockaddr_in servaddr, child_addr;
  struct sigaction sa;
  struct segment_packet data;
  struct ack_packet ack;
  unsigned long conn_req_no;
  float loss_rate;
  long timer;

  memset((void *)&data,0,sizeof(data));
  memset((void *)&ack,0,sizeof(ack));

  if (argc < 6) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: client <indirizzo IP server> <porta server> <dimensione finestra> <probabilita' perdita (float, -1 for 0)> <timeout (us)> -1 for dynamic timer\n");
    exit(EXIT_FAILURE);
  }

  if((serv_port=atoi(argv[2]))<1024){
    fprintf(stderr,"inserisci un numero di porta valido\n");
    exit(EXIT_FAILURE);
  }

  if((window_size=atoi(argv[3]))==0){
    fprintf(stderr,"inserisci dimensione finestra valida\n");
    exit(EXIT_FAILURE);
  }

  if((loss_rate=atof(argv[4]))==0){
      fprintf(stderr,"inserisci un loss rate valido\n");
      exit(EXIT_FAILURE);
  }

  if(loss_rate==-1)
    loss_rate=0;

  if((timer=atol(argv[5]))==0){
    fprintf(stderr,"inserisci un timer valido\n");
    exit(EXIT_FAILURE);
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
  servaddr.sin_port = htons(serv_port); /* assegna la porta del server */
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

  // Per non usare sempre sendto e recvfrom
  if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }

  conn_req_no=lrand48();
  data.seq_no=conn_req_no;
  data.type=SYN;
  if (send(sockfd, &data, sizeof(data), 0) < 0) {
    perror("errore in send richiesta connession syn");
    exit(EXIT_FAILURE);
  }
  
  while(1){
    memset((void *)&data,0,sizeof(data));
    alarm(timer);
    if(recv(sockfd, &data, sizeof(data), 0)<0){
      perror("errore in recv porta dedicata syn_ack");
      exit(EXIT_FAILURE);
    }
    alarm(0);
    if(data.seq_no==conn_req_no)
      break;
  }
  new_port=atoi(data.data);
  ack.type=SYN;
  ack.seq_no=conn_req_no;
  if (send(sockfd, &ack, sizeof(ack), 0) < 0) {
    perror("errore in send ack_syn_ack");
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

  // Per non usare sempre sendto e recvfrom
  if (connect(sockfd, (struct sockaddr *) &child_addr, sizeof(child_addr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }
  printf("Ti sei connesso con successo\n");

  while(1){
    printf("Cosa posso fare per te? Hai un minuto per scegliere\n");
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
        get(sockfd, timer, loss_rate);
        break;
      case LIST:
        //system("clear");
        list(sockfd);
        break;
      default:
        printf("Inserisci un numero valido\n");
        goto selectOpt;
        break;
    }
  }
  close(sockfd);
  exit(EXIT_SUCCESS);
}


void list(int sockfd){
  int n, command=3;
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

void get(int sockfd, long timer, float loss_rate){
  int n, fd, trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  unsigned long expected_seq_no=0;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;
  file_choice:
  printf("Inserire il nome del file da scaricare (con estensione):\n");
  if(scanf("%s",data.data)!=1){
    perror("inserire un nome valido");
    char c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    goto file_choice;
  }

  printf("E' stato scelto %s \n", data.data);
   //apro file
  if((fd = open(data.data, O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere controllare che il file sia presente sul server");
    exit(EXIT_FAILURE);
  }

  data.type=GET;
  while(1){
    if(trial_counter>=10){
      printf("Il server e' morto");
      exit(EXIT_FAILURE);
    }
    /* Invia al server il pacchetto di richiesta*/
    if(send(sockfd, &data, sizeof(data), 0) != sizeof(data)) {
      perror("errore in send file name");
      exit(EXIT_FAILURE);
    }
    alarm(timer);
    printf("Inviato nome file da aprire\n");
    if(recv(sockfd,&ack, sizeof(ack),0) == sizeof(ack)){
      alarm(0);
      trial_counter=0;
      break;
    }
    perror("errore recv comando");
    trial_counter++;
  }
  trial_counter=0;

  //Ricevo dati
  while(1){
    if(trial_counter>=10){
      printf("Il server e' morto\n");
      close(fd);
      system("rm prova1.jpg");
      exit(EXIT_FAILURE);
    }
    if(timeout_event){
      trial_counter==;
      timeout_event=false;
    }

    alarm(timer);
    if(recv(sockfd, &data, sizeof(data), 0)!=sizeof(data)){
      perror("Pacchetto corrotto errore recv\n");
      continue;
    }
    if(!simulate_loss(loss_rate)){
      alarm(0);
      trial_counter=0;
      //Se arriva un pacchetto in ordine lo riscontro e aggiorno il numero di sequenza che mi aspetto
      if(data.seq_no==expected_seq_no){
        if(data.type==FIN){
          printf("Ho ricevuto FIN\n");
          ack.type=FIN;
          ack.seq_no=data.seq_no;
          break;
        }
        else{
          data.data[data.length]=0;
          printf("Ho ricevuto un dato di %d byte\n", data.length);
          if((n=write(fd, data.data, data.length))!=data.length){
            perror("Non ho scritto tutto su file mi riposiziono\n");
            lseek(fd,0,SEEK_CUR-n);
            continue;
          }
          printf("Ho scritto %d byte sul file\n",n);
          ack.type=NORMAL;
          ack.seq_no=data.seq_no;
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine o corrotto invio l'ack della precedente iterazione

      //Invio ack
      send(sockfd, &ack, sizeof(ack), 0);
      printf("ACK inviato\n");

   }
    else
      printf("PERDITA PACCHETTO SIMULATA\n");
  } 

  //Invio ack finale
  send(sockfd, &ack, sizeof(ack), 0);
  printf("FIN ACK inviato\n");
  close(fd);
  printf("\nGET terminata\n\n");
  exit(EXIT_SUCCESS);
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
  timeout_event=true;
}