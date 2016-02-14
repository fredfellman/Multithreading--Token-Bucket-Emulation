#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>

#include "my402list.h"
#include "cs402.h"

#define MAXNOOFTIMESTAMPS 15

typedef struct packet{
  struct timeval tv[MAXNOOFTIMESTAMPS];
  unsigned int interarrivaltime;
  int tokensreqd;
  int packetnum;
  unsigned int servicetime;
}packet;

My402List Q1, Q2;
FILE *file;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
int token_num = 0;
int tokens = 0;
int packet_num = 0;
int filespecified = 0;
char gszProgName[MAXPATHLENGTH];
unsigned int packetinterarrivaltime = 0;
unsigned int servicetime = 0;
int noofpackets = 0;

//Default parameters values
double lambda = 0.5;  //interarrival time
double mu = 0.35;     //service time of the packet
//Note: if 1/r, 1/mu, 1/lambda is greater than 10 seconds, please use an inter-token arrival time of 10 seconds.
double r = 1.5;       //rate at which tokens are generated 
int B = 10;           //token depth
int P = 3;            //number of tokens required for each packet
int num = 20;         //number of packets 
char tsfilepath[MAXPATHLENGTH*4];

int servicingpacketno = 0;
struct timeval emulationbegintime;
struct timeval instancetime;
char buffer[1024];

unsigned int total_packetinterarrivaltime = 0;
unsigned int total_servicetime = 0;
unsigned int total_packettimeinQ1 = 0;
unsigned int total_packettimeinQ2 = 0;
unsigned int total_packettimeinS = 0;
unsigned long int total_packettimeinSystem = 0;
//unsigned long int total_squaretimeinSys = 0; 
unsigned long int total_squaretimeinSys[5000];

int tokens_dropped = 0;
int packets_dropped = 0;
int served_packets = 0;

int int_en = 0;

pthread_t arrival_threadid, server_threadid, token_threadid;
sigset_t set_interruptsignal;
struct sigaction act;

void Usage()
{
    fprintf(stderr,
            "usage: %s [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n",
            gszProgName);
    exit(-1);
}

void SetProgramName(char *s)
{
    char *c_ptr=strrchr(s, DIR_SEP);

    if (c_ptr == NULL) {
        strcpy(gszProgName, s);
    } else {
        strcpy(gszProgName, ++c_ptr);
    }
}

/* void close_file(){ */
/*   fclose(file); */
/* } */
double StandardDeviation(){
  double avg = (total_packettimeinSystem / (double)served_packets)/1000000.0;
  double add =0.0 ;
  int i = 0;
  for(i = 0; i < served_packets; i++){
    add += ((double)total_squaretimeinSys[i]/1000000.0 - avg)*((double)total_squaretimeinSys[i]/1000000.0 - avg);
  }
  double std = sqrt(add / (double)served_packets);
  return std;
}

void interrupt_service_routine(){
  fprintf(stdout, "***********************CTRL C Pressed*****************\n");
  int_en = 1;
  servicingpacketno = 0;
  noofpackets = 0;
  pthread_cancel(arrival_threadid);
  pthread_cancel(token_threadid);
  pthread_cancel(server_threadid);
}

int ParseFile(){
  char *ptr = NULL;
  char temp[1024];
  int i= 0;
  memset(buffer, 0, sizeof(buffer));
  memset(temp, 0, sizeof(temp));
  if(fgets(buffer, sizeof(buffer), file) != NULL){
    char *start_ptr = buffer;
    ptr = start_ptr;
    if(*ptr == ' ' || *ptr == '\t'){
      fprintf(stderr, "Leading space encountered in a line\n");
      exit(-1);
    }
    while(*ptr != ' ' && *ptr != '\t'){
      temp[i] = *ptr++;
      i++;
    }
    temp[i] = '\0';
    packetinterarrivaltime = (unsigned int)(atoi(temp) * 1000);
    memset(temp, 0, sizeof(temp));
    i = 0;
    while(*ptr == ' ' || *ptr == '\t')
      ptr++;
    while(*ptr != ' ' && *ptr != '\t'){
      temp[i] = *ptr++;
      i++;
    }
    temp[i] = '\0';
    P = atoi(temp);
    memset(temp, 0, sizeof(temp));
    i = 0;
    while(*ptr == ' ' || *ptr == '\t')
      ptr++;
    while(*ptr != ' ' && *ptr != '\t' && *ptr != '\n'){
      temp[i] = *ptr++;
      i++;
    }
    temp[i] = '\0';
    servicetime = (unsigned int)(atoi(temp) * 1000);
    if(*ptr != '\n'){
      fprintf(stderr, "Trailing space encountered in a line\n");
      exit(-1);
    }
  }
  //fprintf(stdout, "**********ParsedFile- Interarrivaltime = %dus, Tokens = %d, Servicetime = %dus************\n", packetinterarrivaltime, P, servicetime);
  return TRUE;
}

