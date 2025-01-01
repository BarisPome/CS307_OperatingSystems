#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm_dbg.h"

#define NOPS (16)

#define OPC(i) ((i) >> 12)
#define DR(i) (((i) >> 9) & 0x7)
#define SR1(i) (((i) >> 6) & 0x7)
#define SR2(i) ((i) & 0x7)
#define FIMM(i) ((i >> 5) & 01)
#define IMM(i) ((i) & 0x1F)
#define SEXTIMM(i) sext(IMM(i), 5)
#define FCND(i) (((i) >> 9) & 0x7)
#define POFF(i) sext((i) & 0x3F, 6)
#define POFF9(i) sext((i) & 0x1FF, 9)
#define POFF11(i) sext((i) & 0x7FF, 11)
#define FL(i) (((i) >> 11) & 1)
#define BR(i) (((i) >> 6) & 0x7)
#define TRP(i) ((i) & 0xFF)

/* New OS declarations */

// OS bookkeeping constants
#define PAGE_SIZE (4096)   // Page size in bytes
#define OS_MEM_SIZE (2)    // OS Region size. Also the start of the page tables' page
#define Cur_Proc_ID (0)    // id of the current process
#define Proc_Count (1)     // total number of processes, including ones that finished executing.
#define OS_STATUS (2)      // Bit 0 shows whether the PCB list is full or not
#define OS_FREE_BITMAP (3) // Bitmap for free pages

// Process list and PCB related constants
#define PCB_SIZE (3) // Number of fields in a PCB
#define PID_PCB (0)  // Holds the pid for a process
#define PC_PCB (1)   // Value of the program counter for the process
#define PTBR_PCB (2) // Page table base register for the process

#define CODE_SIZE (2)      // Number of pages for the code segment
#define HEAP_INIT_SIZE (2) // Number of pages for the heap segment initially

bool running = true;

typedef void (*op_ex_f)(uint16_t i);
typedef void (*trp_ex_f)();

enum
{
  trp_offset = 0x20
};
enum regist
{
  R0 = 0,
  R1,
  R2,
  R3,
  R4,
  R5,
  R6,
  R7,
  RPC,
  RCND,
  PTBR,
  RCNT
};
enum flags
{
  FP = 1 << 0,
  FZ = 1 << 1,
  FN = 1 << 2
};

uint16_t mem[UINT16_MAX] = {0};
uint16_t reg[RCNT] = {0};
uint16_t PC_START = 0x3000;

void initOS();
int createProc(char *fname, char *hname);
void loadProc(uint16_t pid);
uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write); // Can use 'bool' instead
int freeMem(uint16_t ptr, uint16_t ptbr);
static inline uint16_t mr(uint16_t address);
static inline void mw(uint16_t address, uint16_t val);
static inline void tbrk();
static inline void thalt();
static inline void tyld();
static inline void trap(uint16_t i);

static inline uint16_t sext(uint16_t n, int b) { return ((n >> (b - 1)) & 1) ? (n | (0xFFFF << b)) : n; }
static inline void uf(enum regist r)
{
  if (reg[r] == 0)
    reg[RCND] = FZ;
  else if (reg[r] >> 15)
    reg[RCND] = FN;
  else
    reg[RCND] = FP;
}
static inline void add(uint16_t i)
{
  reg[DR(i)] = reg[SR1(i)] + (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]);
  uf(DR(i));
}
static inline void and (uint16_t i)
{
  reg[DR(i)] = reg[SR1(i)] & (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]);
  uf(DR(i));
}
static inline void ldi(uint16_t i)
{
  reg[DR(i)] = mr(mr(reg[RPC] + POFF9(i)));
  uf(DR(i));
}
static inline void not(uint16_t i)
{
  reg[DR(i)] = ~reg[SR1(i)];
  uf(DR(i));
}
static inline void br(uint16_t i)
{
  if (reg[RCND] & FCND(i))
  {
    reg[RPC] += POFF9(i);
  }
}
static inline void jsr(uint16_t i)
{
  reg[R7] = reg[RPC];
  reg[RPC] = (FL(i)) ? reg[RPC] + POFF11(i) : reg[BR(i)];
}
static inline void jmp(uint16_t i) { reg[RPC] = reg[BR(i)]; }
static inline void ld(uint16_t i)
{
  reg[DR(i)] = mr(reg[RPC] + POFF9(i));
  uf(DR(i));
}
static inline void ldr(uint16_t i)
{
  reg[DR(i)] = mr(reg[SR1(i)] + POFF(i));
  uf(DR(i));
}
static inline void lea(uint16_t i)
{
  reg[DR(i)] = reg[RPC] + POFF9(i);
  uf(DR(i));
}
static inline void st(uint16_t i) { mw(reg[RPC] + POFF9(i), reg[DR(i)]); }
static inline void sti(uint16_t i) { mw(mr(reg[RPC] + POFF9(i)), reg[DR(i)]); }
static inline void str(uint16_t i) { mw(reg[SR1(i)] + POFF(i), reg[DR(i)]); }
static inline void rti(uint16_t i) {} // unused
static inline void res(uint16_t i) {} // unused
static inline void tgetc() { reg[R0] = getchar(); }
static inline void tout() { fprintf(stdout, "%c", (char)reg[R0]); }
static inline void tputs()
{
  uint16_t *p = mem + reg[R0];
  while (*p)
  {
    fprintf(stdout, "%c", (char)*p);
    p++;
  }
}
static inline void tin()
{
  reg[R0] = getchar();
  fprintf(stdout, "%c", reg[R0]);
}
static inline void tputsp() { /* Not Implemented */ }
static inline void tinu16() { fscanf(stdin, "%hu", &reg[R0]); }
static inline void toutu16() { fprintf(stdout, "%hu\n", reg[R0]); }

