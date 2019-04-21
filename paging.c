#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "simos.h"

// Memory definitions, including the memory itself and a page structure
// that maintains the informtion about each memory page
// config.sys input: pageSize, numFrames, OSpages
// ------------------------------------------------
// process page table definitions
// config.sys input: loadPpages, maxPpages

mType *Memory;   // The physical memory, size = pageSize*numFrames

typedef unsigned char ageType;
typedef struct
{ int pid, page;   // the frame is allocated to process pid for page page
  ageType age;
  char free, dirty, pinned;   // in real systems, these are bits
  int next, prev;
} FrameStruct;

FrameStruct *memFrame;   // memFrame[numFrames]
int freeFhead, freeFtail;   // the head and tail of free frame list

// define values for fields in FrameStruct
#define zeroAge 0x00
#define highestAge 0x80
// #define zeroAge 0x00000000 // change ageType back to just unsigned if you want to use these two lines
// #define highestAge 0x80000000
#define dirtyFrame 1
#define cleanFrame 0
#define freeFrame 1
#define usedFrame 0
#define pinnedFrame 1
#define nopinFrame 0

// define shifts and masks for instruction and memory address 
#define opcodeShift 24
#define operandMask 0x00ffffff

// shift address by pagenumShift bits to get the page number
unsigned pageoffsetMask;
int pagenumShift; // 2^pagenumShift = pageSize

//============================
// Our memory implementation is a mix of memory manager and physical memory.
// get_instr, put_instr, get_data, put_data are the physical memory operations
//   for instr, instr is fetched into registers: IRopcode and IRoperand
//   for data, data is fetched into registers: MBR (need to retain AC value)
// page table management is software implementation
//============================


//==========================================
// run time memory access operations, called by cpu.c
//==========================================

// define rwflag to indicate whehter the addr computation is for read or write
#define flagRead 1
#define flagWrite 2

// address calcuation are performed for the program in execution
// so, we can get address related infor from CPU registers
int calculate_memory_address (unsigned offset, int rwflag)
{ 
  // rwflag is used to differentiate the caller
  // different access violation decisions are made for reader/writer
  // if there is a page fault, need to set the page fault interrupt
  // also need to set the age and dirty fields accordingly
  // returns memory address or mPFault or mError

  // check to see if offset is out of memory address

  // get PTindex from offset
  //in case offset is 0, pageIndex will be -1, but it should be 0
  int pageIndex;
  if(offset == 0){
    pageIndex = 0;
  } else {
    pageIndex = (offset) / pageSize ;
  }
  
  if(pageIndex >= maxPpages){
    // Definintely a memory access violation, outside of pagetable addressing
    return mError;
  }

  //after we get the pageIndex, check the PT and return appropriate result
  int frame = CPU.PTptr[pageIndex];
  printf("page checked out to be frame: %d\n", frame);
  switch(frame){
    case nullPage:
      // return this since this is a access violation
      return mError;
    case diskPage:
      // return this since this is a page fault
        return mPFault;
      break;
    case pendingPage:
      // Not sure what to do here... Nothing?
      // If it hits this, then we've done something wrong I think
      printf("ERROR: @calculate_address() we are definitely doing something wrong\n");
      return -1;
	    break;
    default: {
      int memOffset = frame * pageSize;
      int pageOffset = offset - pageIndex * pageSize;
      int address = memOffset + pageOffset;
      if(rwflag == flagWrite){
        memFrame[frame].dirty = dirtyFrame;
      }
      return address;
    }
  }
  //Should not reach this point
  return -7; 
}

int get_data (int offset)
{ 
  // call calculate_memory_address to get memory address
  // copy the memory content to MBR
  // return mNormal, mPFault or mError

  offset += CPU.MDbase;
  int address = calculate_memory_address(offset, flagRead);

  switch(address){
    case mError:
      return mError;
    case mPFault:
      return mPFault;
    default:
      CPU.MBR = Memory[address].mData;
      return mNormal;
  }

}

int put_data (int offset)
{ 
  // call calculate_memory_address to get memory address
  // copy MBR to memory 
  // return mNormal, mPFault or mError
  offset += CPU.MDbase;
  int address = calculate_memory_address(offset, flagWrite);

  switch(address){
    case mError:
      return mError;
    case mPFault:
      return mPFault;
    default:
      Memory[address].mData = CPU.MBR;
      // dirty bit set in calculate_address since it's easier there than here
      return mNormal;
  }
}