int ReadCommandLine(int argc, char *argv[]){
  int i = 0;
  char *end;
  for(i = 1; i<argc; i= i+2){
    //Lambda
    if(strcmp(argv[i], "-lambda") == 0){
      if(filespecified == 0){
	if(argc > i+1){
	  lambda = strtod(argv[i+1], &end);
	  if(lambda < 0.0){
	    fprintf(stderr, "Lambda value must be positive real numbers\n");
	    Usage();
	  }
	  packetinterarrivaltime = (unsigned int)((1/lambda)*1000000);
	  if(packetinterarrivaltime > 10000000){
	    packetinterarrivaltime = 10000000;
	  }
	}
	else
	  Usage();
      }
    }
    //mu
    else if(strcmp(argv[i], "-mu") == 0){
      if(filespecified == 0){
	if(argc > i+1){
	  mu = strtod(argv[i+1], &end);
	  if(mu < 0.0){
	    fprintf(stderr, "mu value must be positive real numbers\n");
	    Usage();
	  }
	  servicetime = (unsigned int)((1/mu) * 1000000);
	  if(servicetime > 10000000){
	    servicetime = 10000000;
	  }
	}
	else
	  Usage();
      }
    }
    //r
    else if(strcmp(argv[i], "-r") == 0){
      if(argc > i+1)
	r = strtod(argv[i+1], &end);
      else
	Usage();
      if(r < 0.0){
	fprintf(stderr, "r value must be positive real numbers\n");
	Usage();
      }
    }
    //B
    else if(strcmp(argv[i], "-B") == 0){
      if(argc > i+1)
	B = atoi(argv[i+1]);
      else
	Usage();
      if(B < 0 || B > 2147483647){
	fprintf(stderr, "B value must be positive integers with a maximum value of 2147483647\n");
	Usage();
      }
    }
    //P
    else if(strcmp(argv[i], "-P") == 0){
      if(filespecified == 0){
	if(argc > i+1)
	  P = atoi(argv[i+1]);
	else
	  Usage();
	if(P < 0 || P > 2147483647){
	  fprintf(stderr, "P value must be positive integers with a maximum value of 2147483647\n");
	  Usage();
	}
      }
    }
    //n
    else if(strcmp(argv[i], "-n") == 0){
      if(filespecified ==  0){
	if(argc > i+1)
	  num = atoi(argv[i+1]);
	else
	  Usage();
	if(num < 0 || num > 2147483647){
	  fprintf(stderr, "n value must be positive integers with a maximum value of 2147483647\n");
	  Usage();
	}
      }
    }
    //tsfile
    else if(strcmp(argv[i], "-t") == 0){
      if(argc > i+1){
	memset(tsfilepath, 0, sizeof(tsfilepath));
	strcpy(tsfilepath, argv[i+1]);
	//Parse tsfile
	struct stat isdir;
	if(!(file = fopen(argv[i+1], "r"))){
	  fprintf(stderr, "%s\n", strerror(errno));
	  exit(0);
	}
	else{
	  if((stat(argv[i+1], &isdir) == 0) && S_ISDIR(isdir.st_mode)){
	    fprintf(stderr, "Is a directory - %s\n", argv[i+1]);
	    exit(0);
	  }
	}
	//	pthread_cleanup_push(close_file);
	filespecified = 1;
	if(fgets(buffer, sizeof(buffer), file) != NULL){
	  num = atoi(buffer);
	  if(ParseFile() == FALSE){
	    fprintf(stderr, "Error occured while parsing file\n");
	    exit(0);
	  }
	}
      }
      else
	Usage();
    }
    else 
      Usage();
  }
  if(filespecified == 0){
    fprintf(stdout, "Emulation Parameters:\n");
    fprintf(stdout, "\tnumber to arrive = %d\n", num);
    fprintf(stdout, "\tlambda = %.6g\n", lambda);
    fprintf(stdout, "\tmu = %.6g\n", mu);
    fprintf(stdout, "\tr = %.6g\n", r);
    fprintf(stdout, "\tB = %d\n", B);
    fprintf(stdout, "\tP = %d\n\n", P);
    //    noofpackets = num;
  }
  else if(filespecified == 1){
    fprintf(stdout, "Emulation Parameters:\n");
    fprintf(stdout, "\tnumber to arrive = %d\n", num);
    fprintf(stdout, "\tr = %.6g\n", r);
    fprintf(stdout, "\tB = %d\n", B);
    fprintf(stdout, "\ttsfile = %s\n\n", tsfilepath);
    //    noofpackets = num-1;
  }
  servicingpacketno = noofpackets = num;
  if(int_en == 1){
    servicingpacketno = noofpackets = 0;
    pthread_exit((void*)0);
  }
  return 0;
}

