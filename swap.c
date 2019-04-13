#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include "simos.h"


//======================================================================
// This module handles swap space management.
// It has the simulated disk and swamp manager.
// First part is for the simulated disk to read/write pages.
//======================================================================

#define swapFname "swap.disk"
#define itemPerLine 8
int diskfd;
int swapspaceSize;
int PswapSize;
int pagedataSize;

sem_t swap_semaq;
sem_t swapq_mutex;
sem_t disk_mutex;

pthread_t swapQThread;

//===================================================
// This is the simulated disk, including disk read, write, dump.
// The unit is a page
//===================================================
// each process has a fix-sized swap space, its page count starts from 0
// first 2 processes: OS=0, idle=1, have no swap space
// OS frequently (like Linux) runs on physical memory address (fixed locations)
// virtual memory is too expensive and unnecessary for OS => no swap needed

int read_swap_page (int pid, int page, unsigned *buf)
{ 
  // reference the previous code for this part
  // but previous code was not fully completed
  buf = (unsigned*)malloc(sizeof(unsigned*)*pagedataSize);
  if (pid < 2 || pid > maxProcess) 
  { printf ("Error: Incorrect pid for disk read: %d\n", pid); 
    return (-1);
  }
  int location = (pid-2) * PswapSize + page*pagedataSize;
  int ret = lseek (diskfd, location, SEEK_SET);
  if (ret < 0) perror ("Error lseek in read: \n");


  int retsize = read (diskfd, buf, pagedataSize);
  if (retsize != pagedataSize) 
  { printf ("Error: Disk read returned incorrect size: %d\n", retsize); 
    exit(-1);
  }
  usleep (diskRWtime);

  //we should return something better than just 0
  return 0;
}

int write_swap_page (int pid, int page, unsigned *buf)
{ 
  // reference the previous code for this part
  // but previous code was not fully completed
  if (pid < 2 || pid > maxProcess) 
  { printf ("Error: Incorrect pid for disk write: %d\n", pid); 
    return (-1);
  }
  int location = (pid-2) * PswapSize + page*pagedataSize;
  int ret = lseek (diskfd, location, SEEK_SET);
  if (ret < 0) perror ("Error lseek in write: \n");
  int retsize = write (diskfd, buf, pagedataSize);
  if (retsize != pagedataSize) 
    { printf ("Error: Disk write returned incorrect size: %d\n", retsize); 
      exit(-1);
    }
  usleep (diskRWtime);

  //we should return something better than just 0
  return 0;
}

int dump_process_swap_page (int pid, int page)
{ 
  // reference the previous code for this part
  // but previous code was not fully completed
  if (pid < 2 || pid > maxProcess) 
  { printf ("Error: Incorrect pid for disk dump: %d\n", pid); 
    return (-1);
  }
  int location = (pid-2) * PswapSize + page*pagedataSize;
  int ret = lseek (diskfd, location, SEEK_SET);
  //printf ("loc %d %d %d, size %d\n", pid, page, location, pagedataSize);
  if (ret < 0) perror ("Error lseek in dump: \n");
  char *buf = (char *) malloc(pagedataSize);
  int retsize = read (diskfd, buf, pagedataSize);
  if (retsize != pagedataSize) 
  { printf ("Error: Disk dump read incorrect size: %d\n", retsize); 
    exit(-1);
  }
  printf ("Content of process %d page %d:\n", pid, page);
  int k;
  for (k=0; k<pageSize; k++) printf ("%d ", buf[k]);
  printf ("\n");

  //we should return something better than just 0
  return 0;
}

void dump_process_swap (int pid)
{ int j;

  printf ("****** Dump swap pages for process %d\n", pid);
  for (j=0; j<maxPpages; j++) dump_process_swap_page (pid, j);
}
void dump_swap() {
	int pid;
	if (currentPid == 2) {
		printf("No processes loaded in swap.disk");
	}
	for (pid = idlePid + 1; pid < currentPid; pid++)
		if (PCB[pid] != NULL) dump_process_swap(pid);
}
// open the file with the swap space size, initialize content to 0
void initialize_swap_space ()
{ int ret, i, j, k;
  int buf[pageSize];

  swapspaceSize = maxProcess*maxPpages*pageSize*dataSize;
  PswapSize = maxPpages*pageSize*dataSize;
  pagedataSize = pageSize*dataSize;

  diskfd = open (swapFname, O_RDWR | O_CREAT, 0600);
  if (diskfd < 0) { perror ("Error open: "); exit (-1); }
  ret = lseek (diskfd, swapspaceSize, SEEK_SET); 
  if (ret < 0) { perror ("Error lseek in open: "); exit (-1); }
  for (i=2; i<maxProcess; i++)
    for (j=0; j<maxPpages; j++)
    { for (k=0; k<pageSize; k++) buf[k]=0;
      write_swap_page (i, j, buf);
    }
    // last parameter is the origin, offset from the origin, which can be:
    // SEEK_SET: 0, SEEK_CUR: from current position, SEEK_END: from eof
}