int get_instruction (int offset)
{ 
  // call calculate_memory_address to get memory address
  // convert memory content to opcode and operand
  // return mNormal, mPFault or mError

  int address = calculate_memory_address(offset, flagRead);
  switch(address){
    case mError:
      return mError;
    case mPFault:
      return mPFault;
    default: {
      int instr = Memory[address].mInstr;
      CPU.IRopcode = instr >> opcodeShift;
      CPU.IRoperand = instr & operandMask;
      return mNormal;
	  }
  }
}

// these two direct_put functions are only called for loading idle process
// no specific protection check is done
void direct_put_instruction (int findex, int offset, int instr)
{ int addr = (offset & pageoffsetMask) | (findex << pagenumShift);
  Memory[addr].mInstr = instr;
}

void direct_put_data (int findex, int offset, mdType data)
{ int addr = (offset & pageoffsetMask) | (findex << pagenumShift);
  Memory[addr].mData = data;
}

//==========================================
// Memory and memory frame management
//==========================================

// dump the content of one memory frame
void dump_one_frame (int findex) { 
  int i;
  printf("************ Dump contents of frame %d\n", findex);
  
  for(i = findex * pageSize; i < (findex + 1) * pageSize; i++){
    printf("Memory @ - 0x%08x| Data - 0x%08x\n", i, Memory[i]);
  }
}

void dump_memory ()
{ int i;

  printf ("************ Dump the entire memory\n");
  for (i=0; i<numFrames; i++) dump_one_frame (i);
}

// above: dump memory content, below: only dump frame inforation

// dump the list of free memory frames
void dump_free_list ()
{ int i = freeFhead;

  printf ("******************** Free Frame List\n");

  while(i > nullIndex){ //currently gives an endless loop MJ
    printf ("Free Frame %d->", i);
    i = memFrame[i].next;
  }
  printf("||\n");
}

void print_one_frameinfo (int indx)
{ printf ("pid/page/age=%d,%d,%x, ",
          memFrame[indx].pid, memFrame[indx].page, memFrame[indx].age);
  printf ("dir/free/pin=%d/%d/%d, ",
          memFrame[indx].dirty, memFrame[indx].free, memFrame[indx].pinned);
  printf ("preb/next=%d,%d\n",
          memFrame[indx].prev, memFrame[indx].next);
}

void dump_memoryframe_info ()
{ int i;

  printf ("******************** Memory Frame Metadata\n");
  printf ("Memory frame head/tail: %d/%d\n", freeFhead, freeFtail);
  for (i=OSpages; i<numFrames; i++)
  { printf ("Frame %d: ", i); print_one_frameinfo (i); }
  dump_free_list ();
}

void  update_frame_info (findex, pid, page)
int findex, pid, page;
{
  // update the metadata of a frame, need to consider different update scenarios
  // need this function also because loader also needs to update memFrame fields
  // while it is better to not to expose memFrame fields externally

  memFrame[findex].pid = pid;
  memFrame[findex].page = page;

  // we have to update necesarry things if frame is now free
  // by passing pid=nullPid and (page=nullIndex or page==nullPage), this will "reset" frame
  if(pid == nullPid && (page == nullIndex || page == nullPage)){
    memFrame[findex].age = zeroAge;
    memFrame[findex].dirty = cleanFrame;
    memFrame[findex].free = freeFrame;
    memFrame[findex].pinned = nopinFrame;
  }
}

// should write dirty frames to disk and remove them from process page table
// but we delay updates till the actual swap (page_fault_handler)
// unless frames are from the terminated process (status = nullPage)
// so, the process can continue using the page, till actual swap
void addto_free_frame (int findex, int status)
{
  if(status == nullPage){
    // if nullPage, immediately add to free pages
    // there is no need to care about properly swapping out
    // just overwrite the frame info, no loss of important info
    update_frame_info(findex, nullPid, nullIndex);

  }

  if(freeFtail == nullIndex){
    // if the tail is pointing to nullIndex, there's no head either
    freeFhead = findex;
  } else {
    memFrame[freeFtail].next = findex;
    memFrame[findex].prev = freeFtail;
  }
  freeFtail = findex;
}

