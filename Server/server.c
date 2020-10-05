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
#include <math.h>

#define DEFAULT_TIMER 500
#define MAX_CHOICE_TIME 120
#define NORMAL 10
#define FIN 11
#define MAXLINE 497
#define PUT 1
#define GET 2
#define LIST 3

struct segment_packet {
    int type;
    long seq_no;
    int length;
    char data[MAXLINE];
};

struct ack_packet {
    int type;
    long seq_no;
};

bool simulate_loss(float loss_rate);

typedef void Sigfunc(int); 

Sigfunc* signal(int signum, Sigfunc *handler);

void sig_child_handler(int signum);

void sig_alrm_handler(int signum);

void get(int sockfd, struct sockaddr_in addr, double timer, int window_size, float loss_rate, char *file_name);

void put(int sockfd, struct sockaddr_in addr, double timer, float loss_rate, char *file_name);

void list(int sockfd, struct sockaddr_in addr, double timer, int window_size, float loss_rate);


int main(int argc, char *argv[]){
  int sockfd, child_sockfd, serv_port, window_size, len, child_len;
  struct sockaddr_in addr, child_addr;
  pid_t pid;
  struct sigaction sa;
  struct segment_packet data;
  struct ack_packet ack;
  float loss_rate;
  double timer;

  memset((void *)&data,0,sizeof(data));
  memset((void *)&ack,0,sizeof(ack));

  if (argc < 5) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: server <porta server> <dimensione finestra> <probabilita' perdita (float, -1 for 0)> <timeout (in ms double, -1 for dynamic timer)>\n");
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

  if((timer=atof(argv[4]))==0){
    fprintf(stderr,"inserisci un timer valido\n");
    exit(EXIT_FAILURE);
  }

  //random seed per la perdita simulata
  srand48(2345);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket padre");
    exit(EXIT_FAILURE);
  }
  
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
    //Ascolto di richieste di connessione dei client
    if ((recvfrom(sockfd, &data, sizeof(data), 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom attesa client");
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
      //Attendo ack syn ack
      if ((recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&addr, &len)) < 0) {
        perror("errore in recvfrom ach_syn_ack");
        close(child_sockfd);
        goto start;
      }
      //Controllo che sia la richiesta corretta
      if(ack.seq_no==data.seq_no)
        break;
    }

    //Fork del figlio
    if ((pid = fork()) == 0){
      printf("Sono nel figlio\n");
      while(1){
        alarm(MAX_CHOICE_TIME);
        if ((recvfrom(child_sockfd, &data, sizeof(data), 0, (struct sockaddr *)&child_addr, &child_len)) < 0) {
          perror("errore in recvfrom comando");
          close(child_sockfd);
          exit(EXIT_FAILURE);
        }

        switch(data.type){
          case PUT:
            alarm(0);
            //printf("Ho ricevuto il comando put %d\n",command);
            //put(child_sockfd, child_addr);
            ack.type=PUT;
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))!=sizeof(ack)){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            put(child_sockfd, child_addr, timer, loss_rate, data.data);
            break;
          case GET:
            alarm(0);
            //printf("Ho ricevuto il comando get %d\n",data.type);
            //printf("Il nome del file scelto e' %s", data.data);
            ack.type=GET;
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))!=sizeof(ack)){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            get(child_sockfd, child_addr, timer, window_size, loss_rate, data.data);
            break;
          case LIST:
            alarm(0);
            ack.type=LIST;
            printf("Ho ricevuto il comando list %d\n",data.type);
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))!=sizeof(ack)){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            list(child_sockfd, child_addr, timer, window_size, loss_rate);
            //list(child_sockfd, child_addr);
            break;
          default:
            break;
        }
      }
    }
  }
 
   exit(EXIT_SUCCESS);
}

