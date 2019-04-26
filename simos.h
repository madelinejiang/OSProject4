//==========================================================
// simos.h
// Authors: Joel Yin (100%)
// Work division: Refactored a number of functions
//  to clean up the output when making the final executable
//==========================================================

//================= general definitions =========================

//#define Debug 1
int Debug;
int cpuDebug, memDebug, swapDebug, clockDebug;

typedef unsigned *genericPtr;
          // when passing pointers externally, use genericPtr
          // to avoid the necessity of exposing internal structures


//======== sytem.c configuration parameters and variables =========

//cpu and process
int systemActive;
       // indicate whehter system is active,
       // every child thread should test this for termination checking

int maxProcess;    // max number of processes has to < maxProcess
int cpuQuantum;    // time quantum, defined in # instruction-cycles
int idleQuantum;   // time quantum for the idle process

//memory
#define dataSize 4   // each memory unit is of size 4 bytes
#define addrSize 4   // each memory address is of size 4 bytes
// sizes related to memory and memory management
int pageSize, numFrames;
// loadPpages: at load time, #pages allocated to each process
int loadPpages;
// maxPpages: max #pages for each process
int maxPpages;
// OSpages = #pages for OS, OS occupies the begining of the memory
int OSpages;
       
int periodAgeScan; // the period for scanning and shifting the age vectors
                   // defined in # instruction-cycles
int termPrintTime;   // simulated time (sleep) for terminal to output a string
int diskRWtime;   // simulated time (sleep) for disk IO (a page)

//=============== memory.c (NOW paging.c) related definitions ====================

// memory data type defintion, could be int or float
//typedef int mdType; 
//#define mdInFormat "%d"
//#define mdOutFormat "%d"
typedef float mdType;
#define mdInFormat "%f"
#define mdOutFormat "%.2f"

// define special values for page/frame number
#define nullIndex -1   // free frame list null pointer
#define nullPage -1   // page does not exist yet
#define diskPage -2   // page is on disk swap space
#define pendingPage -3  // page is pending till it is actually swapped
// have to ensure: #memory-frames < address-space/2, (pageSize >= 2)
//    becuase we use negative values with the frame number
// nullPage & diskPage are used in process page table 

typedef union     // type definition for memory (its content)
{ mdType mData;
  int mInstr;
} mType;

#define mNormal 1  // memory access return values
#define mError -1
#define mPFault 0

// memory read/write function definitions

int get_data (int offset); 
int put_data (int offset);
int get_instruction (int offset);
  // only cpu.c for the above 3 functions

// basic memory functions
void initialize_memory_manager ();  // called by system.c
void dump_process_memory (int pid);
void dump_process_pagetable (int pid);
void dump_memoryframe_info ();
void dump_free_list();
void dump_memory ();

// memory management functions

int allocate_memory (int pid, int msize, int numinstr);
int free_memory (int pid);  // only called by process.c 
int free_process_memory (int pid);
void memory_agescan ();  // called by cpu.c after age scan interrupt

// additional functions used by other .c files
// by loader.c and swap.c
void init_process_pagetable (int pid);
void update_process_pagetable (int pid, int page, int frame);
void update_frame_info (int findex, int pid, int page);
void direct_put_instruction (int findex, int offset, int instr);
void direct_put_data (int findex, int offset, mdType data);
int get_free_frame ();
int count_free_frames();
int load_page_to_memory(int pid, int page, unsigned *buf, int finishact);

// by cpu.c
void page_fault_handler ();


//================= cpu.c related definitions ======================

// Pid, Registers and interrupt vector in physical CPU

struct
{ int Pid;
  int PC;
  mdType AC;
  mdType MBR;
  int IRopcode;
  int IRoperand;
  int MDbase;
  int *PTptr;
  int exeStatus;
  unsigned interruptV;
  int numCycles;  // this is a global register, not for each process
} CPU;


// define interrupt set bit for interruptV in CPU structure
// 1 means bit 0, 4 means bit 2, ...

#define tqInterrupt 1      // for time quantum
#define ageInterrupt 2     // for age scan
#define endWaitInterrupt 4  // for any IO completion, including page fault
#define pFaultException 8   // page fault exception
#define pFaultInstruction 16
        // before setting endWait, caller should add the pid to endWait list

