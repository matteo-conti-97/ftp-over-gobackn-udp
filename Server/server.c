#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h> 
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>

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

bool timeout_event=false;

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

struct segment_packet make_data_packet(unsigned long seq_no, int length, char* data);

struct segment_packet make_terminal_packet(unsigned long seq_no, int length);

void print_error(char *error);

typedef void Sigfunc(int); 

Sigfunc* signal(int signum, Sigfunc *handler);

void sig_child_handler(int signum);

void sig_alrm_handler(int signum);

void get(int sockfd, struct sockaddr_in addr, int timer, int window_size, float loss_rate);

void put(int sockfd, struct sockaddr_in addr);

void list(int sockfd, struct sockaddr_in addr);


int main(int argc, char *argv[ ]){
  int sockfd, child_sockfd, len, child_len, command, child_port;
  struct sockaddr_in addr, child_addr;
  pid_t pid;
  struct sigaction sa;

  //random seed per la perdita simulata
  srand48(2345);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket padre");
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
  memset((void *)&addr, 0, sizeof(addr));
  memset((void *)&child_addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); /* il server accetta pacchetti su una qualunque delle sue interfacce di rete */
  addr.sin_port = htons(SERV_PORT); /* numero di porta del server */
  len = sizeof(addr);

  /* assegna l'indirizzo al socket */
  if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("errore in bind");
    exit(EXIT_FAILURE);
  }

  if (signal(SIGCHLD, sig_child_handler) == SIG_ERR) { 
    fprintf(stderr, "errore in signal");
    exit(1);
  }

  sa.sa_handler = sig_alrm_handler;
  /* installa il gestore del segnale */
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) < 0) {
    fprintf(stderr, "errore in sigaction");
    exit(EXIT_FAILURE);
  }

  while (1) {
    //Ascolto di richieste di connessione dei client
    if ((recvfrom(sockfd, &command, sizeof(int), 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom");
      exit(EXIT_FAILURE);
    }

    //Creo il socket del figlio dedicato al client
    if ((child_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
    perror("errore in socket figlio");
    exit(EXIT_FAILURE);
    }

    child_addr.sin_family = AF_INET;
    child_addr.sin_addr.s_addr = htonl(INADDR_ANY); /* il figlio accetta pacchetti su una qualunque delle sue interfacce di rete */
    child_addr.sin_port = htons(0);
    child_len=sizeof(child_addr);

    /* assegna l'indirizzo al socket del figlio */
    if (bind(child_sockfd, (struct sockaddr *) &child_addr, sizeof(child_addr)) < 0) {
    perror("errore in bind");
    exit(EXIT_FAILURE);
    }

    //Prendo il numero di porta del figlio
    if(getsockname(child_sockfd, (struct sockaddr *) &child_addr, &child_len)<0){
      perror("errore acquisizione numero porta del socket processo figlio");
      exit(EXIT_FAILURE);
    }
    child_port=child_addr.sin_port;
    //Invio la nuova porta al client
    if(sendto(sockfd, &child_port, sizeof(int), 0, (struct sockaddr *)&addr, sizeof(addr))<0){
      perror("errore in sendto porta figlio");
      exit(EXIT_FAILURE);
    }

    if ((pid = fork()) == 0){
      printf("Sono nel figlio\n");
      while(1){
        if ((recvfrom(child_sockfd, &command, sizeof(int), 0, (struct sockaddr *)&child_addr, &child_len)) < 0) {
          perror("errore in recvfrom");
          exit(EXIT_FAILURE);
        }

        switch(command){
          case PUT:
            //printf("Ho ricevuto il comando put %d\n",command);
            put(child_sockfd, child_addr);
            break;
          case GET:
            //printf("Ho ricevuto il comando get %d\n",command);
            get(child_sockfd, child_addr, TIMER, WIN_SIZE, LOSS);
            break;
          case LIST:
            //printf("Ho ricevuto il comando list %d\n",command);
            list(child_sockfd, child_addr);
            break;
          case EXIT:
            close(child_sockfd);
            exit(EXIT_SUCCESS);
            break;
          default:
            break;
        }
      }
    }
  }
 
   exit(EXIT_SUCCESS);
}




Sigfunc *signal(int signum, Sigfunc *func){
  struct sigaction  act, oact;
    /* la struttura sigaction memorizza informazioni riguardanti la
  manipolazione del segnale */

  act.sa_handler = func;
  sigemptyset(&act.sa_mask);  /* non occorre bloccare nessun altro segnale */
  act.sa_flags = 0;
  if (signum != SIGALRM) 
     act.sa_flags |= SA_RESTART;  
  if (sigaction(signum, &act, &oact) < 0)
    return(SIG_ERR);
  return(oact.sa_handler);
}

void sig_child_handler(int signum){
  int status;
  pid_t pid;
  while((pid = waitpid(WAIT_ANY, &status, WNOHANG)>0))
    printf("Figlio %d terminato",pid);
  return;
}

void get(int sockfd, struct sockaddr_in addr, int timer, int window_size, float loss_rate){
  int fd,n, addr_len=sizeof(addr);
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  unsigned long base=0,next_seq_no=0, file_size;

  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    exit(EXIT_FAILURE);
  }

  memset(packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  if((fd=open("./files/imgPiccola.jpg",O_RDONLY))<0){
    perror("errore apertura file da inviare");
    exit(EXIT_FAILURE);
  }

  //Calcolo dimensione file
  lseek(fd, 0, SEEK_SET);
  file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  while((ack.seq_no+1)*497 < file_size){

    if(timeout_event){
      alarm(timer);
      for(int i=0; i<window_size; i++){
          sendto(sockfd, &packet_buffer[i],sizeof(packet_buffer[i]), 0, (struct sockaddr *) &addr, sizeof(addr));
          //Mettere rtt e timer dinamico
      }
      timeout_event=false;
    }    
    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((packet_buffer[next_seq_no%window_size].length = read(fd, packet_buffer[next_seq_no%window_size].data, MAXLINE)) > 0){
        packet_buffer[next_seq_no%window_size].seq_no = next_seq_no;
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        //Lancia il conteggio dell'rtt
        if(base == next_seq_no){
          //Lancia il timer
          alarm(timer);
          printf("Ho lanciato il timer\n");
        }
        next_seq_no++;
      }
    }

    //Attendo gli ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) > 0){ 
      if(!simulate_loss(loss_rate)){
        base = ack.seq_no;
        //Ferma conteggio rtt  
        if(base == next_seq_no){
          alarm(0);
          printf("Ho fermato il timer\n");
        }
      //}
      else
        printf("PERDITA ACK SIMULATA\n");
      
    }         
  }


  data.type=FINAL_DATA;
  data.seq_no=next_seq_no;
  sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, sizeof(addr));
  printf("Inviato dato finale\n");
  alarm(timer);
  recvfrom(sockfd, &ack, sizeof(struct ack_packet),0, (struct sockaddr *) &addr, &addr_len);
  if(ack.type==FINAL_ACK){
    printf("Ho ricevuto ack finale\n");
    alarm(0);
  }
  close(fd);
}