int select_agest_frame ()
{ 
  // select a frame with the lowest age 
  // if there are multiple frames with the same lowest age, then choose the one
  // that is not dirty

  // start iterating through, we'll be unfair and choose the first instance 
  // if there are multiple frames that are of lower age and not dirty

  // Strategy: Perform 2 linear searches, 
  // First pass, get smallest age as well as count
  int oldestCleanFrame = nullIndex;
  int frameToFree = nullIndex;
  unsigned ageOfOldestFrame = highestAge;
  
  int frameIndex;
  FrameStruct frame;
  int OldestFramesCount = 0;
  // Start at pid after OS frames
  for(frameIndex = OSpages; frameIndex < numFrames; frameIndex++){
    frame = memFrame[frameIndex];
    //skip the frame if it's pinned
    if(frame.pinned == nopinFrame){
      if(frame.age < ageOfOldestFrame){
        ageOfOldestFrame = frame.age;
        if(frame.age == zeroAge){
          break;
        }
      }
    }
  }

  int k, offset;
  mType *buf;
  // Free all frames that are of the oldest age/smallest age
  for(frameIndex = OSpages; frameIndex < numFrames; frameIndex++){
    frame = memFrame[frameIndex];
    if(frame.pinned == nopinFrame && frame.age == ageOfOldestFrame && frame.pid){
      if(oldestCleanFrame == nullIndex){
        oldestCleanFrame = frameIndex;
      } else {
        if(memFrame[oldestCleanFrame].dirty == dirtyFrame){
          if(frame.dirty == cleanFrame){
            frame = memFrame[oldestCleanFrame];
            frameToFree = oldestCleanFrame;
            oldestCleanFrame == frameIndex;
          } else {
            addto_free_frame(frameIndex, nullPage);
            frameToFree = nullIndex;
          }
        } else {
          frameToFree = frameIndex;
        }
        if(frameToFree == nullIndex){
          // We only bother to write to swap disk if the frame is dirty
          // or in this case, the frame to free asap exists
          offset = frameToFree * pageSize;
          buf = (mType *) malloc(sizeof(mType) * pageSize);
          for(k = 0; k < pageSize; k++){
            buf[k] = Memory[offset + k];
          }
          printf("frame pid-%d pg-%d frameI-%d oldestcleanFrameI-%d;", frame.pid, frame.page, frameIndex, oldestCleanFrame);
          //update_process_pagetable(frame.pid, frame.page, pendingPage);
          insert_swapQ(frame.pid, frame.page, (unsigned *)buf, actWrite, freeBuf);
        }

      }
    }
  }

  return oldestCleanFrame;
}

// get a free frame from the head of the free list 
// if there is no free frame, then get one frame with the lowest age
// this func always returns a frame, either from free list or get one with lowest age
int get_free_frame (){ 
  int freeFrameIndex;
  // if the there is a head, then there are free pages
  // If freeFhead is 0, then we've done something very wrong somewhere
  // It should always be 2 or greater
  // same for next and prev for any Q element
  if(freeFhead != nullIndex){
    freeFrameIndex = freeFhead;
    int next = memFrame[freeFhead].next;
    //if there is no next frame, then tail and head must be set to 0
    if(next != nullIndex){
      memFrame[next].prev = nullIndex;
      freeFhead = next;
    } else {
      freeFhead = nullIndex;
      freeFtail = nullIndex;
    }
    memFrame[freeFrameIndex].next = nullIndex;
    return freeFrameIndex;
  } else {
    // we are assuming that a free frame will need to be used
    // so dequeue the frame from list
    
    // in the case there are no free frames, we'll need to get oldest frame, preferably not dirty
    // This will be handled separately
    
    return nullIndex;
  }
} 

// this determines whether a page is all data, all instr, or a mix
// mainly to help make sense of load_page_to_memory()
#define pData 1
#define pInstr 2
#define pMix 4

