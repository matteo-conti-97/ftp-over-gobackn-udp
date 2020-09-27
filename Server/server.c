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
#include <sys/time.h>
#include <time.h>

#define DEFAULT_TIMER 50000
#define MAX_CHOICE_TIME 60
#define NORMAL 10
#define FIN 11
#define MAXLINE 497
#define PUT 1
#define GET 2
#define LIST 3

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

typedef void Sigfunc(int); 

Sigfunc* signal(int signum, Sigfunc *handler);

void sig_child_handler(int signum);

void sig_alrm_handler(int signum);

void get(int sockfd, struct sockaddr_in addr, long timer, int window_size, float loss_rate, char *file_name);

void put(int sockfd, struct sockaddr_in addr);

void list(int sockfd, struct sockaddr_in addr);

void set_timer(long timer);

int main(int argc, char *argv[]){
  int sockfd, child_sockfd, serv_port, window_size, len, child_len;
  struct sockaddr_in addr, child_addr;
  pid_t pid;
  struct sigaction sa;
  struct segment_packet data;
  struct ack_packet ack;
  float loss_rate;
  long timer;

  memset((void *)&data,0,sizeof(data));
  memset((void *)&ack,0,sizeof(ack));

  if (argc < 5) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: client <porta server> <dimensione finestra> <probabilita' perdita (float, -1 for 0)> <timeout (us, -1 for dynamic timer)>\n");
    exit(EXIT_FAILURE);
  }

  if((serv_port=atoi(argv[1]))<1024){
    fprintf(stderr,"inserisci un numero di porta valido\n");
    exit(EXIT_FAILURE);
  }

  if((window_size=atoi(argv[2]))==0){
    fprintf(stderr,"inserisci dimensione finestra valida\n");
    exit(EXIT_FAILURE);
  }

  if((loss_rate=atof(argv[3]))==0){
      fprintf(stderr,"inserisci un loss rate valido\n");
      exit(EXIT_FAILURE);
  }

  if(loss_rate==-1)
    loss_rate=0;

  if((timer=atol(argv[4]))==0){
    fprintf(stderr,"inserisci un timer valido\n");
    exit(EXIT_FAILURE);
  }

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
  addr.sin_port = htons(serv_port); /* numero di porta del server */
  len = sizeof(addr);

  /* assegna l'indirizzo al socket */
  if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("errore in bind");
    exit(EXIT_FAILURE);
  }

  if (signal(SIGCHLD, sig_child_handler) == SIG_ERR) { 
    fprintf(stderr, "errore in signal");
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
  start:
  while (1) {
    printf("Attendo richieste dei client\n");
    //Ascolto di richieste di connessione dei client
    if ((recvfrom(sockfd, &data, sizeof(data), 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom attesa client");
      exit(EXIT_FAILURE);
    }
    printf("Si e' connesso un client\n");

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
    perror("errore in bind socket figlio");
    exit(EXIT_FAILURE);
    }

    //Prendo il numero di porta del figlio
    if(getsockname(child_sockfd, (struct sockaddr *) &child_addr, &child_len)<0){
      perror("errore acquisizione numero porta del socket processo figlio");
      exit(EXIT_FAILURE);
    }
    sprintf(data.data,"%d",child_addr.sin_port);
    //Invio la nuova porta al client
    if(sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *)&addr, sizeof(addr))<0){
      perror("errore in sendto porta figlio syn_ack");
      close(child_sockfd);
      goto start;
    }

    while(1){
      //Ascolto di richieste di connessione dei client
      if(timer<0)
        set_timer(DEFAULT_TIMER);
      else
        set_timer(timer);
      if ((recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&addr, &len)) < 0) {
        perror("errore in recvfrom ach_syn_ack");
        close(child_sockfd);
        goto start;
      }
      set_timer(0);
      //Controllo che sia la richiesta corretta
      if(ack.seq_no==data.seq_no)
        break;
    }
    if ((pid = fork()) == 0){
      printf("Sono nel figlio\n");
      while(1){
        alarm(MAX_CHOICE_TIME);
        if ((recvfrom(child_sockfd, &data, sizeof(data), 0, (struct sockaddr *)&child_addr, &child_len)) < 0) {
          perror("errore in recvfrom comando");
          exit(EXIT_FAILURE);
        }

        switch(data.type){
          case PUT:
            alarm(0);
            timeout_event=false;
            //printf("Ho ricevuto il comando put %d\n",command);
            put(child_sockfd, child_addr);
            break;
          case GET:
            alarm(0);
            timeout_event=false;
            //printf("Ho ricevuto il comando get %d\n",data.type);
            //printf("Il nome del file scelto e' %s", data.data);
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))!=sizeof(ack)){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            get(child_sockfd, child_addr, timer, window_size, loss_rate, data.data);
            break;
          case LIST:
            alarm(0);
            timeout_event=false;
            //printf("Ho ricevuto il comando list %d\n",command);
            list(child_sockfd, child_addr);
            break;
          default:
            break;
        }
      }
    }
  }
 
   exit(EXIT_SUCCESS);
}