unsigned int time_difference(struct timeval *begin, struct timeval *end){
  unsigned int time_diff = (end->tv_sec - begin->tv_sec)*1000000 + (end->tv_usec - begin->tv_usec);
  return time_diff;
}

unsigned int time_addition(struct timeval *begin, struct timeval *end){
  unsigned int time_add = (end->tv_sec + begin->tv_sec)*1000000 + (end->tv_usec + begin->tv_usec);
  return time_add;
}

void transpacket_to_Q2(packet *obj){
  My402ListUnlink(&Q1, My402ListFirst(&Q1));
  gettimeofday(&(obj->tv[2]), NULL);
  fprintf(stdout, "%012.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token\n", (double)(time_difference(&emulationbegintime, &(obj->tv[2])))/1000.0,obj->packetnum, (double)(time_difference(&(obj->tv[1]),&(obj->tv[2])))/1000.0, tokens);
  (void)My402ListAppend(&Q2, obj);
  gettimeofday(&(obj->tv[3]), NULL);
  fprintf(stdout, "%012.3fms: p%d enters Q2\n", (double)(time_difference(&emulationbegintime, &(obj->tv[3])))/1000.0,obj->packetnum);
  //fprintf(stdout, "###########################Q2 has %d packets######################\n", My402ListLength(&Q2));
}


void *arrival(void *arg){
  //fprintf(stdout, "I am in arrival thread\n");
  //fprintf(stdout, "-------------Number of packets: %d---------------\n", noofpackets);
  struct timeval present, prev_interarrivaltime;
  memset(&present, 0, sizeof(present));
  unsigned int actual_packetintarrtime = 0;
  prev_interarrivaltime = emulationbegintime;

  act.sa_handler = interrupt_service_routine;
  sigaction(SIGINT, &act, NULL);

  while(noofpackets != 0){
    gettimeofday(&present, NULL);
    unsigned int t = time_difference(&emulationbegintime, &present);
    actual_packetintarrtime += packetinterarrivaltime;
    if(t < actual_packetintarrtime)
      usleep(actual_packetintarrtime - t);
    if(int_en == 1){
      pthread_exit((void*)0);
    }
    packet *obj = NULL;
    obj = (packet*)malloc(sizeof(packet));
    if(obj == NULL){
      fprintf(stderr, "%s\n", strerror(errno));
    }
    else{
      packet_num++;
    }
    pthread_sigmask(SIG_UNBLOCK, &set_interruptsignal, NULL);
    gettimeofday(&(obj->tv[0]), NULL);
    obj->interarrivaltime = time_difference(&prev_interarrivaltime, &(obj->tv[0]));
    obj->tokensreqd = P;
    obj->servicetime = servicetime;
    obj->packetnum = packet_num;
    //fprintf(stdout, "**********************Packet number %d labelled************************\n", obj->packetnum);
    total_packetinterarrivaltime += obj->interarrivaltime; //in microseconds
    if(obj->tokensreqd <= B){
      fprintf(stdout, "%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n", ((double)time_difference(&emulationbegintime, &(obj->tv[0])))/1000.0, obj->packetnum , obj->tokensreqd, ((double)(obj->interarrivaltime))/1000.0);
    prev_interarrivaltime = obj->tv[0];
    }
    else if(obj->tokensreqd > B){
      //My402ListUnlink(&Q1, My402ListLast(&Q1));
      fprintf(stdout, "%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms, dropped\n", ((double)time_difference(&emulationbegintime, &(obj->tv[0])))/1000.0, packet_num, obj->tokensreqd, ((double)(obj->interarrivaltime))/1000.0);
      free(obj);
      servicingpacketno--;
      packets_dropped++;
      if(servicingpacketno == 0)
	  pthread_cond_broadcast(&cv);
      goto nextpacket;
    }
    if(pthread_mutex_lock(&mutex) != 0){
      fprintf(stderr, "%s\n", strerror(errno));
      return(0);
    }
    /*Critical Section*/
    (void)My402ListAppend(&Q1, obj);
    //fprintf(stdout, "*-*-*-*-*-*-*-*-*-*-*-*Q1 has %d packets-*-*-*-*-*-**-*-*-*-*-*-*-*-**\n", My402ListLength(&Q1));
    gettimeofday(&(obj->tv[1]), NULL);
    fprintf(stdout, "%012.3fms: p%d enters Q1\n", ((double)time_difference(&emulationbegintime, &(obj->tv[1])))/1000.0,obj->packetnum);

    My402ListElem *tmp_elm = NULL;
    for(tmp_elm = My402ListFirst(&Q1); tmp_elm != NULL; tmp_elm = My402ListNext(&Q1, tmp_elm)){
      packet *tmp_obj = ((packet*)(tmp_elm->obj));
      if(tmp_obj->tokensreqd <= tokens){
	tokens = tokens - tmp_obj->tokensreqd;
	transpacket_to_Q2(tmp_obj);
	if(My402ListLength(&Q2) == 1){
	  pthread_cond_broadcast(&cv);
	}
      }
      else 
	goto jump;    
    }
  jump:/*ends*/
    if(pthread_mutex_unlock(&mutex) != 0){
      fprintf(stderr, "%s\n", strerror(errno));
      return(0);
    }
  nextpacket:
    noofpackets--;
    if(filespecified == 1 && noofpackets != 0){
      if(ParseFile() == FALSE){
	fprintf(stderr, "Error occured while parsing file\n");
     exit(0);
      }
    }
  }
  //fprintf(stdout, "----------------------End of arrival thread - Good Bye---------------------\n");
  return 0;
}