trp_ex_f trp_ex[10] = {tgetc, tout, tputs, tin, tputsp, thalt, tinu16, toutu16, tyld, tbrk};
static inline void trap(uint16_t i) { trp_ex[TRP(i) - trp_offset](); }
op_ex_f op_ex[NOPS] = {/*0*/ br, add, ld, st, jsr, and, ldr, str, rti, not, ldi, sti, jmp, res, lea, trap};

/**
 * Load an image file into memory.
 * @param fname the name of the file to load
 * @param offsets the offsets into memory to load the file
 * @param size the size of the file to load
 */
void ld_img(char *fname, uint16_t *offsets, uint16_t size)
{
  FILE *in = fopen(fname, "rb");
  if (NULL == in)
  {
    fprintf(stderr, "Cannot open file %s.\n", fname);
    exit(1);
  }

  for (uint16_t s = 0; s < size; s += PAGE_SIZE)
  {
    uint16_t *p = mem + offsets[s / PAGE_SIZE];
    uint16_t writeSize = (size - s) > PAGE_SIZE ? PAGE_SIZE : (size - s);
    fread(p, sizeof(uint16_t), (writeSize), in);
  }

  fclose(in);
}

void run(char *code, char *heap)
{
  while (running)
  {
    uint16_t i = mr(reg[RPC]++);
    op_ex[OPC(i)](i);
  }
}

// YOUR CODE STARTS HERE

// baris.pome - CS307 - PA4 - 31311

// I realized that i can implement some helper functions to make the code more readable and efficient
// Helper Functions

// page_table_entry Operations

// This function creates a page table entry (page_table_entry) with the given frame number, read permission, and write permission.
static inline uint16_t create_page_table_entry(uint16_t frame_num, bool read, bool write)
{
  uint16_t page_table_entry = (frame_num << 11) | 0x0001; // Valid bit
  if (read)
    page_table_entry |= 0x0002; // Read bit
  if (write)
    page_table_entry |= 0x0004; // Write bit
  return page_table_entry;
}

// This function checks if a page table entry (page_table_entry) is valid.
static inline bool is_page_valid(uint16_t page_table_entry)
{
  return page_table_entry & 0x0001;
}

// This function checks if a page table entry (page_table_entry) has read permission.
static inline bool has_read_permission(uint16_t page_table_entry)
{
  return page_table_entry & 0x0002;
}

// This function checks if a page table entry (page_table_entry) has write permission.
static inline bool has_write_permission(uint16_t page_table_entry)
{
  return page_table_entry & 0x0004;
}

// This function extracts the frame number from a page table entry (page_table_entry).
static inline uint16_t get_frame_number(uint16_t page_table_entry)
{
  return page_table_entry >> 11;
}

// Bitmap Operations

// This function sets a frame as used in the free bitmap.
static inline void set_frame_used(uint16_t frame_num)
{
  if (frame_num < 16)
  {
    mem[OS_FREE_BITMAP] &= ~(1 << (15 - frame_num));
  }
  else
  {
    mem[OS_FREE_BITMAP + 1] &= ~(1 << (31 - frame_num));
  }
}

