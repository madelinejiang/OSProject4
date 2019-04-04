#include <stdio.h>
#include <stdlib.h>
#include "simos.h"

// need to be consistent with paging.c: mType and constant definitions
#define opcodeShift 24
#define operandMask 0x00ffffff
#define diskPage -2

FILE *progFd;

//==========================================
// load program into memory and build the process, called by process.c
// a specific pid is needed for loading, since registers are not for this pid
//==========================================

// load instruction to buffer
// may return progNormal or progError (the latter, if the program is incorrect)
int load_instruction (mType *buf, int page, int offset){ 
  int ret, opcode, operand;
  ret = fscanf (progFd, "%d %d\n", &opcode, &operand);
  if(ret < 2) {
    return(progError);
  }

  if(Debug) {
    ("Loading instruction: %d, %d\n", opcode, operand); 
  }
  opcode = opcode << opcodeShift;
  operand = operand & operandMask;
  buf[offset].mInstr = opcode | operand;
  return (progNormal);
}

// load data to buffer (same as load instruction, but use the mData field
int load_data (mType *buf, int page, int offset)
{ 
  int ret, data;
  
  ret = fscanf (progFd, "%d\n", &data);
  if(ret < 1) {
    return(progError);
  }

  if(Debug) {
    ("Loading data: %d\n", data); 
  }
  buf[offset].mData = data;
  return (progNormal);
}

// load program to swap space, returns the #pages loaded
int load_process_to_swap (int pid, char *fname)
{ 
  // read from program file "fname" and call load_instruction & load_data
  // to load the program into the buffer, write the program into
  // swap space by inserting it to swapQ
  // update the process page table to indicate that the page is not empty
  // and it is on disk (= diskPage)

  int msize, numinstr, numdata;
  int ret, i, j, opcode, operand;
  float data;

  progFd = fopen(fname, "w");
  if(progFd == NULL){
    printf("Submission Error: Program name not found, incorrect program name: %s!\n", fname);
    return progError;
  }
  ret = fscanf(progFd, "%d %d %d\n", &msize, &numinstr, &numdata);
  // did not get all three inputs
  if(ret < 3) {
    printf("Submission failure: Invalid file, missing %d program parameters!\n", 3-ret);
    return progError;
  }

  //Pages needed needs to check for msize - 1, because if msize is 32, it should still fit on 1 page
  //And I HIGHLY doubt we will get an msize of 0, but may add check later down the line
  int pagesNeeded = (msize - 1) / pageSize + 1;
  int line = 0;
  for(i = 0; i < pagesNeeded; i++){
    mType *page = (mType *) malloc (pageSize*sizeof(mType));
    for(j=0; j < pageSize; j++){
      if(line < msize){
        if(line < numinstr){
          load_instruction(page, i, j);
        } else {
          load_data(page, i, j);
        }
      } else {
        break;
      }
    }
    insert_swapQ(pid, i, (unsigned *) page, actWrite, freeBuf);
    PCB[pid]->PTptr[i] = diskPage;
  }
}

int load_pages_to_memory (int pid, int numpages)
{
  // call insert_swapQ to load the pages of process pid to memory
  // #pages to load = min (loadPpages, numpage = #pages loaded to swap for pid)
  // ask swap.c to place the process to ready queue only after the last load
  // do not forget to update the page table of the process
  // this function has some similarity with page fault handler
  int k;
  for(k = 0; k < numpages; k++){
    unsigned *buf = malloc(pageSize * dataSize);
    insert_swapQ(pid, k, buf, actRead, toReady);
    //update PCB
  }

  
  return 0;
}

int load_process (int pid, char *fname)
{ int ret;
  ret = load_process_to_swap (pid, fname);   // return #pages loaded

  if (ret != progError) load_pages_to_memory (pid, ret);
  return (ret);
}

// load idle process, idle process uses OS memory
// We give the last page of OS memory to the idle process
#define OPifgo 5   // has to be consistent with cpu.c
void load_idle_process ()
{ int page, frame;
  int instr, opcode, operand, data;

  init_process_pagetable (idlePid);
  page = 0;   frame = OSpages - 1;
  update_process_pagetable (idlePid, page, frame);
  update_frame_info (frame, idlePid, page);
  
  // load 1 ifgo instructions (2 words) and 1 data for the idle process
  opcode = OPifgo;   operand = 0;
  instr = (opcode << opcodeShift) | operand;
  direct_put_instruction (frame, 0, instr);   // 0,1,2 are offset
  direct_put_instruction (frame, 1, instr);
  direct_put_data (frame, 2, 1);
}