void get(int sockfd, struct sockaddr_in addr, long timer, int window_size, float loss_rate, char *file_name){
  int fd, trial_counter=0, addr_len=sizeof(addr);
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  unsigned long base=0,next_seq_no=0, file_size;
  bool dyn_timer_enable=false;
  long sample_RTT=0, estimated_RTT=0, dev_RTT=0, dyn_timer_value=DEFAULT_TIMER; //microsecondi
  struct timeval start_sample, end_sample;
  char path[505];
  strcpy(path,"./files/");

  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    exit(EXIT_FAILURE);
  }

  memset((void *)&start_sample,0,sizeof(start_sample));
  memset((void *)&end_sample,0,sizeof(end_sample));
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  //Se timer e' minore di 0 attivo il timer dinamico
  if(timer<0)
    dyn_timer_enable=true;

  strcat(path,file_name);
  if((fd=open(path,O_RDONLY))<0){
    perror("errore apertura file da inviare"); 
    exit(EXIT_FAILURE);
  }

  //Calcolo dimensione file
  lseek(fd, 0, SEEK_SET);
  file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  while((ack.seq_no+1)*497 < file_size){
    if(trial_counter>=10){
      printf("Abort il client e' morto o e' irraggiungibile\n");
      exit(EXIT_SUCCESS);
    }
    //Se c'e' stato un timeout ritrasmetti tutti i pacchetti in finestra
    if(timeout_event){
      trial_counter++;
      if(dyn_timer_enable)
        set_timer(dyn_timer_value);
      else
        set_timer(timer);

      gettimeofday(&start_sample,NULL);
      for(int i=0; i<window_size; i++){
          sendto(sockfd, &packet_buffer[i],sizeof(packet_buffer[i]), 0, (struct sockaddr *) &addr, sizeof(addr));
      }
      printf("Rinviati pacchetti e iniziato campionamento\n");
      timeout_event=false;
    }    

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((packet_buffer[next_seq_no%window_size].length = read(fd, packet_buffer[next_seq_no%window_size].data, MAXLINE)) > 0){
        packet_buffer[next_seq_no%window_size].seq_no = next_seq_no;
        packet_buffer[next_seq_no%window_size].type = NORMAL;
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        gettimeofday(&start_sample,NULL);
        printf("Inviato pacchetto %ld e iniziato campionamento\n",packet_buffer[next_seq_no%window_size].seq_no);

        //Se il next sequence number corrisponde con la base lancia il timer
        if(base == next_seq_no){
          if(dyn_timer_enable){
            set_timer(dyn_timer_value);
            printf("Ho lanciato il timer dinamico di %ld us\n",dyn_timer_value);
          }
          else{
            set_timer(timer);
            printf("Ho lanciato il timer statico di %ld us\n",timer);
          } 
        }

        next_seq_no++;
      }
    }

    //Controllo se ci sono ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) > 0){ 
      if(!simulate_loss(loss_rate)){
        trial_counter=0; //Vuoldire che il client e' ancora vivo
        printf("ACK %ld ricevuto, ricalcolo timer\n",ack.seq_no);
        base = ack.seq_no+1;
        gettimeofday(&end_sample,NULL);
        sample_RTT=((end_sample.tv_sec - start_sample.tv_sec) * 1000000) + (end_sample.tv_usec - start_sample.tv_usec);
        //printf("sample_RTT %ld\n",sample_RTT);
        estimated_RTT=(estimated_RTT*0.875)+(sample_RTT*0.125);
        //printf("estimated_RTT %ld\n",estimated_RTT);
        dev_RTT=(dev_RTT*0.75)+((abs(sample_RTT-estimated_RTT))*0.25);
        //printf("dev_RTT %ld\n",dev_RTT);
        dyn_timer_value=estimated_RTT+(dev_RTT*4);
        //printf("Il nuovo timer e' %ld\n", dyn_timer_value);
        //Stop del timer associato al pacchetto piu' vecchio della finestra 
        if(base == next_seq_no){
          set_timer(0);
          printf("Ho fermato il timer\n");
        }
        else{
          if(dyn_timer_enable){
            set_timer(dyn_timer_value);
            printf("Ho rilanciato il timer dinamico di %ld us\n",dyn_timer_value);
          }
          else{
            set_timer(timer);
            printf("Ho rilanciato il timer statico di %ld us\n",timer);
          }
        }
      }
      else
        printf("PERDITA ACK SIMULATA\n");
      
    }         
  }

  trial_counter=-1;
  while(1){
    timeout_event=false;
    trial_counter++;
    if(trial_counter>=10){
      printf("Ho consegnato il pacchetto con successo ma il client e' morto o e' irraggiungibile\n");
      exit(EXIT_SUCCESS);
    }
    data.type=FIN;
    data.seq_no=next_seq_no;
    sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, sizeof(addr));
    printf("Inviato FIN\n");
    if(dyn_timer_enable){
        set_timer(dyn_timer_value);
        printf("Ho lanciato il timer dinamico finale di %ld us\n",dyn_timer_value);
      }
      else{
        set_timer(timer);
        printf("Ho lanciato il timer statico finale di %ld us\n",timer);
      }
    recvfrom(sockfd, &ack, sizeof(struct ack_packet),0, (struct sockaddr *) &addr, &addr_len);
    if(!simulate_loss(loss_rate)){
      if(ack.type==FIN){
        printf("Ho ricevuto FIN ACK\n");
        set_timer(0);
        break;
      }
   }
   else
    printf("PERDITA ACK FINALE SIMULATA\n");
  }
  close(fd);
  printf("Get terminata\n");
  exit(EXIT_SUCCESS);
}