void *token_depositing(void *arg){
  // fprintf(stdout, "I am in token_depositing thread\n");
  double token_interarrivaltime = 1.0/r;
  if(token_interarrivaltime > 10.0){
    token_interarrivaltime = 10.0;
  }
  unsigned int i = 1;
  unsigned int usecs = (unsigned int)(token_interarrivaltime * 1000000);
  struct timeval present;
  memset(&present, 0, sizeof(present));
  //end = emulationbegintime;
  while(servicingpacketno != 0){// Q1 has to be empty and arrival thread is killed then stop token thread
    //generate_token:
    gettimeofday(&present, NULL);
    unsigned int t = time_difference(&emulationbegintime, &present);
    if(t < i*usecs)
      usleep(i*usecs - t);
    //end = begin;
    i++;
    if(int_en == 1){
      pthread_exit((void*)0);
    }
    if(pthread_mutex_lock(&mutex) != 0){
      fprintf(stderr, "%s\n", strerror(errno));
      return(0);
    }
    /*Critical Section*/
    token_num++;
    gettimeofday(&instancetime, NULL);
    if(tokens < B){
      tokens++;
      fprintf(stdout, "%012.3fms: token t%d arrives, token bucket now has %d token\n", ((double)time_difference(&emulationbegintime, &instancetime))/1000.0,token_num, tokens);
    }
    else{
      fprintf(stdout, "%012.3fms: token t%d arrives, dropped\n", ((double)time_difference(&emulationbegintime, &instancetime))/1000.0, token_num); //if dont put \n then it doesnt print the statement (Reason ?)
      tokens_dropped++;
    }
    if(My402ListEmpty(&Q1) == FALSE){
      My402ListElem *pckt = My402ListFirst(&Q1);
      if(((packet*)(pckt->obj))->tokensreqd <= tokens){
	tokens = tokens - ((packet*)(pckt->obj))->tokensreqd;
	transpacket_to_Q2((packet*)(pckt->obj));
	if(My402ListLength(&Q2) == 1){
	  pthread_cond_broadcast(&cv);
	}
      }
    }
    /*ends*/
    if(pthread_mutex_unlock(&mutex) != 0){
      fprintf(stderr, "%s\n", strerror(errno));
      return(0);
    }
  }
  //goto generate_token;
  //fprintf(stdout, "----------------------End of token thread - Good Bye---------------------\n");
  return 0;
}

