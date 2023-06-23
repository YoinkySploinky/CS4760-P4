#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <fstream>

#define BUFF_SZ sizeof ( int ) * 10
const int SHMKEY1 = 4201069;
const int SHMKEY2 = 4201070;
const int PERMS = 0644;
const int maxTimeBetweenNewProcsNS = 500000;
const int maxTimeBetweenNewProcsSecs = 0;
const int quantum = 10000;
using namespace std;

int shmid1;
int shmid2;
int msqid;
int *nano;
int *sec;

struct PCB{

  int occupied; // either true or false
  pid_t pid; // process id of this child
  int timeOn; // total time this process has been on a cpu
};

//message buffer
struct msgbuffer {
   long mtype; //needed
   int quantum; //nano quantum for how long worker runs
};

void myTimerHandler(int dummy) {
  
  shmctl( shmid1, IPC_RMID, NULL ); // Free shared memory segment shm_id
  shmctl( shmid2, IPC_RMID, NULL ); 
  if (msgctl(msqid, IPC_RMID, NULL) == -1) { //Free memory queue
      perror("msgctl");
      exit(1);
   }
  cout << "Oss has been running for 3 seconds! Freeing shared memory before exiting" << endl;
  cout << "Shared memory detached" << endl;
  kill(0, SIGKILL);
  exit(1);

}

static int setupinterrupt(void) { /* set up myhandler for SIGPROF */
  struct sigaction act;
  act.sa_handler = myTimerHandler;
  act.sa_flags = 0;
  return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void) { /* set ITIMER_PROF for 60-second intervals */
  struct itimerval value;
  value.it_interval.tv_sec = 3;
  value.it_interval.tv_usec = 0;
  value.it_value = value.it_interval;
  return (setitimer(ITIMER_PROF, &value, NULL));  
}



void myHandler(int dummy) {
    shmctl( shmid1, IPC_RMID, NULL ); // Free shared memory segment shm_id
    shmctl( shmid2, IPC_RMID, NULL );
    if (msgctl(msqid, IPC_RMID, NULL) == -1) { //Free memory queue
      perror("msgctl");
      exit(1);
   } 
    cout << "Ctrl-C detected! Freeing shared memory before exiting" << endl;
    cout << "Shared memory detached" << endl;
    kill(0, SIGKILL);
    exit(1);
}

void initClock(int check) {

  
  if (check == 1) {
    shmid1 = shmget ( SHMKEY1, BUFF_SZ, 0777 | IPC_CREAT );
    if ( shmid1 == -1 ) {
      fprintf(stderr,"Error in shmget ...\n");
      exit (1);
    }
    shmid2 = shmget ( SHMKEY2, BUFF_SZ, 0777 | IPC_CREAT );
    if ( shmid2 == -1 ) {
      fprintf(stderr,"Error in shmget ...\n");
      exit (1);
    }
    cout << "Shared memory created" << endl;
    // Get the pointer to shared block
    sec = ( int * )( shmat ( shmid1, 0, 0 ) );
    nano = ( int * )( shmat ( shmid2, 0, 0 ) );
    *sec = 0;
    *nano = 0;
    return;
  }
//detaches shared memory
  else {
    shmdt(sec);    // Detach from the shared memory segment
    shmdt(nano);
    shmctl( shmid1, IPC_RMID, NULL ); // Free shared memory segment shm_id
    shmctl( shmid2, IPC_RMID, NULL ); 
    cout << "Shared memory detached" << endl;
    return;
  }
  
}

void incrementClock(int incNano, int incSec) {

  int * clockSec = ( int * )( shmat ( shmid1, 0, 0 ) );
  int * clockNano = ( int * )( shmat ( shmid2, 0, 0 ) );
  
  *clockNano = *clockNano + incNano;
  if (*clockNano >= 1000000000) {
    *clockNano = *clockNano - 1000000000;
    *clockSec = *clockSec + 1;
  }
  *clockSec = *clockSec + incSec;
  shmdt(clockSec);
  shmdt(clockNano);
  return;
  
}