void get(int sockfd, struct sockaddr_in addr, double timer, int window_size, float loss_rate, char *file_name){
  int fd, trial_counter=0, addr_len=sizeof(addr);
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  long base=0,next_seq_no=0, file_size;
  char *path;
  clock_t start_sample_RTT, timer_sample; 
  double sample_RTT=0, estimated_RTT=0, dev_RTT=0;
  bool dyn_timer_enable=false, timer_enable=false, RTT_sample_enable=false, FIN_sended=false;

  //Attivo timer dinamico
  if(timer<0){
    dyn_timer_enable=true;
    timer=DEFAULT_TIMER;
  }

  //Costruisco la stringa path del file da scaricare 
  if((path=malloc(strlen(file_name)))==NULL){
    perror("malloc fallita");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  sprintf(path,"./files/%s",file_name);

  //Alloco il buffer della finestra
  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    exit(EXIT_FAILURE);
  }

  //Pulizia
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  //Apro il file
  if((fd=open(path,O_RDONLY))<0){
    perror("errore apertura file da inviare"); 
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  //Calcolo dimensione file
  lseek(fd, 0, SEEK_SET);
  file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  //Invio dati
  while((ack.seq_no+1)*497 < file_size){

    //Se ci sono troppe ritrasmissioni lascio stare
    if(trial_counter>10){
      printf("Il client e' morto oppure il canale e' molto disturbato\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((packet_buffer[next_seq_no%window_size].length = read(fd, packet_buffer[next_seq_no%window_size].data, MAXLINE)) > 0){
        packet_buffer[next_seq_no%window_size].seq_no = next_seq_no;
        packet_buffer[next_seq_no%window_size].type = NORMAL;
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        
        //Se e' attivato il timer dinamico campiono per calcolare l'rtt
        if((dyn_timer_enable)&&(!RTT_sample_enable)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Inviato pacchetto %ld\n",packet_buffer[next_seq_no%window_size].seq_no);

        //Se il next sequence number corrisponde con la base lancia il timer
        if(base == next_seq_no){
          timer_sample = clock();
          timer_enable = true;
        }

        next_seq_no++;
      }
    }

    //Scadenza del timer ritrasmetto tutto quello che era presente nella finestra
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      if(dyn_timer_enable)
        timer=timer*2; 
      trial_counter++;
      timer_sample = clock();
      for(int i=0; i<window_size; i++){
        sendto(sockfd, &packet_buffer[i], sizeof(packet_buffer[i]), 0, (struct sockaddr *) &addr, sizeof(addr));
        if((dyn_timer_enable)&&(RTT_sample_enable==0)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Pacchetto %ld ritrasmesso\n",packet_buffer[i].seq_no);  
      }
    }

    //Controllo se ci sono ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) > 0){ 
      if(!simulate_loss(loss_rate)){
        printf("ACK %ld ricevuto\n",ack.seq_no);
        base = ack.seq_no;

        //Azzero il contatore di tentativi di ritrasmissione in quanto se ricevo ACK il client e' vivo
        trial_counter=0;

        //Aggiorno timer dinamico
        if((dyn_timer_enable)&&(RTT_sample_enable==1)){
          RTT_sample_enable = false;
          sample_RTT=(double)(clock()-start_sample_RTT)*1000/CLOCKS_PER_SEC;
          printf("SAMPLE RTT %f\n", sample_RTT);
          estimated_RTT=(double)(0.875*estimated_RTT)+(0.125*sample_RTT);
          printf("ESTIMATED RTT %f\n", estimated_RTT);
          dev_RTT=(double)(0.75*dev_RTT)+(0.25*fabs(sample_RTT-estimated_RTT));
          printf("DEV RTT %f\n", dev_RTT);
          timer=(double)estimated_RTT+4*dev_RTT;
          printf("Nuovo timer %f\n",timer);
          //timer = (double)(clock()-sample_RTT)*1000/CLOCKS_PER_SEC; 
        } 
        //Stop del timer associato al pacchetto piu' vecchio della finestra 
        if(base == next_seq_no){
          timer_enable = false;
          printf("Ho fermato il timer\n");
        }
      }
      else
        printf("PERDITA ACK SIMULATA\n"); 
    }            
  }

  //Termine operazione
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il client e' morto
    if(trial_counter>10){
      printf("Il client e' morto oppure il canale e' molto disturbato tuttavia il file e' stato consegnato con successo\n");
      break;
    }

    //Invio il FIN solo se devo farlo per la prima volta o lo rinvio in caso di timeout per non inviarne inutilmente
    if(!FIN_sended){
      data.type=FIN;
      data.seq_no=next_seq_no;
      sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, sizeof(addr));
      FIN_sended=true;
      //Start timer
      timer_sample = clock();
      timer_enable = true;
      printf("Inviato FIN\n");
    } 

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      timer_sample = clock();

      //Non avendo modo di poter aggiustare il timer, se ci sono perdite probabilmente e' troppo breve e lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;
      FIN_sended=false;
      trial_counter++;
      printf("Timeout FIN\n");
    }

    //Attendo FINACK
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len)>0){
      if(!simulate_loss(loss_rate)){
        if(ack.type==FIN){
          printf("Ho ricevuto FIN ACK\n");
          break;
        }
      }
      else
        printf("PERDITA ACK FINALE SIMULATA\n");
    }
  }
  close(fd);
  close(sockfd);
  printf("Get terminata\n");
  exit(EXIT_SUCCESS);
}