void *server(void *arg){
  int empty = 0;
  unsigned int trans_time = 0;
  My402ListElem *pckt = NULL;
  packet *obj = NULL;
  while(servicingpacketno != 0){
    // servicenextpacket:
    //fprintf(stdout, "I am in server thread\n");
    if(pthread_mutex_lock(&mutex) != 0){
      fprintf(stderr, "%s\n", strerror(errno));
      return(0);
    }
    /*Critical Section*/
    if(My402ListEmpty(&Q2) == TRUE){
      //empty = 0;
      pthread_cond_wait(&cv, &mutex);
    }
    empty = 0;
    if(servicingpacketno !=0){
      pckt = My402ListFirst(&Q2);
      obj = (packet*)(pckt->obj);
      trans_time = ((packet*)(pckt->obj))->servicetime;
      My402ListUnlink(&Q2, pckt);
      gettimeofday(&(obj->tv[4]), NULL);
      fprintf(stdout, "%012.3fms: p%d leaves Q2, time in Q2 = %.3fms, begin service at S\n",((double)time_difference(&emulationbegintime, &(obj->tv[4])))/1000.0,obj-> packetnum, ((double)time_difference(&(obj->tv[3]), &(obj->tv[4])))/1000.0);
      empty = 1;
    }
    /*ends*/
    if(pthread_mutex_unlock(&mutex) != 0){
      fprintf(stderr, "%s\n", strerror(errno));
      return(0);
    }
    if(empty == 1){
      gettimeofday(&(obj->tv[5]), NULL);
      usleep(trans_time);
      gettimeofday(&(obj->tv[6]), NULL);
      fprintf(stdout, "%012.3fms: p%d departs from S, service time = %.3fms, time in system = %.3fms\n", ((double)time_difference(&emulationbegintime, &(obj->tv[6])))/1000.0, obj->packetnum, ((double)time_difference(&(obj->tv[4]), &(obj->tv[6])))/1000.0 , ((double)time_difference(&(obj->tv[1]), &(obj->tv[6])))/1000.0);
      //fprintf(stdout, "-----------------Serviced packet number %d---------------\n", obj->packetnum);
      total_servicetime += time_difference(&(obj->tv[4]),&(obj->tv[6]));
      total_packettimeinQ1 += time_difference(&(obj->tv[1]), &(obj->tv[2]));
      total_packettimeinQ2 += time_difference(&(obj->tv[3]), &(obj->tv[4]));
      total_packettimeinS += time_difference(&(obj->tv[4]), &(obj->tv[6]));
      total_packettimeinSystem += time_difference(&(obj->tv[1]), &(obj->tv[6]));
      total_squaretimeinSys[served_packets] = time_difference(&(obj->tv[1]), &(obj->tv[6]));
      //total_squaretimeinSys = time_difference(&(obj->tv[1]), &(obj->tv[6]))*time_difference(&(obj->tv[1]), &(obj->tv[6]));
      served_packets++;
      free(obj);
      servicingpacketno--;
      if(int_en == 1){
	servicingpacketno = 0;
      }
    }
    //else
    //wait for the queue-not-empty condition to be signaled
  }
  //goto servicenextpacket;
  //fprintf(stdout, "----------------------End of server thread - Good Bye---------------------\n");
  return 0;
}