// This function sets a frame as free in the free bitmap.
static inline void set_frame_free(uint16_t frame_num)
{
  if (frame_num < 16)
  {
    mem[OS_FREE_BITMAP] |= (1 << (15 - frame_num));
  }
  else
  {
    mem[OS_FREE_BITMAP + 1] |= (1 << (31 - frame_num));
  }
}

// This function checks if a frame is free in the free bitmap.
static inline bool is_frame_free(uint16_t frame_num)
{
  uint16_t bitmap_region;
  uint16_t bit_position;

  if (frame_num < 16)
  {
    bitmap_region = mem[OS_FREE_BITMAP];
    bit_position = 15 - frame_num;
  }
  else
  {
    bitmap_region = mem[OS_FREE_BITMAP + 1];
    bit_position = 31 - frame_num;
  }

  return bitmap_region & (1 << bit_position);
}

// PCB Operations

// This function gets the base address of a PCB for a given process ID.
static inline uint16_t get_pcb_base(uint16_t pid)
{
  return 12 + (pid * PCB_SIZE);
}

// This function gets the base address of a page table for a given process ID.
static inline uint16_t get_page_table_base(uint16_t pid)
{
  return 4096 + (pid * 64);
}

// This function checks if a process is terminated by checking the PID_PCB field in the PCB.
static inline bool is_process_terminated(uint16_t pid)
{
  return mem[get_pcb_base(pid) + PID_PCB] == 0xffff;
}

// Address Translation

// This function gets the virtual page number from an address.
static inline uint16_t get_virtual_page_number(uint16_t address)
{
  return address >> 11;
}

// This function gets the page offset from an address.
static inline uint16_t get_page_offset(uint16_t address)
{
  return address & 0x07FF;
}

// This function gets the physical address from a frame number and an offset.
static inline uint16_t get_physical_address(uint16_t frame_num, uint16_t offset)
{
  return (frame_num << 11) | offset;
}

// End of Helper Functions

// the function that initializes the OS
void initOS()
{
  // bitmaps
  mem[OS_FREE_BITMAP + 1] = 0xFFFF;
  mem[OS_FREE_BITMAP] = 0x1FFF;

  // status registers
  mem[OS_STATUS] = 0x0000;
  mem[Cur_Proc_ID] = 0xffff;
  mem[Proc_Count] = 0;
}

// Process Creation

// This function creates a new process by allocating memory for its code and heap segments.
int createProc(char *fname, char *hname)
{
  // Verify if the OS memory region is full
  if (mem[OS_STATUS] & 0x0001)
  {
    printf("The OS memory region is full. Cannot create a new PCB.\n");
    return 0;
  }

  uint16_t process_id = mem[Proc_Count];
  uint16_t page_table_base = get_page_table_base(process_id);
  uint16_t pcb_base = get_pcb_base(process_id);

  // Initialize the Process Control Block (PCB)
  mem[pcb_base + PID_PCB] = process_id;

  // set program counter to start address
  mem[pcb_base + PC_PCB] = PC_START;

  // set page table base register to page table base
  mem[pcb_base + PTBR_PCB] = page_table_base;

  // Allocate memory for the code segment
  uint16_t code_frame_addresses[CODE_SIZE];
  for (int idx = 0; idx < CODE_SIZE; idx++)
  {
    uint16_t virtual_page_number = idx + 6;
    if (allocMem(page_table_base, virtual_page_number, 0xFFFF, 0) == 0)
    {
      printf("Cannot create code segment.\n");
      // rollback code segment
      for (int rollback = 0; rollback < idx; rollback++)
      {
        freeMem(rollback + 6, page_table_base);
      }
      return 0;
    }
    uint16_t page_table_entry = mem[page_table_base + virtual_page_number];
    uint16_t frame_number = get_frame_number(page_table_entry);
    code_frame_addresses[idx] = get_physical_address(frame_number, 0);
  }

  // Load the code segment from the file
  ld_img(fname, code_frame_addresses, CODE_SIZE * PAGE_SIZE);

  // Allocate memory for the heap segment
  uint16_t heap_frame_addresses[HEAP_INIT_SIZE];
  for (int idx = 0; idx < HEAP_INIT_SIZE; idx++)
  {
    uint16_t heap_virtual_page = idx + 8;                              // get virtual page number
    if (!allocMem(page_table_base, heap_virtual_page, 0xFFFF, 0xFFFF)) // if allocation fails
    {
      printf("Failed to allocate memory for the heap segment.\n");
      for (int rollback_code = 0; rollback_code < CODE_SIZE; rollback_code++) // rollback code segment
      {
        freeMem(rollback_code + 6, page_table_base);
      }
      for (int rollback_heap = 0; rollback_heap < idx; rollback_heap++) // rollback heap segment
      {
        freeMem(rollback_heap + 8, page_table_base);
      }
      return 0;
    }
    // if allocation succeeds
    uint16_t page_table_entry = mem[page_table_base + heap_virtual_page];
    // get frame number
    uint16_t frame_number = get_frame_number(page_table_entry);
    // get physical address
    heap_frame_addresses[idx] = get_physical_address(frame_number, 0);
  }

  // Load the heap segment from the file
  ld_img(hname, heap_frame_addresses, HEAP_INIT_SIZE * PAGE_SIZE);

  // Increment the process count
  mem[Proc_Count]++;
  return 1;
}