void put(int sockfd, struct sockaddr_in addr, double timer, float loss_rate, char *file_name){
  int n, fd, trial_counter=0, len=sizeof(addr);
  struct segment_packet data;
  struct ack_packet ack;
  long expected_seq_no=0;
  char *rm_string;
  char *path;

  if((path=malloc(strlen(file_name)))==NULL){
    perror("malloc fallita");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  sprintf(path,"./files/%s",file_name);

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  
  //Utile solo per la pulizia della directory in caso di errori
  rm_string=malloc(strlen(file_name)+3);
  sprintf(rm_string,"rm %s",path);

   //apro file
  if((fd = open(path, O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  //Ricevo dati
  while(1){

    if(recvfrom(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, &len)!=sizeof(data)){
      perror("Pacchetto corrotto errore recv\n");
      continue;
    }

    if(!simulate_loss(loss_rate)){
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
          printf("Ho ricevuto un dato di %d byte del pacchetto %ld\n", data.length, data.seq_no);
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
      sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) &addr, sizeof(addr));
      printf("ACK %ld inviato\n",ack.seq_no);

   }
    else
      printf("PERDITA PACCHETTO SIMULATA\n");
  } 

  //Invio ack finale
  sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) &addr, sizeof(addr));
  printf("FIN ACK inviato\n");
  close(fd);
  close(sockfd);
  printf("\nPUT terminata\n\n");
  exit(EXIT_SUCCESS);
}

