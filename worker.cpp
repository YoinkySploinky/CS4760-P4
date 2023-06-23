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

using namespace std;
const int PERMS = 0644;

//message buffer
struct msgbuffer {
   long mtype; //needed
   int quantum; //nano quantum for how long worker runs
};

int main(int argc, char *argv[]) {

  struct msgbuffer buf;
  buf.mtype = 1;
  int msqid = 0;
  key_t key;
  
  if ((key = ftok("oss.cpp", 'B')) == -1) {
    perror("ftok");
    exit(1);
   }
   
  if ((msqid = msgget(key, PERMS)) == -1) {
    perror("msgget in child");
    exit(1);
  }

  //Create random value between 1 and 10 to decide what the worker is going to do
  int randOpt = 0;
  int quantum = 1;
  msgbuffer rcvbuf;
  msgbuffer sndbuf;
  srand((unsigned) time(NULL) * getpid());
  
  while(true) {
       //receive message
    if ( msgrcv(msqid, &rcvbuf, 20, getpid(), 0) == -1) {
      perror("failed to receive message from parent\n");
      exit(1);
    }
    randOpt = 1 + rand() % 10;
   // cout << "RandOpt: " << randOpt << endl;
  //if randOpt is 1,2,3,4,5 the worker will use the full time quantum;
    if (randOpt < 6) {
      //cout << "Opt1" << endl;
      quantum = rcvbuf.quantum; 
      //cout << quantum << endl;
    }
    //If randOpt is 6,7,8 the worker will be I/O interrupted and return quantum - 5000ns
    if (randOpt >= 6 && randOpt < 9) {
      //cout << "Opt2" << endl;
      quantum = rcvbuf.quantum - 5000;
      //cout << quantum << endl;
    }
    //if randOpt is 9 or 10 the worker will use full time quantum then return a negative number to show that it is finished
    if (randOpt >= 9) {
      //cout << "Opt3" << endl;
      quantum = -(rcvbuf.quantum);
      break;
    }
      // now send a message back to our parent
    sndbuf.mtype = getppid();
    sndbuf.quantum = quantum;
    if (msgsnd(msqid, &sndbuf, 20, 0) == -1) {
      perror("msgsnd to parent failed\n");
      exit(1);
    }
    
  }
  //Second msg send to catch the break option from the while loop
  sndbuf.mtype = getppid();
  sndbuf.quantum = quantum;
  if (msgsnd(msqid, &sndbuf, 20, 0) == -1) {
    perror("msgsnd to parent failed\n");
    exit(1);
  }

  
  return 0;
}