// define exeStatus in CPU structure
#define eRun 1
#define eReady 2
#define ePFault 3
#define eWait 4
#define eEnd 0
#define eError -1


// cpu function definitions

void initialize_cpu ();  // called by system.c
void cpu_execution ();   // called by process.c
void dump_registers ();
void set_interrupt (unsigned bit);  
     // called by clock.c for tqInterrup, memory.c  for ageInterrupt
     // called by clock.c for endWaitInterrupt (sleep)
     // called by term.c for endWaitInterrupt (termio)
     // called by clock.c for endWaitInterrupt (page fault)


//=============== process.c related definitions ====================

typedef struct
{ int Pid;
  int PC;
  mdType AC;
  int *PTptr;
  int MDbase;
  int exeStatus;
  int timeUsed;
  int numPF;
} typePCB;

typePCB **PCB;
  // the system can have at most maxPCB processes,
  // maxProcess further confines it
  // first process is OS, pid=0, second process is idle, pid = 1, 
  // so, pid of any user process starts from 2
  // each process get a PCB, allocate PCB space upon process creation

#define nullPid -1
#define osPid 0
#define idlePid 1

int currentPid;

void initialize_process_manager ();

// define process manipulation functions

void dump_PCB (int pid); 
void dump_PCB_list ();
void dump_PCB_memory ();
void dump_ready_queue ();

void insert_endWait_process (int pid); 
     // called by clock.c (sleep), term.c (output), memory.c (page fault)
     // need semaphore protection for the endWait queue access
void endWait_moveto_ready ();
     // called by cpu.c
void dump_endWait_list ();

void initialize_process ();  // called by system.c
int submit_process (char* fname);  // called by submit.c
void execute_process ();  // called by admin.c
void insert_ready_process(); //called by loader when loading fresh programs, and probably 


//=============== swap.c related definitions ====================

// the following flags are used in swap.c, loader.c, paging.c
#define Nothing 1   // flag values for finishact field (what to do after swap)
#define freeBuf 2   // 1: do nothing, 2: swap.c should free the input buffer
#define toReady 4   // 4: swap.c should send the process to ready queue
#define Both    6   // 6: both 2 and 4 (not used)
#define actRead 0   // flags for act (action), read or write, with(out) signal
#define actWrite 1

void insert_swapQ (int pid, int page, unsigned *buf, int act, int finishact);
void dump_swapQ ();
int dump_process_swap_page (int pid, int page);
void dump_process_swap (int pid);
void dump_swap ();
void start_swap_manager ();
void end_swap_manager ();

//=============== clock.c related definitions ====================

#define oneTimeTimer 0

// define the action codes for timer
#define actTQinterrupt 1
#define actAgeInterrupt 2
#define actReadyInterrupt 3
#define actNull 0

// define the clock function
void advance_clock ();  
     // called by cpu.c to advance instruction cycle based clock

// define the timer functions 
void dump_events ();  
void initialize_timer ();  // called by system.c
genericPtr add_timer (int time, int pid, int action, int recurperiod);
           // called by process.c for time quantum,
           // by memory.c for age scan, by cpu.c for sleep timer
void deactivate_timer (genericPtr castedevent);
     // called by process.c when process ends due to error or completed


//=============== term.c related definitions ====================

// type of the terminal output request, input to insert_termio
#define regularIO 1   // indicate that this is a regular IO
#define endIO 0   // indicate that this is the end process IO

void insert_termio (int pid, char *outstr, int status);
     // called by cpu.c for print instruction, process.c for end process print
     // need semaphore protection for the endWait queue access
void dump_termio_queue ();  
void start_terminal ();  // called by system.c
void end_terminal ();  // called by system.c


//=============== other modules ====================
// admin.c submit.c loader.c

void process_admin_command ();
void start_client_submission ();
void end_client_submission ();
void one_submission ();
int load_process (int pid, char *fname);
void load_idle_process ();
void start_swap_manager ();

// return status of loader, whether the program being loaded is correct
#define progError -1
#define progNormal 1