int main(int argc, char *argv[]){
  /* if(ReadFile(argc, argv) == 0){ */
  /*   exit(0); */
  /* } */

  SetProgramName(*argv);
  //Usage();
  void *result;
  memset(&Q1, 0, sizeof(Q1));
  (void)My402ListInit(&Q1);
  memset(&Q2, 0, sizeof(Q2));
  (void)My402ListInit(&Q2);


  packetinterarrivaltime = (unsigned int)((1/lambda)*1000000);
  if(packetinterarrivaltime > 10000000){
    packetinterarrivaltime = 10000000;
  }
  servicetime = (unsigned int)((1/mu) * 1000000);
  if(servicetime > 10000000){
    servicetime = 10000000;
  }
  
  (void)ReadCommandLine(argc, argv);
  memset(&total_squaretimeinSys, 0, sizeof(total_squaretimeinSys));
  //fprintf(stdout, "Emulation Parameters:\n\tnumber to arrive = %d\n\tlambda = %3.2f\n\tmu = %3.2f\n\tr = %3.2f\n\tB = %d\n\tP = %d\n\ttsfile =%s \n\n",num, lambda, mu, r, B, P, tsfilepath);
  //  fprintf(stdout, "Emulation Parameters\n\tnumber to arrive\n = ");

  gettimeofday(&emulationbegintime, NULL);
  fprintf(stdout, "%012.3fms: emulation begins\n", (double)(time_difference(&(emulationbegintime), &(emulationbegintime)))/1000.0);

  sigemptyset(&set_interruptsignal);
  sigaddset(&set_interruptsignal, SIGINT);
  pthread_sigmask(SIG_BLOCK, &set_interruptsignal, NULL);

  if(pthread_create(&arrival_threadid, 0, arrival, (void*)2) != 0){
   fprintf(stderr, "Arrival thread not created successfully - %s\n", strerror(errno));
  }
  if(pthread_create(&token_threadid, 0, token_depositing, (void*)2) != 0){
    fprintf(stderr, "Token Depositing thread not created successfully - %s\n", strerror(errno));
  }
  if(pthread_create(&server_threadid, 0, server, (void*)2) != 0){
   fprintf(stderr, "Server thread not created successfully - %s\n", strerror(errno));
  }
  pthread_join(server_threadid, (void**)&result);
  if(result == 0)
    goto terminate_immediately;
  pthread_join(arrival_threadid, NULL);
  pthread_join(token_threadid, NULL);
 terminate_immediately:

  if(int_en == 1){
    My402ListElem *elm = NULL;
    for(elm = My402ListFirst(&Q2); elm != NULL; elm = My402ListNext(&Q2, elm)){
      fprintf(stdout, "p%d removed from Q2\n", ((packet*)(elm->obj))->packetnum);
      free(((packet*)(elm->obj)));
      My402ListUnlink(&Q1, elm);
    }
    for(elm = My402ListFirst(&Q1); elm != NULL; elm = My402ListNext(&Q1, elm)){
      fprintf(stdout, "p%d removed from Q1\n", ((packet*)(elm->obj))->packetnum);
      free(((packet*)(elm->obj)));
      My402ListUnlink(&Q2, elm);
    }
  }


  My402ListUnlinkAll(&Q1);
  My402ListUnlinkAll(&Q2);

  unsigned int emulationendtime = 0;
  gettimeofday(&instancetime, NULL);
  emulationendtime = time_difference(&emulationbegintime, &instancetime);
  fprintf(stdout, "%012.3fms: emulation ends\n\n", ((double)(emulationendtime))/1000.0);
  fprintf(stdout, "Statistics:\n\n");
  if(packet_num == 0){
    total_packetinterarrivaltime = 0;
    packet_num = 1;
  }
  fprintf(stdout, "\taverage packet inter-arrival time = %.6gs\n", (total_packetinterarrivaltime / (double)packet_num)/1000000.0);
  if(served_packets == 0){
    total_servicetime = 0;
    served_packets = 1;
  }
  fprintf(stdout, "\taverage packet service time = %.6gs\n\n", (total_servicetime / (double)served_packets)/1000000.0);

  if(emulationendtime == 0){
    total_packettimeinQ1 = total_packettimeinQ2 = total_packettimeinS = 0;
    emulationendtime = 1;
  }
  fprintf(stdout, "\taverage number of packets in Q1 = %.6g\n", total_packettimeinQ1 / (double)emulationendtime);
  fprintf(stdout, "\taverage number of packets in Q2 = %.6g\n", total_packettimeinQ2 / (double)emulationendtime);
  fprintf(stdout, "\taverage number of packets at S = %.6g\n\n", total_packettimeinS / (double)emulationendtime);

  fprintf(stdout, "\taverage time a packet spent in system = %.6gs\n", (total_packettimeinSystem / (double)served_packets)/1000000.0);
  //StandardDeviation();
  fprintf(stdout, "\tstandard deviation for time spent in system = %.6g\n\n", StandardDeviation() /* sqrt((((total_squaretimeinSys*(double)served_packets)-(total_packettimeinSystem * total_packettimeinSystem/1000000.0))/ ((double)served_packets*(double)served_packets))/1000000.0) */);

  if(token_num == 0){
    tokens_dropped = 0;
    token_num = 1;
  }
  fprintf(stdout, "\ttoken drop probability = %.6g\n", tokens_dropped/ (double)token_num);
  fprintf(stdout, "\tpacket drop probability = %.6g\n\n", packets_dropped/(double)packet_num);
  //pthread_cleanup_pop(0);
  if(filespecified == 1)
    fclose(file);
  return 0;
}