// This function loads a process into the CPU registers and sets up the current process ID.
void loadProc(uint16_t pid)
{
  // Set the current process ID
  mem[Cur_Proc_ID] = pid;

  // Get the PCB base address for the current process
  uint16_t pcb_base = get_pcb_base(pid);

  // Load the program counter and page table base register
  reg[RPC] = mem[pcb_base + PC_PCB];

  // get page table base register
  reg[PTBR] = mem[pcb_base + PTBR_PCB];
}

// Memory Allocation

// This function allocates memory for a given virtual page number (VPN) in a page table (PTBR).
uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write)
{
  // Check if page is already allocated
  if (is_page_valid(mem[ptbr + vpn]))
  {
    return 0;
  }

  // Find free frame
  uint16_t current_pfn = 3;
  while (current_pfn < 32)
  {
    if (is_frame_free(current_pfn))
    {
      set_frame_used(current_pfn);
      break;
    }
    current_pfn++;
  }

  if (current_pfn >= 32)
  {
    return 0;
  }

  // Create and store page_table_entry
  mem[ptbr + vpn] = create_page_table_entry(current_pfn, read == 0xFFFF, write == 0xFFFF);
  return 1;
}

// This function frees a page in a page table (PTBR) by invalidating the page_table_entry and setting the frame as free.
int freeMem(uint16_t vpn, uint16_t ptbr)
{
  uint16_t page_table_entry = mem[ptbr + vpn]; // get page_table_entry

  if (!is_page_valid(page_table_entry)) // if page_table_entry is not valid
  {
    return 0;
  }

  uint16_t frame_number = get_frame_number(page_table_entry); // get frame number
  set_frame_free(frame_number);                               // set frame as free

  // Invalidate the page_table_entry
  mem[ptbr + vpn] &= ~0x0001; // invalidate page_table_entry
  return 1;
}

// Instructions to implement
static inline void tbrk()
{
  // Extract R0 value and determine parameters
  uint16_t reg_r0_value = reg[R0];
  uint16_t virtual_page_number = get_virtual_page_number(reg_r0_value); // get virtual page number
  uint16_t allocation_flag = reg_r0_value & 0x0001;                     // get allocation flag
  uint16_t read_permission;                                             // read permission
  // get read permission
  if ((reg_r0_value >> 1) & 0x0001)
  {
    read_permission = 0xFFFF;
  }
  else
  {
    read_permission = 0;
  }
  uint16_t write_permission; // write permission
  // get write permission
  if ((reg_r0_value >> 2) & 0x0001)
  {
    write_permission = 0xFFFF;
  }
  else
  {
    write_permission = 0;
  }

  uint16_t current_pid = mem[Cur_Proc_ID];                          // get current pid
  uint16_t page_table_entry = mem[reg[PTBR] + virtual_page_number]; // get page_table_entry

  if (allocation_flag) // if allocation flag is true
  {
    // Handle heap allocation
    printf("Heap increase requested by process %d.\n", current_pid);
    if (is_page_valid(page_table_entry)) // if page_table_entry is valid
    {
      printf("Cannot allocate memory for page %d of pid %d since it is already allocated.\n",
             virtual_page_number, current_pid);
      return;
    }
    if (!allocMem(reg[PTBR], virtual_page_number, read_permission, write_permission)) // if allocation fails
    {
      printf("Cannot allocate more space for pid %d since there is no free page frames.\n",
             current_pid);
    }
  }
  else
  {
    // Handle heap deallocation
    printf("Heap decrease requested by process %d.\n", current_pid);
    if (!is_page_valid(page_table_entry)) // if page_table_entry is not valid
    {
      printf("Cannot free memory of page %d of pid %d since it is not allocated.\n",
             virtual_page_number, current_pid);
      return;
    }
    freeMem(virtual_page_number, reg[PTBR]); // free memory
  }
}