void put(int sockfd, struct sockaddr_in addr){
  int n, len, fd, command=2;
  char buff[MAXLINE];
  len=sizeof(addr);
  //apro file
  if((fd = open("files/prova1.jpg", O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere");
    exit(EXIT_FAILURE);
  }

   while((n = recvfrom(sockfd, buff, MAXLINE, 0, (struct sockaddr *) &addr, &len))) {
        buff[n] = 0;
        if (!(strcmp(buff, "End"))) {
            break;
        }
        write(fd, buff, n);
    }
  printf("Ho finito di ricevere\n");
  close(fd);
}

void list(int sockfd, struct sockaddr_in addr){
  DIR *d;
  struct dirent *dir;
  int n;
  if((d = opendir("./files"))==NULL){
    perror("errore apertura directory dei file");
    exit(EXIT_FAILURE);
  }
  while ((dir = readdir(d)) != NULL) {
    if((strcmp(dir->d_name,".")==0)||(strcmp(dir->d_name,"..")==0)) continue;
    if (sendto(sockfd, &(dir->d_name), sizeof(dir->d_name), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      perror("errore in sendto");
      exit(EXIT_FAILURE);
    }
  }
  printf("Ho finito di inviare\n");
  sendto(sockfd, "End", strlen("End"), 0, (struct sockaddr *) &addr, sizeof(addr));
  closedir(d);
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

void sig_alrm_handler(int signum){
   printf("SIGALRM\n");
   timeout_event=true;
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

struct segment_packet make_data_packet (unsigned long seq_no, int length, char* data){

    struct segment_packet packet;

    packet.type = NORMAL_DATA;
    packet.seq_no = seq_no;
    packet.length = length;
    memset(packet.data, 0, sizeof(packet.data));
    strcpy(packet.data, data);

    return packet;
}

struct segment_packet make_terminal_packet (unsigned long seq_no, int length){

    struct segment_packet packet;

    packet.type = FINAL_DATA;
    packet.seq_no = seq_no;
    packet.length = 0;
    memset(packet.data, 0, sizeof(packet.data));

    return packet;
}