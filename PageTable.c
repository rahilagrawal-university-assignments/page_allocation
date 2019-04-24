// PageTable.c ... implementation of Page Table operations
// COMP1521 17s2 Assignment 2
// Written by John Shepherd, September 2017

#include <stdlib.h>
#include <stdio.h>
#include "Memory.h"
#include "Stats.h"
#include "PageTable.h"

// Symbolic constants

#define NOT_USED 0
#define IN_MEMORY 1
#define ON_DISK 2

// PTE = Page Table Entry

typedef struct {
        char status; // NOT_USED, IN_MEMORY, ON_DISK
        char modified; // boolean: changed since loaded
        int frame;   // memory frame holding this page
        int accessTime; // clock tick for last access
        int loadTime; // clock tick for last time loaded
        int nPeeks;  // total number times this page read
        int nPokes;  // total number times this page modified
        int next; // pointer to next page in the list
        int prev; // pointer to previous page in the list
} PTE;

// The virtual address space of the process is managed
//  by an array of Page Table Entries (PTEs)
// The Page Table is not directly accessible outside
//  this file (hence the static declaration)

static PTE *PageTable;      // array of page table entries
static int nPages;          // # entries in page table
static int replacePolicy;   // how to do page replacement
static int replace_head;        // index of first PTE in list
static int replace_tail;        // index of last PTE in list

// Forward refs for private functions

static int findVictim(int);

// initPageTable: create/initialise Page Table data structures

void initPageTable(int policy, int np)
{
        PageTable = malloc(np * sizeof(PTE));
        if (PageTable == NULL) {
                fprintf(stderr, "Can't initialise Memory\n");
                exit(EXIT_FAILURE);
        }
        replacePolicy = policy;
        nPages = np;
        replace_head = 0;
        replace_tail = nPages-1;
        for (int i = 0; i < nPages; i++) {
                PTE *p = &PageTable[i];
                p->status = NOT_USED;
                p->modified = 0;
                p->frame = NONE;
                p->accessTime = NONE;
                p->loadTime = NONE;
                p->nPeeks = p->nPokes = 0;
        }
}

// requestPage: request access to page pno in mode
// returns memory frame holding this page
// page may have to be loaded
// PTE(status,modified,frame,accessTime,nextPage,nPeeks,nWrites)