int main(int argc, char *argv[])  {

//Ctrl-C handler
  signal(SIGINT, myHandler);
  if (setupinterrupt() == -1) {
    perror("Failed to set up handler for SIGPROF");
    return 1;  
  }
  if (setupitimer() == -1) {
    perror("Failed to set up the ITIMER_PROF interval timer");
    return 1;
  }


//Had issues with default selection in switch decided to have an argc catch at the beginning to insure that more than one option is given
  if (argc == 1) {
  
    cout << "Error! No parameters given, enter ./oss -h for how to operate this program" << endl;
    exit(1);

  }
  
  int opt, optCounter = 0;
  int status;
  string fValue;

//opt function to collect command line params  
  while ((opt = getopt ( argc, argv, "hr:f:" ) ) != -1) {
    
    optCounter++;
    
    switch(opt) {
    
      case 'h':
        cout << "Usage: ./oss -f logFileName" << endl;
        cout << "-f: The name of the file the program will write to for logging." << endl;
        exit(1);
        
        case 'f':
          fValue = optarg;
          while (fValue == "") {
            cout << "Error! No log file name given! Please provide a log file!" << endl;
            cin >> fValue;
          }
          break;
    } 
  }
    
//setup logfile
  ofstream logFile(fValue.c_str());
    
//Creates seed for random gen
    int randSec = 0, randNano = 0;
    srand((unsigned) time(NULL));
    initClock(1);
    
//create Message queue
  struct msgbuffer buf;
  key_t key;
  
  if ((key = ftok("oss.cpp", 'B')) == -1) {
      perror("ftok");
      exit(1);
   }
   
   if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) {
      perror("msgget");
      exit(1);
   }

//creates arracy of structs to hold max possible processes
  struct PCB processTable[18];
  //init all PCB values to 0 for better looking output
  for (int i = 0; i < 18; i++) {
    processTable[i].occupied = 0;
    processTable[i].pid = 0;
    processTable[i].timeOn = 0;
  }
  int * clockSec = ( int * )( shmat ( shmid1, 0, 0 ) );
  int * clockNano = ( int * )( shmat ( shmid2, 0, 0 ) );
  pid_t pid;
  
  int maxProcess = 100, numProcess = 0, queueChecker = 0, timePassed, procOutPCB, PCBClear = 0;
