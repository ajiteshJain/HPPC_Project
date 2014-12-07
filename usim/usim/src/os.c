#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "params.h"
#include "os.h"


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
#define OS_PAGESIZE 4096
#define OS_NUM_RND_TRIES 1
extern int config_param;

OS *os_new(uns64 num_pages, uns num_threads)
{
    OS *os = (OS *) calloc (1, sizeof (OS));

    os->num_pages      = num_pages;
    os->num_threads    = num_threads;
    os->lines_in_page  = OS_PAGESIZE/CACHE_LINE_SIZE;
    os->pt     = (PageTable *) calloc (1, sizeof (PageTable));
    os->pt->entries     = (Hash_Table *) calloc (1, sizeof(Hash_Table));
    init_hash_table(os->pt->entries, "PageTableEntries", 4315027, sizeof( PageTableEntry ));
    os->pt->max_entries = os->num_pages;

    os->ipt     = (InvPageTable *) calloc (1, sizeof (InvPageTable));
    os->ipt->entries = (InvPageTableEntry *) calloc (os->num_pages, sizeof (InvPageTableEntry));
    os->ipt->num_entries = os->num_pages;

    assert(os->pt->entries);
    assert(os->ipt->entries);

    printf("Initialized OS for %llu pages\n", num_pages);

    return os;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns64 os_vpn_to_pfn(OS *os, uns64 vpn, uns tid, Flag *hit)
{
    Flag first_access;
    PageTable *pt = os->pt;
    InvPageTable *ipt = os->ipt;
    PageTableEntry *pte;
    InvPageTableEntry *ipte;
    *hit = TRUE;

    assert(vpn>>ADDRESS_BITS == 0);
    uns64 threadId = tid;
    vpn = (threadId<<ADDRESS_BITS)+vpn; // embed tid information in high bits
    
    if( pt->last_xlation[tid].vpn == vpn ){
	return pt->last_xlation[tid].pfn;
    }
    
    pte = (PageTableEntry *) hash_table_access_create(pt->entries, vpn, &first_access);

    if(first_access){
	pte->pfn = os_get_victim_from_ipt(os, tid);
	ipte = &ipt->entries[ pte->pfn ]; 
	ipte->valid = TRUE;
	ipte->dirty = FALSE;
	ipte->vpn   = vpn;
	assert( (uns)pt->entries->count <= pt->max_entries);
	pt->miss_count++;
	*hit=FALSE;
    }

    ipte = &ipt->entries[ pte->pfn ]; 
    ipte->ref = TRUE;
    
    pt->last_xlation[tid].vpn = vpn;
    pt->last_xlation[tid].pfn = pte->pfn;

    return pte->pfn;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns64    os_get_victim_from_ipt(OS *os, uns tid)
{
    PageTable *pt = os->pt;
    InvPageTable *ipt = os->ipt;
    Flag found=FALSE;
    uns64 victim=0;
    uns random_invalid_tries=OS_NUM_RND_TRIES;
    uns tries=0;
    tries=tries; // To avoid warning
    random_invalid_tries=random_invalid_tries; //To avoid warning

    uns64 numPagesPerThread = os->num_pages / os->num_threads;
    uns64 startPageNumber = tid * numPagesPerThread;
    uns64 ptr = startPageNumber;
    uns64 count = 0; 

   // try random invalid first
    while( tries < random_invalid_tries){
	victim = startPageNumber + rand() % numPagesPerThread;
	if(! ipt->entries[victim].valid ){
	    found = TRUE;
	    break;
	}
	tries++;
    }
    // try finding a victim if no invalid victim
    while(!found){
	  if( ! ipt->entries[ ptr ].valid ){
	    found = TRUE;
	  }
	
	  if( ipt->entries[ ptr ].valid && ipt->entries[ ptr ].ref == FALSE){
	    found = hash_table_access_delete(pt->entries, ipt->entries[ptr].vpn);
	    assert(found);
	  }else{
	    ipt->entries[ptr].ref = FALSE;
	  }
	  victim = ptr;
	  ptr = startPageNumber + (++count) % numPagesPerThread;
    }
    // update page writeback information
    if( ipt->entries[victim].valid){
	pt->total_evicts++;
	if(ipt->entries[victim].dirty ){
	    pt->evicted_dirty_page++;
	}
    }  
    return victim; 
}
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void os_print_stats(OS *os)
{
    char header[256];
    sprintf(header, "OS");
    
    printf("\n\n");
    printf("\n%s_PAGE_MISS       \t : %llu",  header, os->pt->miss_count);
    printf("\n%s_PAGE_EVICTS     \t : %llu",  header, os->pt->total_evicts);
    printf("\n%s_FOOTPRINT       \t : %llu",  header, (os->pt->miss_count*OS_PAGESIZE)/(1024*1024));
    printf("\n");

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Addr os_v2p_lineaddr(OS *os, Addr lineaddr, uns tid){
  uns64 vpn = lineaddr/os->lines_in_page;
  uns lineid = lineaddr%os->lines_in_page;
  Flag pagehit;
  uns64 pfn = os_vpn_to_pfn(os, vpn, tid, &pagehit);
  Addr retval = (pfn*os->lines_in_page)+lineid;
  return retval;
}