int requestPage(int pno, char mode, int time)
{
        if (pno < 0 || pno >= nPages) {
                fprintf(stderr,"Invalid page reference\n");
                exit(EXIT_FAILURE);
        }
        PTE *p = &PageTable[pno];
        int fno; // frame number
        switch (p->status) {
        case NOT_USED:
        case ON_DISK:
                countPageFault();
                fno = findFreeFrame();
                if (fno == NONE) {
                        int vno = findVictim(time);
#ifdef DBUG
                        printf("Evict page %d\n",vno);
#endif
                        // if victim page modified, save its frame
                        PTE *victim_page = &PageTable[vno];
                        if(victim_page->modified) {
                                saveFrame(victim_page->frame);
                        }
                        // collect frame# (fno) for victim page
                        fno = victim_page->frame;
                        // update PTE for victim page
                        // - new status
                        victim_page->status = ON_DISK;
                        // - no longer modified
                        victim_page->modified = 0;
                        // - no frame   mapping
                        victim_page->frame = NONE;
                        // - not accessed, not loaded
                        victim_page->accessTime = NONE;
                        victim_page->loadTime = NONE;
                }
                #ifdef DBUG
                    printf("Page %d given frame %d\n",pno,fno);
                #endif
                // load page pno into frame fno
                PTE *loadpage = &PageTable[pno];
                loadFrame(fno, pno, time);
                // update PTE for page
                // - new status
                loadpage->status = IN_MEMORY;
                // - not yet modified
                loadpage->modified = 0;
                // - associated with frame fno
                loadpage->frame = fno;
                // - just loaded
                loadpage->loadTime = time;
                if (time == 0) {
                        loadpage->prev = -1;
                        replace_head = pno;
                        replace_tail = pno;
                }
                //Make the newly added page the last page in the list
                //Same for both LRU and FIFO as load time is the current time
                p->prev = replace_tail;
                //If this is the first page to be added, set previous pointer as -1
                if (time==0){
                    p->prev = -1;
                }
                (&PageTable[replace_tail])->next = pno;
                replace_tail = pno;
                p->next = -1;
                break;
        case IN_MEMORY:
                //Update list if the replacement policy is LRU
                //No changes needed for FIFO as it only depends on load time, not access time.
                if (replacePolicy == REPL_LRU){
                    int prev_index = (&PageTable[pno])->prev;
                    int next_index = (&PageTable[pno])->next;
                    //First node and/or only node
                    if (prev_index==-1){
                        //First page is the only page
                        if(next_index == -1){
                            //First page remains start and end of list, do nothing else
                            replace_tail = replace_head = pno;
                            countPageHit();
                            break;
                        }
                        //First page is not the only page
                        //Set next page as the first page
                        else{
                            PTE *nextpage = &PageTable[next_index];
                            nextpage->prev = -1;
                            replace_head = next_index;
                        }
                    }
                    //If the page that was accessed is not the first page
                    else {
                        //If the last page was accessed, change nothing
                        if(next_index == -1){
                            countPageHit();
                            break;
                        }
                        ////Link the pages before and after the page that was accessed
                        PTE *prevpage = &PageTable[prev_index];
                        prevpage->next = p->next;
                        PTE *nextpage = &PageTable[next_index];
                        nextpage->prev = p->prev;
                    }
                    //Set the accessed page as the last page in the list
                    //because this page is the most recently accessed page now
                    p->prev = replace_tail;
                    (&PageTable[replace_tail])->next = pno;
                    replace_tail = pno;
                    p->next = -1;
                }
                //Increase number of hits regardless of replacement policy
                countPageHit();
                break;
        default:
                fprintf(stderr,"Invalid page status\n");
                exit(EXIT_FAILURE);
        }
        if (mode == 'r')
                p->nPeeks++;
        else if (mode == 'w') {
                p->nPokes++;
                p->modified = 1;
        }
        p->accessTime = time;
        return p->frame;
}

// findVictim: returns victim = first page in list, sets new first page as the previous second page.
// uses the configured replacement policy
static int findVictim(int time)
{
        int victim = 0;
        //Get first PTE in the list
        PTE *page_victim = &PageTable[replace_head];
        //Vicitm is first element in list
        victim = replace_head;
        //Update new first element
        replace_head = page_victim->next;
        (&PageTable[replace_head])->prev = -1;
        switch (replacePolicy) {
        case REPL_LRU:
                break;
        case REPL_FIFO:
                break;
        case REPL_CLOCK:
                return 0;
        }
        return victim;
}

// showPageTableStatus: dump page table
// PTE(status,modified,frame,accessTime,nextPage,nPeeks,nWrites)

void showPageTableStatus(void)
{
        char *s;
        printf("%4s %6s %4s %6s %7s %7s %7s %7s\n",
               "Page","Status","Mod?","Frame","Acc(t)","Load(t)","#Peeks","#Pokes");
        for (int i = 0; i < nPages; i++) {
                PTE *p = &PageTable[i];
                printf("[%02d]", i);
                switch (p->status) {
                case NOT_USED:  s = "-"; break;
                case IN_MEMORY: s = "mem"; break;
                case ON_DISK:   s = "disk"; break;
                }
                printf(" %6s", s);
                printf(" %4s", p->modified ? "yes" : "no");
                if (p->frame == NONE)
                        printf(" %6s", "-");
                else
                        printf(" %6d", p->frame);
                if (p->accessTime == NONE)
                        printf(" %7s", "-");
                else
                        printf(" %7d", p->accessTime);
                if (p->loadTime == NONE)
                        printf(" %7s", "-");
                else
                        printf(" %7d", p->loadTime);
                printf(" %7d", p->nPeeks);
                printf(" %7d", p->nPokes);
                printf("\n");
        }
}
