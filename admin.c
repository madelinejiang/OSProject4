#include <stdio.h>
#include "simos.h"

void menu() {
	printf("Menu*********************************\n");
	printf("Enter s to submit a file\n");
	printf("Enter x to execute a file\n");
	printf("Enter y to execute multiple cycles\n");
	printf("Enter q to view ready queue and endWait queue\n");
	printf("Enter r to dump current running process registers\n");
	printf("Enter p to dump PCB information for submitted processes\n");
	printf("Enter b to dump page tables for submitted processes\n");
	printf("Enter m to dump memory content for submitted process\n");
	printf("Enter n to dump main memory contents\n");
	printf("Enter f to dump frame metadata for submitted processes\n");
	printf("Enter e to dump events\n");
	printf("Enter d to dump disk contents\n");
	printf("Enter w to dump swap queue\n");
	printf("Enter T to terminate\n");

}

void process_admin_command ()
{ 
	menu();
	char action[10];
	char fname[100];
	int round, i;

  while (systemActive)
  { printf ("command> ");
    scanf ("%s", &action);
    if (Debug) printf ("Command is %c\n", action[0]);
    // only first action character counts, discard remainder
    switch (action[0])
    { case 's':  // submit
        one_submission (); break;
      case 'x':  // execute
        execute_process (); break;
      case 'y':  // multiple rounds of execution
        printf ("Iterative execution: #rounds? ");
        scanf ("%d", &round);
        for (i=0; i<round; i++)
        { execute_process();
          if (Debug) { dump_memoryframe_info(); dump_PCB_memory(); }
        }
        break;
      case 'q':  // dump ready queue
		  //and list of processes in endWait
        dump_ready_queue ();
        dump_endWait_list ();
        break;
      case 'r':   // dump the registers
        dump_registers (); break;
      case 'p':   // dump the list of available PCBs
        dump_PCB_list (); break;
	  case 'b':
		  dump_entries();//process.c dump page table information
		 break;
      case 'm':   // dump memory of each process
        dump_PCB_memory (); break;
      case 'f':   // dump memory frames and free frame list
        dump_memoryframe_info ();
		break;
      case 'n':   // dump the content of the entire memory
        dump_memory ();
		dump_free_list();
		break;
      case 'e':   // dump events in clock.c
        dump_events (); break;
      case 't':   // dump terminal IO queue
        dump_termio_queue (); break;
      case 'w':   // dump swap queue
        dump_swapQ (); break;
	  case 'd':
		  dump_swap(); break;//dump swap.disk
      case 'T':  // Terminate, do nothing, terminate in while loop
        systemActive = 0; break;

      default:   // can be used to yield to client submission input
        printf ("Error: Incorrect command!!!\n");
    }
  }
  printf ("Admin command processing loop ended!\n");
}