int load_page_to_memory(int pid, int page, unsigned *buf, int action){
  int frameIndex = get_free_frame();
  if (frameIndex == nullIndex) { //no free frames
		//get the lowest age frame
		frameIndex = select_agest_frame();

    FrameStruct frame = memFrame[frameIndex];
		//need to identify the pid of the frame being swapped out
		int pidout = frame.pid;
		int pageout = frame.page;

		if (frame.dirty == dirtyFrame) {
			// if the frame is dirty, insert a write request to swapQ 
			//buf will be the contents of the frame in memory
			//mType outbuf = (mType *)malloc(pageSize * sizeof(mType));
			//mType outbuf = Memory[frame * pageSize];
			int j = 0;
			mType *outbuf = (mType*) malloc(pageSize * sizeof(mType));
			for (int i = frameIndex * pageSize; i < (frameIndex + 1) * pageSize; i++) {
				outbuf[j] = Memory[i];
				j++;
			}
      update_process_pagetable(pidout, pageout, pendingPage);
			insert_swapQ(pidout, pageout, (unsigned *) outbuf, actWrite, freeBuf);
		}
	} // else since the frame isn't dirty, we don't need to write back to swapQ

  // Needed to properly interpret the incoming buffer, apparently
  mType *inbuf = (mType *) buf;
  int j = 0;
  for (int i = frameIndex * pageSize; i < (frameIndex + 1) * pageSize; i++) {
    Memory[i] = inbuf[j];  
    j++;
  }

  if(action == toReady){
    insert_ready_process(pid);
  }

  update_frame_info(frameIndex, pid, page);
  update_process_pagetable(pid, page, frameIndex);
  free(inbuf);
  return 0;
}

void initialize_memory ()
{ int i;

  // create memory + create page frame array memFrame 
  Memory = (mType *) malloc (numFrames*pageSize*sizeof(mType));
  memFrame = (FrameStruct *) malloc (numFrames*sizeof(FrameStruct));

  // compute #bits for page offset, set pagenumShift and pageoffsetMask
  // *** ADD CODE
  pagenumShift = (int)round(log2(pageSize)); // I'm rounding just in case I have some imprecision
  pageoffsetMask = ~(-1 << pagenumShift);

  // initialize OS pages
  for (i=0; i<OSpages; i++)
  { memFrame[i].age = zeroAge;
    memFrame[i].dirty = cleanFrame;
    memFrame[i].free = usedFrame;
    memFrame[i].pinned = pinnedFrame;
    memFrame[i].pid = osPid;
    memFrame[i].next = nullIndex;
    memFrame[i].prev = nullIndex;
  }
  // initilize the remaining pages, also put them in free list
  // *** ADD CODE
  // Create pages and set them as free pages
  for(i = OSpages; i<numFrames; i++){
    memFrame[i].age = zeroAge;
    memFrame[i].dirty = cleanFrame;
    memFrame[i].free = freeFrame;
    memFrame[i].pinned = nopinFrame;
    memFrame[i].next = nullIndex;
    memFrame[i].prev = nullIndex;
  }

  // Create the next free frame for all but the last frame
  for(i = OSpages; i<numFrames-1; i++){
    memFrame[i].next = i+1;
  }

  // Create the prev free frame for all but the first free frame
  for(i = numFrames - 1; i > OSpages; i--){
    memFrame[i].prev = i-1;
  }

  // First free frame is at OSpages, last is at numFrames-1
  freeFhead = OSpages;
  freeFtail = numFrames - 1;
}

//==========================================
// process page table manamgement
//==========================================

void init_process_pagetable (int pid)
{ int i;

  PCB[pid]->PTptr = (int *) malloc (sizeof(int)*maxPpages);
  for (i=0; i<maxPpages; i++) PCB[pid]->PTptr[i] = nullPage;
}

// frame can be normal frame number or nullPage, diskPage
// frame should really be called frametype imo
void update_process_pagetable (pid, page, frame)
int pid, page, frame;
{ 
  // update the page table entry for process pid to point to the frame
  // or point to disk or null

  PCB[pid]->PTptr[page] = frame;
}