// Process Switching

// This function saves the current process state and loads the next process.
static inline void tyld()
{
  uint16_t current_pid = mem[Cur_Proc_ID];       // get current pid
  uint16_t pcb_base = get_pcb_base(current_pid); // get pcb base

  // Save current process state
  mem[pcb_base + PC_PCB] = reg[RPC];
  mem[pcb_base + PTBR_PCB] = reg[PTBR];

  // Find next non-terminated process
  uint16_t next_pid = (current_pid + 1) % mem[Proc_Count]; // get next pid
  while (next_pid != current_pid)                          // while next pid is not current pid
  {
    if (!is_process_terminated(next_pid)) // if next pid is not terminated
    {
      break;
    }
    next_pid = (next_pid + 1) % mem[Proc_Count]; // increment next pid
  }

  // Log process switching if applicable
  if (current_pid != next_pid)
  {
    printf("We are switching from process %d to %d.\n", current_pid, next_pid); // log process switching
  }

  // Load the next process
  loadProc(next_pid);
}

// Instructions to modify

// This function halts a process by freeing all its allocated pages and marking it as terminated.
static inline void thalt()
{
  uint16_t current_pid = mem[Cur_Proc_ID];                     // get current pid
  uint16_t pcb_base = get_pcb_base(current_pid);               // get pcb base
  uint16_t page_table_base = get_page_table_base(current_pid); // get page table base

  // Free all allocated pages
  for (int vpn = 6; vpn < 32; vpn++)
  {
    if (is_page_valid(mem[page_table_base + vpn])) // if page_table_entry is valid
    {
      freeMem(vpn, page_table_base); // free memory
    }
  }

  // Mark process as terminated
  mem[pcb_base + PID_PCB] = 0xffff;

  // Find next runnable process
  uint16_t next_pid = (current_pid + 1) % mem[Proc_Count]; // get next pid
  while (next_pid != current_pid)                          // while next pid is not current pid
  {
    if (!is_process_terminated(next_pid)) // if next pid is not terminated
    {
      loadProc(next_pid); // load process
      return;
    }
    next_pid = (next_pid + 1) % mem[Proc_Count]; // increment next pid
  }

  running = false; // set running to false
}

// Memory Read

// This function reads a value from a given address by accessing the physical memory.
static inline uint16_t mr(uint16_t address)
{
  uint16_t vpn = get_virtual_page_number(address); // get virtual page number
  uint16_t offset = get_page_offset(address);      // get page offset

  if (vpn < 6)
  {
    printf("Segmentation fault.\n");
    exit(1);
  }

  uint16_t page_table_entry = mem[reg[PTBR] + vpn];

  if (!is_page_valid(page_table_entry)) // if page_table_entry is not valid
  {
    printf("Segmentation fault inside free space.\n");
    exit(1);
  }

  if (!has_read_permission(page_table_entry)) // if page_table_entry does not have read permission
  {
    printf("Cannot read from a write-only page.\n");
    exit(1);
  }

  uint16_t frame_number = get_frame_number(page_table_entry); // get frame number
  return mem[get_physical_address(frame_number, offset)];     // return value at physical address
}

// Memory Write

// This function writes a value to a given address by accessing the physical memory.
static inline void mw(uint16_t address, uint16_t val)
{
  uint16_t vpn = get_virtual_page_number(address); // get virtual page number
  uint16_t offset = get_page_offset(address);      // get page offset

  // check if address is in the code segment
  if (vpn < 6)
  {
    printf("Segmentation fault.\n");
    exit(1);
  }

  uint16_t page_table_entry = mem[reg[PTBR] + vpn];

  if (!is_page_valid(page_table_entry)) // if page_table_entry is not valid
  {
    printf("Segmentation fault inside free space.\n");
    exit(1);
  }

  if (!has_write_permission(page_table_entry)) // if page_table_entry does not have write permission
  {
    printf("Cannot write to a read-only page.\n");
    exit(1);
  }

  uint16_t frame_number = get_frame_number(page_table_entry); // get frame number
  mem[get_physical_address(frame_number, offset)] = val;      // write value to physical address
}

// YOUR CODE ENDS HERE