void list(int sockfd, struct sockaddr_in addr, double timer, int window_size, float loss_rate){
  DIR *d;
  struct dirent *dir;
  off_t head, cur;
  long num_of_files=0, base=0,next_seq_no=0;
  int trial_counter=0, addr_len=sizeof(addr);
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  clock_t start_sample_RTT, timer_sample; 
  double sample_RTT=0, estimated_RTT=0, dev_RTT=0;
  bool dyn_timer_enable=false, timer_enable=false, RTT_sample_enable=false, FIN_sended=false;

  //Attivo timer dinamico
  if(timer<0){
    dyn_timer_enable=true;
    timer=DEFAULT_TIMER;
  }

  //Alloco il buffer della finestra finestra
  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    exit(EXIT_FAILURE);
  }

  //Pulizia
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  //Apro la directory contente i file
  if((d = opendir("./files"))==NULL){
    perror("errore apertura directory dei file");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  //Tengo traccia della testa della directory
  head=telldir(d);

  //Conto quanti pacchetti dovro' inviare
  while ((dir = readdir(d)) != NULL){
    if((strcmp(dir->d_name,".")==0)||(strcmp(dir->d_name,"..")==0)) 
      continue;
    num_of_files++;
  }
  printf("numero di file %ld\n",num_of_files);

  //Mi riposiziono
  seekdir(d,head);

  //Invio la lista dei file
  while((ack.seq_no+1)<num_of_files){

    //Se ci sono troppe ritrasmissioni lascio stare
    if(trial_counter>10){
      printf("Il client e' morto oppure il canale e' molto disturbato\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((dir = readdir(d)) != NULL){
        if((strcmp(dir->d_name,".")==0)||(strcmp(dir->d_name,"..")==0)) 
          continue;
        strcpy(packet_buffer[next_seq_no%window_size].data, dir->d_name);
        packet_buffer[next_seq_no%window_size].length=strlen(dir->d_name);
        packet_buffer[next_seq_no%window_size].seq_no = next_seq_no;
        packet_buffer[next_seq_no%window_size].type = NORMAL;
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        
        //Se e' attivato il timer dinamico campiono per calcolare l'rtt
        if((dyn_timer_enable)&&(!RTT_sample_enable)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Inviato pacchetto %ld\n",packet_buffer[next_seq_no%window_size].seq_no);

        //Se il next sequence number corrisponde con la base lancia il timer
        if(base == next_seq_no){
          timer_sample = clock();
          timer_enable = true;
        }
         next_seq_no++;
      }
    }

    //Scadenza del timer ritrasmetto tutto quello che era presente nella finestra
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){
      if(dyn_timer_enable)
        timer=timer*2;
      trial_counter++;
      timer_sample = clock();
      for(int i=0; i<window_size; i++){
        sendto(sockfd, &packet_buffer[i], sizeof(packet_buffer[i]), 0, (struct sockaddr *) &addr, sizeof(addr));
        if((dyn_timer_enable)&&(RTT_sample_enable==0)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Pacchetto %ld ritrasmesso\n",packet_buffer[i].seq_no);  
      }
    }

    //Controllo se ci sono ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) ==sizeof(ack)){ 
      if(!simulate_loss(loss_rate)){
        printf("ACK %ld ricevuto, ricalcolo timer\n",ack.seq_no);
        base = ack.seq_no;

        //Azzero il contatore di tentativi di ritrasmissione in quanto se ricevo ACK il client e' vivo
        trial_counter=0;

        //Aggiorno timer dinamico
        if((dyn_timer_enable)&&(RTT_sample_enable==1)){
          RTT_sample_enable = false;
          sample_RTT=(double)(clock()-start_sample_RTT)*1000/CLOCKS_PER_SEC;
          //printf("SAMPLE RTT %f\n", sample_RTT);
          estimated_RTT=(double)(0.875*estimated_RTT)+(0.125*sample_RTT);
          //printf("ESTIMATED RTT %f\n", estimated_RTT);
          dev_RTT=(double)(0.75*dev_RTT)+(0.25*fabs(sample_RTT-estimated_RTT));
          //printf("DEV RTT %f\n", dev_RTT);
          timer=(double)estimated_RTT+4*dev_RTT;
          //printf("Nuovo timer %f\n",timer);
          //timer = (double)(clock()-sample_RTT)*1000/CLOCKS_PER_SEC; 
        } 
        //Stop del timer associato al pacchetto piu' vecchio della finestra 
        if(base == next_seq_no){
          timer_enable = false;
          printf("Ho fermato il timer\n");
        }
      }
      else
        printf("PERDITA ACK SIMULATA\n");     
    } 
  }
  
  //Termine operazione
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il client e' morto
    if(trial_counter>10){
      printf("Il client e' morto oppure il canale e' molto disturbato tuttavia la lista dei file e' stata consegnata con successo\n");
      break;
    }

    //Invio il FIN solo se devo farlo per la prima volta o lo rinvio in caso di timeout per non inviarne inutilmente
    if(!FIN_sended){
      data.type=FIN;
      data.seq_no=next_seq_no;
      sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, sizeof(addr));
      FIN_sended=true;
      //Start timer
      timer_sample = clock();
      timer_enable = true;
      printf("Inviato FIN\n");
    } 

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      timer_sample = clock();

      //Non avendo modo di poter aggiustare il timer, se ci sono perdite probabilmente e' troppo breve e lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;
      FIN_sended=false;
      trial_counter++;
      printf("Timeout FIN\n");
    }

    //Attendo FINACK
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len)>0){
      if(!simulate_loss(loss_rate)){
        if(ack.type==FIN){
          printf("Ho ricevuto FIN ACK\n");
          break;
        }
      }
      else
        printf("PERDITA ACK FINALE SIMULATA\n");
    }
  }

  closedir(d);
  close(sockfd);
  printf("List terminata\n");
  exit(EXIT_SUCCESS);
}

void sig_alrm_handler(int signum){
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