int free_process_memory (int pid)
{ 
  // free the memory frames for a terminated process
  // some frames may have already been freed, but still in process pagetable
  int pageIndex, frameIndex;
  for(pageIndex = 0; pageIndex < maxPpages; pageIndex++){
    frameIndex = PCB[pid]->PTptr[pageIndex];
    switch(frameIndex){
      case nullPage:
        // don't need to do anything I think
      case diskPage:
        // Technically don't need to do anything
      case pendingPage:
        // I'm hoping we don't have to deal with this, cuz I don't know how'd we handle this
        break;
      default:
        // update_frame_info(frameIndex, nullPid, nullIndex); // not really needed I guess, it'll get written over
        addto_free_frame(frameIndex, nullPage);
        break;
    }
  }
}

void dump_process_pagetable (int pid)
{ 
  // print page table entries of process pid
  printf ("************** Page Table for Process pid: %d\n", pid);
  int i;
  for (i=0; i<maxPpages; i++) { 
    printf ("Page %d @ %d: ", i, PCB[pid]->PTptr[i]); 
  }

}

void dump_process_memory (int pid)
{ 
  // print out the memory content for process pid
  printf ("************** Memory Content for Process pid: %d\n", pid);
  int i, frame;
  for (i=0; i<maxPpages; i++) { 
    frame = PCB[pid]->PTptr[i];
    switch(frame){
      case nullPage:
        // break out of for loop by setting i to maxPpages
        i = maxPpages;
        break;
      case diskPage:
        printf("Page %d is in DISK\n", i);
        break;
      case pendingPage:
        printf("Page %d is in SWAPQ\n", i);
        break;
      default:
        printf("Page %d is in frame %d\n", i, frame);
        dump_one_frame(frame);
        break;
    }
  }
}

//==========================================
// the major functions for paging, invoked externally
//==========================================

#define OPload 2
#define OPstore 6

void page_fault_handler (unsigned pFaultTypeBit)
{ 
	// pidin, pagein, inbuf: for the page with PF, needs to be brought into mem 
  // pidout, pageout, outbuf: for the page to be swapped out (write to disk)
  // if there is no page to be swapped out (not dirty), then pidout = nullPid
  // inbuf and outbuf are the actual memory page content
  /*=======================^From original file*========================================*/
  // context switch On a page fault, the state of the faulting program is saved and the O.S.takes over
	// via process.c (TODO)
	// get_free_frame should be called only once, upon load to memory
  // the select_agest_frame should also be in load_page_to_memory
  // SO what does this do? Basically it just sends the page request to swapQ
  // Then load_page_to_memory does its best to load the page, and swap out if needed
	
	// update the frame metadata and the page tables of the involved processes
  int pagein;
  if(pFaultTypeBit == pFaultInstruction){
    pagein = CPU.PC;
  } else {
    pagein = CPU.MDbase + CPU.IRoperand;
  }
	pagein = pagein / pageSize;
	int pidin = CPU.Pid;
  update_process_pagetable(CPU.Pid, pagein, pendingPage);
	insert_swapQ(pidin, pagein, NULL, actRead, toReady);
}

// scan the memory and update the age field of each frame
void memory_agescan ()
{ int frameIndex;
  for(frameIndex = OSpages; frameIndex < numFrames; frameIndex++){
    FrameStruct frame = memFrame[frameIndex];
    // if frame is free, don't bother with it
    // otherwise, we need to shift the bits
    if(memFrame[frameIndex].pinned == nopinFrame && memFrame[frameIndex].free == usedFrame){
      frame.age = frame.age >> 1;
      // Do I need to free the pages if they are too old here?
      // I have a feeling that I do have to
      if(frame.age == zeroAge){
        // since frame is old, we'll need to swap it out to swap.disk
        // free page
        if(frame.dirty == dirtyFrame){
          //if it is dirty, we need to shove it to the swapQ
          int k;
          mType *buf = malloc(sizeof(mType) * pageSize);
          for(k = 0; k < pageSize; k++){
            buf[k] = Memory[frameIndex * pageSize + k];
          }
          insert_swapQ(frame.pid, frame.page, (unsigned *) buf, actWrite, freeBuf);
        }
      }
    }
  }
}

void start_periodical_page_scan ()
{ add_timer (periodAgeScan, osPid, actAgeInterrupt, periodAgeScan);
}


void initialize_memory_manager ()
{ 
  // initialize memory and add page scan event request
  initialize_memory();
  //start_periodical_page_scan();
}