void put(int sockfd, struct sockaddr_in addr){
  int n, len, fd;
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

void new_list(int sockfd, struct sockaddr_in addr, long timer, int window_size, float loss_rate){
  DIR *d;
  struct dirent *dir;
  off_t head, cur;
  unsigned long num_of_files=0, base=0,next_seq_no=0;
  int trial_counter=0, addr_len=sizeof(addr);
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  bool dyn_timer_enable=false;
  long sample_RTT=0, estimated_RTT=0, dev_RTT=0, dyn_timer_value=DEFAULT_TIMER; //microsecondi
  struct timeval start_sample, end_sample;

  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    exit(EXIT_FAILURE);
  }

  memset((void *)&start_sample,0,sizeof(start_sample));
  memset((void *)&end_sample,0,sizeof(end_sample));
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  //Se timer e' minore di 0 attivo il timer dinamico
  if(timer<0)
    dyn_timer_enable=true;

  //Apro la directory contente i file
  if((d = opendir("./files"))==NULL){
    perror("errore apertura directory dei file");
    exit(EXIT_FAILURE);
  }

  head=telldir(d);//Tengo traccia della testa
  //Conto quanti pacchetti dovro' inviare
  while ((dir = readdir(d)) != NULL)
    num_of_files++;
  printf("numero di file %ld\n",num_of_files);
  //Mi riposiziono
  seekdir(d,head);

  //Invio la lista dei file
  while((ack.seq_no+1)<num_of_files){
     if(trial_counter>=10){
      printf("Abort il client e' morto o e' irraggiungibile\n");
      exit(EXIT_SUCCESS);
    }
    //Se c'e' stato un timeout ritrasmetti tutti i pacchetti in finestra
    if(timeout_event){
      trial_counter++;
      if(dyn_timer_enable)
        set_timer(dyn_timer_value);
      else
        set_timer(timer);

      gettimeofday(&start_sample,NULL);
      for(int i=0; i<window_size; i++){
          sendto(sockfd, &packet_buffer[i],sizeof(packet_buffer[i]), 0, (struct sockaddr *) &addr, sizeof(addr));
      }
      //printf("Rinviati pacchetti e iniziato campionamento\n");
      timeout_event=false;
    }    

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((dir = readdir(d)) != NULL){
        if((strcmp(dir->d_name,".")==0)||(strcmp(dir->d_name,"..")==0)) continue;
        strcpy(packet_buffer[next_seq_no%window_size].data, dir->d_name);
        packet_buffer[next_seq_no%window_size].length=strlen(dir->d_name);
        packet_buffer[next_seq_no%window_size].seq_no = next_seq_no;
        packet_buffer[next_seq_no%window_size].type = NORMAL;
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        gettimeofday(&start_sample,NULL);
        //printf("Inviato pacchetto %ld e iniziato campionamento\n",packet_buffer[next_seq_no%window_size].seq_no);

        //Se il next sequence number corrisponde con la base lancia il timer
        if(base == next_seq_no){
          if(dyn_timer_enable){
            set_timer(dyn_timer_value);
            //printf("Ho lanciato il timer dinamico di %ld us\n",dyn_timer_value);
          }
          else{
            set_timer(timer);
            //printf("Ho lanciato il timer statico di %ld us\n",timer);
          } 
        }
         next_seq_no++;
      }
    }

    //Controllo se ci sono ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) > 0){ 
      if(!simulate_loss(loss_rate)){
        trial_counter=0; //Vuoldire che il client e' ancora vivo
        //printf("ACK %ld ricevuto, ricalcolo timer\n",ack.seq_no);
        base = ack.seq_no+1;
        gettimeofday(&end_sample,NULL);
        sample_RTT=((end_sample.tv_sec - start_sample.tv_sec) * 1000000) + (end_sample.tv_usec - start_sample.tv_usec);
        //printf("sample_RTT %ld\n",sample_RTT);
        estimated_RTT=(estimated_RTT*0.875)+(sample_RTT*0.125);
        //printf("estimated_RTT %ld\n",estimated_RTT);
        dev_RTT=(dev_RTT*0.75)+((abs(sample_RTT-estimated_RTT))*0.25);
        //printf("dev_RTT %ld\n",dev_RTT);
        dyn_timer_value=estimated_RTT+(dev_RTT*4);
        //printf("Il nuovo timer e' %ld\n", dyn_timer_value);
        //Stop del timer associato al pacchetto piu' vecchio della finestra 
        if(base == next_seq_no){
          set_timer(0);
          //printf("Ho fermato il timer\n");
        }
        else{
          if(dyn_timer_enable){
            set_timer(dyn_timer_value);
            //printf("Ho rilanciato il timer dinamico di %ld us\n",dyn_timer_value);
          }
          else{
            set_timer(timer);
            //printf("Ho rilanciato il timer statico di %ld us\n",timer);
          }
        }
      }
      else
        printf("PERDITA ACK SIMULATA\n");
      
    } 
  }
  trial_counter=-1;
  while(1){
    timeout_event=false;
    trial_counter++;
    if(trial_counter>=10){
      printf("Ho consegnato il pacchetto con successo ma il client e' morto o e' irraggiungibile\n");
      exit(EXIT_SUCCESS);
    }
    data.type=FIN;
    data.seq_no=next_seq_no;
    sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, sizeof(addr));
    printf("Inviato FIN\n");
    if(dyn_timer_enable){
        set_timer(dyn_timer_value);
        printf("Ho lanciato il timer dinamico finale di %ld us\n",dyn_timer_value);
      }
      else{
        set_timer(timer);
        printf("Ho lanciato il timer statico finale di %ld us\n",timer);
      }
    recvfrom(sockfd, &ack, sizeof(struct ack_packet),0, (struct sockaddr *) &addr, &addr_len);
    if(!simulate_loss(loss_rate)){
      if(ack.type==FIN){
        printf("Ho ricevuto FIN ACK\n");
        set_timer(0);
        break;
      }
   }
   else
    printf("PERDITA ACK FINALE SIMULATA\n");
  }

  closedir(d);
  printf("List terminata\n");
  exit(EXIT_SUCCESS);
}

void sig_alrm_handler(int signum){
   timeout_event=true;
   printf("SIGALRM\n"); 
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

void set_timer(long timer){
  struct itimerval timer_info;
  struct timeval timer_value;

  memset((void *)&timer_info,0,sizeof(timer_info));
  memset((void *)&timer_value,0,sizeof(timer_value));

  timer_value.tv_usec=timer;
  timer_info.it_value=timer_value;

  if (setitimer (ITIMER_REAL, &timer_info, NULL) < 0)
    perror("Errore set timer\n");
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