//increment clock by 1000 nanoseconds to simulate time it took to set everything above up
  incrementClock(1000, 0);
  buf.quantum = quantum;
  
  while (numProcess < maxProcess) {
    randNano = rand() % maxTimeBetweenNewProcsNS;
    incrementClock(randNano, 0);
    //loop beings by filling the PCB with processes
    //forks child only if the process table slot is unoccupied
    if (processTable[queueChecker].occupied == 0 && numProcess < maxProcess) {
      char *args[]={"./worker", NULL};
      pid = fork();
      if(pid == 0) {
        execvp(args[0],args);
        printf("Exec failed for first child\n");
        exit(1);
      }
      else {
        processTable[queueChecker].occupied = 1;
        processTable[queueChecker].pid = pid;
        cout << "OSS: Generating process with PID: " << processTable[queueChecker].pid << " and putting it in queue " << queueChecker << " at time " << *clockSec << ":" << *clockNano << endl;
        logFile << "OSS: Generating process with PID: " << processTable[queueChecker].pid << " and putting it in queue " << queueChecker << " at time " << *clockSec << ":" << *clockNano << endl;
        queueChecker++;
        numProcess++;
        incrementClock(1000, 0);
      }
    }
    else {
        buf.mtype = processTable[queueChecker].pid;
        buf.quantum = quantum;
        if (msgsnd(msqid, &buf, 20, 0) == -1)
          perror("msgsnd");
                cout << "OSS: Dispatching process with PID: " << processTable[queueChecker].pid << " from queue " << queueChecker << " at time " << *clockSec << ":" << *clockNano << endl;
        logFile << "OSS: Dispatching process with PID: " << processTable[queueChecker].pid << " from queue " << queueChecker << " at time " << *clockSec << ":" << *clockNano << endl;
        incrementClock(1000, 0);
        msgbuffer rcvbuf;
        // Then let me read a message, but only one meant for me
        // ie: the one the child just is sending back to me
        if (msgrcv(msqid, &rcvbuf,20, getpid(),0) == -1) {
          perror("failed to receive message in parent\n");
          exit(1);
        }
        if (rcvbuf.quantum < 0) {
          //cout << "CHECK1" << " rcv Quantum: " << rcvbuf.quantum << " current pid: " << processTable[queueChecker].pid << endl;
          wait(NULL);
          cout << "OSS: Total time for this dispatch was: " << -(rcvbuf.quantum) << " nanoseconds" << endl;
          logFile << "OSS: Total time for this dispatch was: " << -(rcvbuf.quantum) << " nanoseconds" << endl;
          incrementClock(-(rcvbuf.quantum), 0);
          processTable[queueChecker].timeOn += -(rcvbuf.quantum);
          cout << "OSS: Total time this process was dispatched for: " << processTable[queueChecker].timeOn << " nanoseconds" << endl;
          logFile << "OSS: Total time this process was dispatched for: " << processTable[queueChecker].timeOn << " nanoseconds" << endl;
          processTable[queueChecker].pid = 0;
          processTable[queueChecker].occupied = 0;
          processTable[queueChecker].timeOn = 0;
        }
        else {
          cout << "OSS: Total time for this dispatch was: " << rcvbuf.quantum << " nanoseconds" << endl;
          logFile << "OSS: Total time for this dispatch was: " << rcvbuf.quantum << " nanoseconds" << endl;
          incrementClock(rcvbuf.quantum, 0);
          processTable[queueChecker].timeOn += rcvbuf.quantum;
          
        }
        
        queueChecker++;
    }
    
    //Once it reaches the end of the PCB array resets back to 0
    if (queueChecker >= 18) {
      queueChecker = 0;
    }
    //cout << queueChecker << endl;
  }
  
  queueChecker = 0;
  //second while loop to catch straggling workers
  while(PCBClear == 0) {
        
    if (processTable[queueChecker].occupied == 1) {
      buf.mtype = processTable[queueChecker].pid;
      buf.quantum = quantum;
      if (msgsnd(msqid, &buf, 20, 0) == -1)
        perror("msgsnd");
      cout << "OSS: Dispatching process with PID: " << processTable[queueChecker].pid << " from queue " << queueChecker << " at time " << *clockSec << ":" << *clockNano << endl;
      logFile << "OSS: Dispatching process with PID: " << processTable[queueChecker].pid << " from queue " << queueChecker << " at time " << *clockSec << ":" << *clockNano << endl;
      incrementClock(1000, 0);
      msgbuffer rcvbuf;
        // Then let me read a message, but only one meant for me
        // ie: the one the child just is sending back to me
      if (msgrcv(msqid, &rcvbuf,20, getpid(),0) == -1) {
        perror("failed to receive message in parent\n");
        exit(1);
      }
      if (rcvbuf.quantum < 0) {
        //cout << "CHECK1" << " rcv Quantum: " << rcvbuf.quantum << " current pid: " << processTable[queueChecker].pid << endl;
        wait(NULL);
        cout << "OSS: Total time for this dispatch was: " << -(rcvbuf.quantum) << " nanoseconds" << endl;
        logFile << "OSS: Total time for this dispatch was: " << -(rcvbuf.quantum) << " nanoseconds" << endl;
        incrementClock(-(rcvbuf.quantum), 0);
        processTable[queueChecker].timeOn += -(rcvbuf.quantum);
        cout << "OSS: Total time this process was dispatched for: " << processTable[queueChecker].timeOn << " nanoseconds" << endl;
        logFile << "OSS: Total time this process was dispatched for: " << processTable[queueChecker].timeOn << " nanoseconds" << endl;
        processTable[queueChecker].pid = 0;
        processTable[queueChecker].occupied = 0;
        processTable[queueChecker].timeOn = 0;
      }
      else {
        cout << "OSS: Total time for this dispatch was: " << rcvbuf.quantum << " nanoseconds" << endl;
        logFile << "OSS: Total time for this dispatch was: " << rcvbuf.quantum << " nanoseconds" << endl;
        incrementClock(rcvbuf.quantum, 0);
        processTable[queueChecker].timeOn += rcvbuf.quantum;
      }
      queueChecker++;
    }
    
    procOutPCB = 0;
    for (int i = 0; i < 18; i++) {
      if (processTable[i].occupied == 0) {
        procOutPCB++;
      }
      if(procOutPCB == 18) {
        PCBClear = 1;
      }
    }
    if (queueChecker >= 18) {
      queueChecker = 0;
    }
  }

  cout << "Oss finished" << endl;
  logFile << "Oss finished" << endl;
  shmdt(clockSec);
  shmdt(clockNano);
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
      perror("msgctl");
      exit(1);
   }
  initClock(0);
  return 0;
    
}