//===================================================
// Here is the swap space manager. 
//===================================================
// When a process address to be read/written is not in the memory,
// meory raises a page fault and process it (in kernel mode).
// We implement this by cheating a bit.
// We do not perform context switch right away and switch to OS.
// We simply let OS do the processing.
// OS decides whether there is free memory frame, if so, use one.
// If no free memory, then call select_aged_page to free up memory.
// In either case, proceed to insert the page fault req to swap queue
// to let the swap manager bring in the page
//===================================================

typedef struct SwapQnodeStruct
{ int pid, page, act, finishact;
  unsigned *buf;
  struct SwapQnodeStruct *next;
} SwapQnode;
// pidin, pagein, inbuf: for the page with PF, needs to be brought in
// pidout, pageout, outbuf: for the page to be swapped out
// if there is no page to be swapped out (not dirty), then pidout = nullPid
// inbuf and outbuf are the actual memory page content

SwapQnode *swapQhead = NULL;
SwapQnode *swapQtail = NULL;

void print_one_swapnode (SwapQnode *node)
{ printf ("pid,page=(%d,%d), act,ready=(%d, %d), buf=%x\n", 
           node->pid, node->page, node->act, node->finishact, node->buf);
}

void dump_swapQ ()
{ 
  // dump all the nodes in the swapQ
  if(swapQhead ==NULL){ 
    printf("*** Swap Q Empty ***\n");
  } else {
    printf("********************* Dumping Swap Q\n");
    SwapQnode *node = swapQhead;
    while(node != NULL){
      print_one_swapnode(node);
	  node = node->next;
    }
  }
}

// act can be actRead or actWrite
// finishact indicates what to do after read/write swap disk is done, it can be:
// toReady (send pid back to ready queue), freeBuf: free buf, Both, Nothing
void insert_swapQ (pid, page, buf, act, finishact)
int pid, page, act, finishact;
unsigned *buf;
{   sem_wait(&swapq_mutex);
  SwapQnode *node = (SwapQnode *) malloc(sizeof(SwapQnode));

  node->pid = pid;
  node->page = page;
  node->act = act;
  node->finishact = finishact;
  node->buf = buf;
  ////MJ
  if (swapQhead == NULL) { //empty swapQ so make it the head
	  swapQhead = node;
  }
  else {// not an empty swapQ, add to the tail
	  swapQtail->next = node;
  }
  swapQtail = node;  //new node is the tail
  swapQtail->next = NULL;
  sem_post(&swap_semaq);

  printf("finished inserting one item into swapQ\n");//debugging

  sem_post(&swapq_mutex);
}

void *process_swapQ ()
{
  // called as the entry function for the swap thread
  //wait for something in the queue before proceeding
	while (systemActive) {
		sem_wait(&swap_semaq);
		sem_wait(&swapq_mutex);
		//<critical section>
		//dequeue
		SwapQnode *node = swapQhead;
		swapQhead = node->next;

		//prepare for the disk action
		//
		switch (node->act) {
		case actRead:
		{
		//read from swap space
			read_swap_page(node->pid, node->page, node->buf);
		}
		break;
		case actWrite: {
			//write to swap space
			write_swap_page(node->pid, node->page,node->buf);
			printf("wrote to swap.disk %d %d %u\n", node->pid, node->page, node->buf);
		}
			break;
		default:
			break;
		}
		switch (node->finishact) {
		case Nothing:
			break;
		case freeBuf:
			if (node->act==actRead) {
				//should only occur for write not read
				printf("Attempt to free buffer during read\n");
			}
			else {
				node->buf = NULL;
			}
			break;
		case toReady:
			insert_endWait_process(node->pid);
			set_interrupt(endWaitInterrupt);
			break;
		case Both:
			node->buf = NULL;
			insert_endWait_process(node->pid);
			set_interrupt(endWaitInterrupt);
			break;
		default:
			break;

		}
		free(node);
		node = NULL;
		sem_post(&swapq_mutex);
	}

}

void start_swap_manager ()
{ 
  // initialize semaphores
  sem_init(&swap_semaq, 0, 0);  //nothing in swapq
  sem_init(&swapq_mutex, 0, 1); //swapq should be available at the start
  sem_init(&disk_mutex, 0, 1);  //disk should be available at the start

  initialize_swap_space ();

  // create swap thread
  int ret = pthread_create(&swapQThread, NULL, process_swapQ, NULL);
  if(ret < 0){
    printf("Swap.c thread creation problem.\n");
    exit(1);
  } else {
    printf("SwapQ thread created successfully!\n");
  }

}

void end_swap_manager ()
{ 
  // terminate the swap thread
  sem_post(&swap_semaq);
  sem_post(&swapq_mutex);
  sem_post(&disk_mutex);
  close(diskfd);
  int ret = pthread_join(swapQThread, NULL);

  printf("Swap Q thread has terminated. Return int: %d\n", ret);
}


