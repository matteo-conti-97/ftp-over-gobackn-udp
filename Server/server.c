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

#define SYN 12
#define MAX_TRIALS_NO 10
#define DEFAULT_TIMER 50
#define MAX_CHOICE_TIME 140
//Tipi di dato
#define NORMAL 10
#define FIN 11
#define SYN 12
//Tipi di comando
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
  int sockfd, child_sockfd, serv_port, window_size, len, child_len, trial_counter=0;
  struct sockaddr_in addr, child_addr;
  pid_t pid;
  struct sigaction sa;
  struct segment_packet data;
  struct ack_packet ack;
  float loss_rate;
  double timer, synack_timer;
  clock_t timer_sample; 
  bool dyn_timer_enable=false, timer_enable=false, SYNACK_sended=false;
  long conn_req_no;

  //Pulizia
  memset((void *)&data,0,sizeof(data));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&addr, 0, sizeof(addr));
  memset((void *)&child_addr, 0, sizeof(addr));

  //Controllo numero di argomenti
  if (argc < 5) { 
    fprintf(stderr, "utilizzo: server <porta server> <dimensione finestra> <probabilita' perdita (float, -1 for 0)> <timeout (in ms double, -1 for dynamic timer)>\n");
    exit(EXIT_FAILURE);
  }

  //Controllo numero di porta
  if((serv_port=atoi(argv[1]))<1024){
    fprintf(stderr,"inserisci un numero di porta valido\n");
    exit(EXIT_FAILURE);
  }

  //Controllo dimensione finestra
  if((window_size=atoi(argv[2]))==0){
    fprintf(stderr,"inserisci dimensione finestra valida\n");
    exit(EXIT_FAILURE);
  }

  //Controllo probabilita' di perdita
  if((loss_rate=atof(argv[3]))==0){
      fprintf(stderr,"inserisci un loss rate valido\n");
      exit(EXIT_FAILURE);
  }

  if(loss_rate==-1)
    loss_rate=0;

  //Controllo timer
  if((timer=atof(argv[4]))==0){
    fprintf(stderr,"inserisci un timer valido\n");
    exit(EXIT_FAILURE);
  }

  if(timer<0){
    synack_timer=DEFAULT_TIMER;
    dyn_timer_enable=true;
  }
  else
    synack_timer=timer;

  //Seed per la perdita simulata
  srand48(2345);

  //Creazione socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
    perror("errore in socket padre");
    exit(EXIT_FAILURE);
  }
  
  //Assegnazione tipo di indirizzo
  addr.sin_family = AF_INET;
  //Assegnazione interfacce da cui accettare pacchetti (tutti)
  addr.sin_addr.s_addr = htonl(INADDR_ANY); 
  //Assegnazione porta del server
  addr.sin_port = htons(serv_port);
  len = sizeof(addr);

  //Assegna indirizzo al socket
  if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("errore in bind");
    exit(EXIT_FAILURE);
  }

  //Installazione gestore sigchild
  if (signal(SIGCHLD, sig_child_handler) == SIG_ERR) { 
    fprintf(stderr, "errore in signal");
    exit(EXIT_FAILURE);
  }

  //Installazione gestore sigalarm
  sa.sa_handler = sig_alrm_handler; 
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) < 0) {
    fprintf(stderr, "errore in sigaction");
    exit(EXIT_FAILURE);
  }

  start:
  while (1) {
    trial_counter=0;
    //Ascolto di richieste di connessione dei client
    if ((recvfrom(sockfd, &data, sizeof(data), 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom attesa client");
      exit(EXIT_FAILURE);
    }

    conn_req_no=ntohl(data.seq_no);

    //Creo il socket del figlio dedicato al client
    if ((child_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
    perror("errore in socket figlio");
    exit(EXIT_FAILURE);
    }

    //Assegnazione tipo di indirizzo figlio
    child_addr.sin_family = AF_INET;
    //Assegnazione interfacce figlio da cui accettare pacchetti (tutti)
    child_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
     //Assegnazione porta del figlio
    child_addr.sin_port = htons(0);
    child_len=sizeof(child_addr);

    //Assegna l'indirizzo al socket del figlio 
    if (bind(child_sockfd, (struct sockaddr *) &child_addr, sizeof(child_addr)) < 0) {
    perror("errore in bind socket figlio");
    exit(EXIT_FAILURE);
    }

    //Prendo il numero di porta del figlio
    if(getsockname(child_sockfd, (struct sockaddr *) &child_addr, &child_len)<0){
      perror("errore acquisizione numero porta del socket processo figlio");
      exit(EXIT_FAILURE);
    }

    sprintf(data.data,"%d", htons(child_addr.sin_port));

    while(1){ 

      //Se faccio troppi tentativi lascio stare probabilmente il server e' morto
      if(trial_counter>MAX_TRIALS_NO){
        printf("Il client e' morto oppure il canale e' molto disturbato abort\n");
        close(child_sockfd);
        goto start;
      }

      if(!SYNACK_sended){
        //Invio la nuova porta al client
        if(sendto(sockfd, &data, sizeof(data), 0, (struct sockaddr *)&addr, sizeof(addr))<0){
          perror("errore in sendto porta figlio synack");
          close(child_sockfd);
          goto start;
        }

        //Se il timer scade probabilmente e' troppo breve, lo raddoppio
        SYNACK_sended=true;

        //Start timer
        timer_sample = clock();
        timer_enable = true;
        printf("SYNACK inviato\n");
      }

      //Timeout
      if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
        timer_sample = clock();

        if(dyn_timer_enable)
          timer=timer*2;

        SYNACK_sended=false;
        trial_counter++;
        printf("Timeout SYNACK\n");
      }

      //Attendo ack syn ack
      if ((recvfrom(sockfd, &ack, sizeof(ack), MSG_DONTWAIT, (struct sockaddr *)&addr, &len)) > 0) {
        if(!simulate_loss(loss_rate)){
          //Controllo che sia la richiesta corretta
          if(ntohl(ack.seq_no)==conn_req_no){
            printf("ACKSYNACK ricevuto\n");
            break;
          }
        }
        else
          printf("PERDITA ACKSYNACK SIMULATA\n");
      }
      
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

        switch(ntohs(data.type)){
          case PUT:
            alarm(0);
            ack.type=htons(PUT);
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))<0){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            put(child_sockfd, child_addr, timer, loss_rate, data.data);
            break;
          case GET:
            alarm(0);
            ack.type=htons(GET);
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))<0){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            get(child_sockfd, child_addr, timer, window_size, loss_rate, data.data);
            break;
          case LIST:
            alarm(0);
            ack.type=htons(LIST);
            if(sendto(child_sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&child_addr, sizeof(child_addr))<0){
              perror("errore sendto ack comando");
              exit(EXIT_FAILURE);
            }
            list(child_sockfd, child_addr, timer, window_size, loss_rate);
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
    data.length=htons(strlen("Get fallita: errore interno del server"));
    strcpy(data.data,"Get fallita: errore interno del server");
    goto get_termination;
  }
  sprintf(path,"./files/%s",file_name);

  //Alloco il buffer della finestra
  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    data.length=htons(strlen("Get fallita: errore interno del server"));
    strcpy(data.data,"Get fallita: errore interno del server");
    goto get_termination;
  }

  //Pulizia
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=htonl(-1);

  //Apro il file
  if((fd=open(path,O_RDONLY))<0){
    perror("errore apertura file da inviare"); 
    data.length=htons(strlen("Get fallita: file non presente sul server"));
    strcpy(data.data,"Get fallita: file non presente sul server");
    goto get_termination;
  }

  //Calcolo dimensione file
  lseek(fd, 0, SEEK_SET);
  file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  //Invio dati
  while((ntohl(ack.seq_no)+1)*497 < file_size){

    //Se ci sono troppe ritrasmissioni lascio stare
    if(trial_counter>MAX_TRIALS_NO){
      printf("Il client e' morto oppure il canale e' molto disturbato\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((packet_buffer[next_seq_no%window_size].length = htons(read(fd, packet_buffer[next_seq_no%window_size].data, MAXLINE))) > 0){
        packet_buffer[next_seq_no%window_size].seq_no = htonl(next_seq_no);
        packet_buffer[next_seq_no%window_size].type = htons(NORMAL);
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        
        //Se e' attivato il timer dinamico campiono per calcolare l'rtt
        if((dyn_timer_enable)&&(!RTT_sample_enable)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Inviato pacchetto %d\n", ntohl(packet_buffer[next_seq_no%window_size].seq_no));

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

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
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
        printf("Pacchetto %d ritrasmesso\n", ntohl(packet_buffer[i].seq_no));  
      }
    }

    //Controllo se ci sono ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) > 0){ 
      if(!simulate_loss(loss_rate)){
        printf("ACK %d ricevuto\n", ntohl(ack.seq_no));
        base = ntohl(ack.seq_no);

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

  //Pulizia
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));

  //Termine operazione
  get_termination:
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il client e' morto
    if(trial_counter>MAX_TRIALS_NO){
      if(ntohs(data.length)>0)
        printf("Il client e' morto oppure il canale e' molto disturbato, errore: %s", data.data);
      else
        printf("Il client e' morto oppure il canale e' molto disturbato tuttavia il file e' stato consegnato con successo\n");
      break;
    }

    //Invio il FIN solo se devo farlo per la prima volta o lo rinvio in caso di timeout per non inviarne inutilmente
    if(!FIN_sended){
      data.type=htons(FIN);
      data.seq_no=htonl(next_seq_no);
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

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;
      FIN_sended=false;
      trial_counter++;
      printf("Timeout FIN\n");
    }

    //Attendo FINACK
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len)>0){
      if(!simulate_loss(loss_rate)){
        if(ntohs(ack.type)==FIN){
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
  //ack.seq_no=htonl(-1);
  
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

    //Se ci sono troppi errori di lettura lascio stare
    if(trial_counter>MAX_TRIALS_NO){
      printf("Il client e' morto oppure il canale e' molto disturbato, errore: %s", data.data);
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    if(recvfrom(sockfd, &data, sizeof(data), 0, (struct sockaddr *) &addr, &len)<0){
      perror("Pacchetto corrotto errore recv\n");
      trial_counter++;
      continue;
    }

    if(!simulate_loss(loss_rate)){
      //Se arriva un pacchetto in ordine lo riscontro e aggiorno il numero di sequenza che mi aspetto
      if(ntohl(data.seq_no)==expected_seq_no){
        if(ntohs(data.type)==FIN){

          //Se e' un FIN di errore printo l'errore, rimuovo il file sporco ed esco
          if(ntohs(data.length)>0){
            printf("%s\n", data.data);
            system(rm_string);
          }
          else
            printf("Ho ricevuto FIN\n");
          ack.type=htons(FIN);
          ack.seq_no=data.seq_no;
          break;
        }
        else{
          data.data[ntohs(data.length)]=0;
          printf("Ho ricevuto un dato di %d byte del pacchetto %d\n", ntohs(data.length), ntohl(data.seq_no));
          if((n=write(fd, data.data, ntohs(data.length)))!=ntohs(data.length)){
            perror("Non ho scritto tutto su file mi riposiziono\n");
            lseek(fd,0,SEEK_CUR-n);
            continue;
          }
          printf("Ho scritto %d byte sul file\n",n);
          ack.type=htons(NORMAL);
          ack.seq_no=data.seq_no;
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine invio l'ack con l'expected sequence number
      else{
        ack.type=htons(NORMAL);
        ack.seq_no=htonl(expected_seq_no);
      }

      //Invio ack
      sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) &addr, sizeof(addr));
      printf("ACK %d inviato\n", ntohl(ack.seq_no));

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
    data.length=htons(strlen("List fallita: errore interno del server"));
    strcpy(data.data,"List fallita: errore interno del server");
    goto list_termination;
  }

  //Pulizia
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=htonl(-1);

  //Apro la directory contente i file
  if((d = opendir("./files"))==NULL){
    perror("errore apertura directory dei file");
    data.length=htons(strlen("List fallita: errore interno del server"));
    strcpy(data.data,"List fallita: errore interno del server");
    goto list_termination;
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
  while((ntohl(ack.seq_no)+1)<num_of_files){

    //Se ci sono troppe ritrasmissioni lascio stare
    if(trial_counter>MAX_TRIALS_NO){
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
        packet_buffer[next_seq_no%window_size].length=htons(strlen(dir->d_name));
        packet_buffer[next_seq_no%window_size].seq_no = htonl(next_seq_no);
        packet_buffer[next_seq_no%window_size].type = htons(NORMAL);
        sendto(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0, (struct sockaddr *) &addr, sizeof(addr));
        
        //Se e' attivato il timer dinamico campiono per calcolare l'rtt
        if((dyn_timer_enable)&&(!RTT_sample_enable)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Inviato pacchetto %d\n", ntohl(packet_buffer[next_seq_no%window_size].seq_no));

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
      
      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
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
        printf("Pacchetto %d ritrasmesso\n", ntohl(packet_buffer[i].seq_no));  
      }
    }

    //Controllo se ci sono ack
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len) > 0){ 
      if(!simulate_loss(loss_rate)){
        printf("ACK %d ricevuto, ricalcolo timer\n", ntohl(ack.seq_no));
        base = ntohl(ack.seq_no);

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
  
  //Pulizia
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));

  //Termine operazione
  list_termination:
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il client e' morto
    if(trial_counter>MAX_TRIALS_NO){
      if(ntohs(data.length)>0)
        printf("Il client e' morto oppure il canale e' molto disturbato, errore: %s", data.data);
      else
        printf("Il client e' morto oppure il canale e' molto disturbato tuttavia la lista dei file e' stata consegnata con successo\n");
      break;
    }

    //Invio il FIN solo se devo farlo per la prima volta o lo rinvio in caso di timeout per non inviarne inutilmente
    if(!FIN_sended){
      data.type=htons(FIN);
      data.seq_no=htonl(next_seq_no);
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

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;
      FIN_sended=false;
      trial_counter++;
      printf("Timeout FIN\n");
    }

    //Attendo FINACK
    if(recvfrom(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr *) &addr, &addr_len)>0){
      if(!simulate_loss(loss_rate)){
        if(ntohs(ack.type)==FIN){
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

  act.sa_handler = func;
  sigemptyset(&act.sa_mask); 
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
    printf("Figlio %d terminato\n",pid);
  return;
}

