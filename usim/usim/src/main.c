#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>

#include "processor.h"
#include "configfile.h"
#include "memory_controller.h"
#include "scheduler.h"
#include "params.h"
#include "randomize.h"
#include "cache.h"
#include "global_types.h"
#include "os.h"
#define L3_LATENCY 10

#define MAXTRACELINESIZE 64
//Set this for table length of randomizer
#define MAX_TABLE_LEN 1024 
//Set RANDOM_TABLE_ENABLE bit to 1 to enable randomization of addresses
#define RANDOM_TABLE_ENABLE 0
//Notify if it uses virtual or physical address
#define USE_PHY_ADDR 0

long long int BIGNUM = 1000000;


/*********************Variables added by Prashant for stats*********************************/
//Stall Loggers
long long int robf_stalls=0;
long long int wrqf_stalls=0;
long long int robn_stalls=0;
long long int wrqn_stalls=0;

/******************** POWER LOGGERS **************************/
float tot_background=0;
float tot_act=0;
float tot_read=0;
float tot_write=0;
float tot_rdt=0;
float tot_wrt=0;
float tot_ref=0;
float tot_rnk=0;

/******************************************************************************************/

/********************Randomizer init and call**********************************************/
long int **rand_table;

int addr_rand_init(int numcores)
{
int i=0;
int jj=0;
long int kk=0;
long int temp=0;
long int rand_value=0;

//Generate Table
  rand_table=(long int **)malloc(sizeof(long int*)*numcores);
  for(i=0; i<numcores ; i++)
  {
    rand_table[i]=(long int *)malloc(sizeof(long int)*MAX_TABLE_LEN);
  }
  
  //Initialize all values
  for(jj=0; jj<numcores;jj++)
  {
    for(kk=0; kk<MAX_TABLE_LEN; kk++)
    {
      rand_table[jj][kk]=kk;
    }
  }
  
  //Generate Random Values and Swap
  for(jj=0; jj<numcores; jj++)
  {
    for(kk=0; kk<MAX_TABLE_LEN; kk++)
    {
      rand_value=rand()%MAX_TABLE_LEN;
      temp = rand_table[jj][kk];
      rand_table[jj][kk]=rand_table[jj][rand_value];
      rand_table[jj][rand_value]=temp;
    }
  }
  
  //Random Table Ready
  return 1;
}

int addr_randomize(long long int *addr, int numc)
{
  long long int addr_temp=0;
  long int addr_last_bits=0;
  unsigned int offset=0;
  unsigned int columns=0;
  unsigned int channels=0;
  unsigned int tablelen=0;
  long int temp=0;
  //Apply the random addresses to a total of MAX_TABLE_LEN bits with the last bank bit as the LSB
  if(ADDRESS_MAPPING==1)
  {
  offset=log_base2(CACHE_LINE_SIZE);
  columns=log_base2(NUM_COLUMNS);
  channels=log_base2(NUM_CHANNELS);
  tablelen=log_base2(MAX_TABLE_LEN);
  temp=1<<(offset+columns+channels);
  temp=temp-1;
  addr_last_bits=addr[numc] & temp;
  addr_temp=addr[numc]>>(offset+columns+channels);
  temp=1<<tablelen;
  temp=temp-1;
  addr_temp=addr_temp & temp; //Extracting the next MAX_TABLE_LEN bits only
  addr_temp=rand_table[numc][addr_temp]; //Extracted the random address
  addr[numc]=addr[numc] >> (offset+columns+channels+tablelen);
  addr[numc]=addr[numc] << tablelen;
  addr[numc]=addr[numc] | addr_temp; //appended the MAX_TABLE_LEN bits now
  addr[numc]=addr[numc] << (offset+columns+channels);
  addr[numc]=addr[numc] | addr_last_bits; //new address
  }
  else
  {
  offset=log_base2(CACHE_LINE_SIZE);
  columns=log_base2(NUM_COLUMNS);
  channels=log_base2(NUM_CHANNELS);
  tablelen=log_base2(MAX_TABLE_LEN);
  temp=1<<(offset+channels);
  temp=temp-1;
  addr_last_bits=addr[numc] & temp;
  addr_temp=addr[numc]>>(offset+channels);
  temp=1<<tablelen;
  temp=temp-1;
  addr_temp=addr_temp & temp; //Extracting the next MAX_TABLE_LEN bits only
  addr_temp=rand_table[numc][addr_temp]; //Extracted the random address
  addr[numc]=addr[numc] >> (offset+channels+tablelen);
  addr[numc]=addr[numc] << tablelen;
  addr[numc]=addr[numc] | addr_temp; //appended the MAX_TABLE_LEN bits now
  addr[numc]=addr[numc] << (offset+channels);
  addr[numc]=addr[numc] | addr_last_bits; //new address
  }
  return 1; //New address is generated
}

//Free up used memory
int free_rand(int numcores)
{
  int i;
  for(i=0; i<numcores ; i++)
  {
    free(rand_table[i]);
  }
  free(rand_table);
  return 1;
}
/***********************************************************************************************/



int expt_done=0;  
int config_param=0;
long long int CYCLE_VAL=0;

long long int get_current_cycle()
{
  return CYCLE_VAL;
}

struct robstructure *ROB;
LLCache *L3Cache;
int LAT_6EC7ED;

FILE **tif=NULL;  /* The handles to the trace input files. */
FILE *config_file=NULL;
FILE *vi_file=NULL;

int *prefixtable;
// Moved the following to memory_controller.h so that they are visible
// from the scheduler.
//long long int *committed;
//long long int *fetched;
long long int *time_done;
long long int total_time_done;
float core_power=0;

int main(int argc, char * argv[])
{
  
  printf("---------------------------------------------\n");
  printf("-- USIMM: the Utah SImulated Memory Module --\n");
  printf("--              Version: 1.3               --\n");
  printf("---------------------------------------------\n");
  
  int numc=0;
  int num_ret=0;
  int num_fetch=0;
  int num_done=0;
  int numchwr=0;
  /*********** Adding Read and Write Queues into USIMM ******/
  int writeqfull=0;
  /**********************************************************/
  int fnstart;
  int currMTapp;
  long long int maxtd;
  int maxcr;
  int pow_of_2_cores;
  char newstr[MAXTRACELINESIZE];
  int *nonmemops;
  char *opertype;
  long long int *addr;
  long long int *instrpc;
  int chips_per_rank=-1;
  long long int total_inst_fetched = 0;
  int fragments=1;
  //Variable to check if Physical Addresses are being used as input
//int use_phy_addr=USE_PHY_ADDR;
  unsigned long long int phy_addr=0;
  unsigned long long int os_pages = 0;
  OS *os;
  //To keep track of how much is done
  unsigned long long int inst_comp=0;
  unsigned long int Mcount=0;
  /* Initialization code. */
  printf("Initializing.\n");
  
  

 // added by choucc 
  // add an argument for refreshing selection
  if (argc < 4) {
    printf("Need at least one input configuration file and one trace file as argument.  Quitting.\n");
    return -3;
  }

  config_param = atoi(argv[1]);


  config_file = fopen(argv[2], "r");
  if (!config_file) {
    printf("Missing system configuration file.  Quitting. \n");
    return -4;
  }

  NUMCORES = argc-3;
  L3Cache = (LLCache*)malloc(sizeof(LLCache));

  ROB = (struct robstructure *)malloc(sizeof(struct robstructure)*NUMCORES);
  tif = (FILE **)malloc(sizeof(FILE *)*NUMCORES);
  committed = (long long int *)malloc(sizeof(long long int)*NUMCORES);
  fetched = (long long int *)malloc(sizeof(long long int)*NUMCORES);
  time_done = (long long int *)malloc(sizeof(long long int)*NUMCORES);
  nonmemops = (int *)malloc(sizeof(int)*NUMCORES);
  opertype = (char *)malloc(sizeof(char)*NUMCORES);
  addr = (long long int *)malloc(sizeof(long long int)*NUMCORES);
  instrpc = (long long int *)malloc(sizeof(long long int)*NUMCORES);
  prefixtable = (int *)malloc(sizeof(int)*NUMCORES);
  currMTapp = -1;
  // added by choucc 
  // add an argument for refreshing selection
  for (numc=0; numc < NUMCORES; numc++) {
     tif[numc] = fopen(argv[numc+3], "r");
     if (!tif[numc]) {
       printf("Missing input trace file %d.  Quitting. \n",numc);
       return -5;
     }

     /* The addresses in each trace are given a prefix that equals
        their core ID.  If the input trace starts with "MT", it is
	assumed to be part of a multi-threaded app.  The addresses
	from this trace file are given a prefix that equals that of
	the last seen input trace file that starts with "MT0".  For
	example, the following is an acceptable set of inputs for
	multi-threaded apps CG (4 threads) and LU (2 threads):
	usimm 1channel.cfg MT0CG MT1CG MT2CG MT3CG MT0LU MT1LU */
     prefixtable[numc] = numc;

     /* Find the start of the filename.  It's after the last "/". */
     for (fnstart = strlen(argv[numc+3]) ; fnstart >= 0; fnstart--) {
       if (argv[numc+3][fnstart] == '/') {
         break;
       }
     }
     fnstart++;  /* fnstart is either the letter after the last / or the 0th letter. */

     if ((strlen(argv[numc+3])-fnstart) > 2) {
       if ((argv[numc+3][fnstart+0] == 'M') && (argv[numc+3][fnstart+1] == 'T')) {
         if (argv[numc+3][fnstart+2] == '0') {
	   currMTapp = numc;
	 }
	 else {
	   if (currMTapp < 0) {
	     printf("Poor set of input parameters.  Input file %s starts with \"MT\", but there is no preceding input file starting with \"MT0\".  Quitting.\n", argv[numc+3]);
	     return -6;
	   }
	   else 
	     prefixtable[numc] = currMTapp;
	 }
       }
     }
     printf("Core %d: Input trace file %s : Addresses will have prefix %d\n", numc, argv[numc+3], prefixtable[numc]);

     committed[numc]=0;
     fetched[numc]=0;
     time_done[numc]=0;
     ROB[numc].head=0;
     ROB[numc].tail=0;
     ROB[numc].inflight=0;
     ROB[numc].tracedone=0;
  }

  read_config_file(config_file);


  	vi_file = fopen("../input/8Gb_x8.vi", "r");
	chips_per_rank= 8;
  	printf("Reading vi file: 8Gb_x8.vi\t\n%d Chips per Rank\n",chips_per_rank);

  	if (!vi_file) {
 	  printf("Missing DRAM chip parameter file.  Quitting. \n");
  	  return -5;
  	}


  assert((log_base2(NUM_CHANNELS) + log_base2(NUM_RANKS) + log_base2(NUM_BANKS) + log_base2(NUM_ROWS) + log_base2(NUM_COLUMNS) + log_base2(CACHE_LINE_SIZE)) == ADDRESS_BITS );
  if (NUMCORES == 1) {
    pow_of_2_cores = 1;
  }
  else {
  pow_of_2_cores = 1 << ((int)log_base2(NUMCORES-1) + 1);
  }
  read_config_file(vi_file);
  fragments=1;
  T_RFC=T_RFC/fragments;
  
  printf("Fragments: %d of length %d\n",fragments, T_RFC);

  print_params();

  for(int i=0; i<NUMCORES; i++)
  {
	  ROB[i].comptime = (long long int*)malloc(sizeof(long long int)*ROBSIZE);
	  ROB[i].mem_address = (long long int*)malloc(sizeof(long long int)*ROBSIZE);
	  ROB[i].instrpc = (long long int*)malloc(sizeof(long long int)*ROBSIZE);
	  ROB[i].optype = (int*)malloc(sizeof(int)*ROBSIZE);
  }
  CACHE_SIZE = 1024;// LLC (SHARED)

  construct_cache(L3Cache, CACHE_SIZE, 8, NUMCORES, 64);
  os_pages = ((((unsigned long long int)1)<<ADDRESS_BITS)>>12); //4KB page size and covering the address space
  init_memory_controller_vars();
  init_scheduler_vars();
  os = os_new(os_pages,NUMCORES);

  
/*********************************** ALL INITIALIZATIONS ARE DONE UNTIL HERE ****************************************/
  /* Must start by reading one line of each trace file. */
  for(numc=0; numc<NUMCORES; numc++)
  {
	      if (fgets(newstr,MAXTRACELINESIZE,tif[numc])) {
		inst_comp++;
	        if (sscanf(newstr,"%d %c",&nonmemops[numc],&opertype[numc]) > 0) {
		  if (opertype[numc] == 'R') {
		    if (sscanf(newstr,"%d %c %Lx %Lx",&nonmemops[numc],&opertype[numc],&addr[numc],&instrpc[numc]) < 1) {
		      printf("Panic.  Poor trace format.\n");
		      return -4;
		    }
		  }
		  else {
		    if (opertype[numc] == 'W') {
		      if (sscanf(newstr,"%d %c %Lx",&nonmemops[numc],&opertype[numc],&addr[numc]) < 1) {
		        printf("Panic.  Poor trace format.\n");
		        return -3;
		      }
		    }
		    else {
		      printf("Panic.  Poor trace format.\n");
		      return -2;
		    }
		  }
		}
		else {
		  printf("Panic.  Poor trace format.\n");
		  return -1;
		}
                if(config_param==0) // PARSEC
                addr[numc]=addr[numc]>>6; //As the cache line is 64 bytes

                phy_addr=os_v2p_lineaddr(os,addr[numc],numc);
                addr[numc]=phy_addr;
                addr[numc]=addr[numc]<<6; //As the cache line is 64 bytes for parsec and spec

                inst_comp=nonmemops[numc]+inst_comp;
	      }
	      else {
	        if (ROB[numc].inflight == 0) {
	          num_done++;
	          if (!time_done[numc]) time_done[numc] = 1;
	        }
	        ROB[numc].tracedone=1;
	      }
  }

/**************Initialize the random value table*************************/
  addr_rand_init(NUMCORES);
/************************************************************************/
  printf("Starting simulation.\n");
  while (!expt_done) {
    /* For each core, retire instructions if they have finished. */
    for (numc = 0; numc < NUMCORES; numc++) {
      num_ret = 0;
      while ((num_ret < MAX_RETIRE) && ROB[numc].inflight) {
        /* Keep retiring until retire width is consumed or ROB is empty. */
        if (ROB[numc].comptime[ROB[numc].head] < CYCLE_VAL) {  
	  /* Keep retiring instructions if they are done. */
	  ROB[numc].head = (ROB[numc].head + 1) % ROBSIZE;
	  ROB[numc].inflight--;
	  committed[numc]++;
	  num_ret++;
        }
	else  /* Instruction not complete.  Stop retirement for this core. */
	  break;
      }  /* End of while loop that is retiring instruction for one core. */
    }  /* End of for loop that is retiring instructions for all cores. */

    if(CYCLE_VAL%PROCESSOR_CLK_MULTIPLIER == 0)
    { 
      /* Execute function to find ready instructions. */
      update_memory();

      /* Execute user-provided function to select ready instructions for issue. */
      /* Based on this selection, update DRAM data structures and set 
	 instruction completion times. */
      for(int c=0; c < NUM_CHANNELS; c++)
      {
	schedule(c);
	gather_stats(c);	
      }
    }

    /* For each core, bring in new instructions from the trace file to
       fill up the ROB. */
    num_done = 0;
    writeqfull =0;
    for(int c=0; c<NUM_CHANNELS; c++){
	    if(write_queue_length[c] == WQ_CAPACITY)
	    {
		    writeqfull = 1;
		    break;
	    }
    }

    for (numc = 0; numc < NUMCORES; numc++) {
      if (!ROB[numc].tracedone) { /* Try to fetch if EOF has not been encountered. */
        num_fetch = 0;
        while ((num_fetch < MAX_FETCH) && (ROB[numc].inflight != ROBSIZE) && (!writeqfull)) {
          /* Keep fetching until fetch width or ROB capacity or WriteQ or ReadQ are fully consumed. */
	  /* Read the corresponding trace file and populate the tail of the ROB data structure. */
	  /* If Memop, then populate read/write queue.  Set up completion time. */

	  if (nonmemops[numc]) {  /* Have some non-memory-ops to consume. */
	    ROB[numc].optype[ROB[numc].tail] = 'N';
	    ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL+PIPELINEDEPTH;
	    nonmemops[numc]--;
	    ROB[numc].tail = (ROB[numc].tail +1) % ROBSIZE;
	    ROB[numc].inflight++;
	    fetched[numc]++;
	    num_fetch++;
	  }
	  else { /* Done consuming non-memory-ops.  Must now consume the memory rd or wr. */
	      if (opertype[numc] == 'R') {
	          if(RANDOM_TABLE_ENABLE)
		  {
		    addr_randomize(addr,numc); //Randomize the address
		  }
		  ROB[numc].mem_address[ROB[numc].tail] = addr[numc];
	          ROB[numc].optype[ROB[numc].tail] = opertype[numc];
	          ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL + BIGNUM;
	          ROB[numc].instrpc[ROB[numc].tail] = instrpc[numc];
		  long long int wb_addr = 0;
                  long long int wb_inst_addr = 0;
                  LINE_STATE* currLine = CacheIsWriteback(L3Cache, numc, instrpc[numc], addr[numc], ACCESS_LOAD);
                  if(currLine != NULL){
                     wb_addr = currLine->phy_addr;
                     wb_inst_addr = currLine->PC;
                  }
                  int L3Hit = LookupAndFillCache(L3Cache, numc, instrpc[numc], addr[numc], ACCESS_LOAD);
		
		  // Check to see if the read is for buffered data in write queue - 
		  // return constant latency if match in WQ
		  // add in read queue otherwise
		  int lat = read_matches_write_or_read_queue(addr[numc]);
		  if(L3Hit){
                     ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL+L3_LATENCY+PIPELINEDEPTH;                          
                  }else{
                  if(currLine != NULL){
                     insert_write(wb_addr, CYCLE_VAL, numc, ROB[numc].tail); 
                     //   fprintf(stdout, "  @%lld WB\n", CYCLE_VAL);
                  }
		  // Check to see if the read is for buffered data in write queue - 
		  // return constant latency if match in WQ
		  // add in read queue otherwise

		  if(lat) {
			ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL+lat+PIPELINEDEPTH;
		  }
		  else {
			insert_read(addr[numc], CYCLE_VAL, numc, ROB[numc].tail, instrpc[numc]);
		  }
	        }
	      }else {  /* This must be a 'W'.  We are confirming that while reading the trace. */
	        if (opertype[numc] == 'W') {
		      if(RANDOM_TABLE_ENABLE)
		      {
        		      addr_randomize(addr,numc); //Randomize the address
		      }
		      ROB[numc].mem_address[ROB[numc].tail] = addr[numc];
		      ROB[numc].optype[ROB[numc].tail] = opertype[numc];
		      ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL+PIPELINEDEPTH;
		      /* Also, add this to the write queue. */
                      long long int wb_addr = 0;
                      long long int wb_inst_addr = 0;
                      LINE_STATE* currLine = CacheIsWriteback(L3Cache, numc, instrpc[numc], addr[numc], ACCESS_STORE);
                      if(currLine != NULL){
	                      wb_addr = currLine->phy_addr;
        	              wb_inst_addr = currLine->PC;
                      }
                      int L3Hit = LookupAndFillCache(L3Cache, numc, instrpc[numc], addr[numc], ACCESS_STORE);
                      if(L3Hit){
                      }else{
				if(currLine != NULL){
                             		insert_write(wb_addr, CYCLE_VAL, numc, ROB[numc].tail); 
                                //   fprintf(stdout, "  @%lld WB\n", CYCLE_VAL);
                                }
                                if(!write_exists_in_write_queue(addr[numc])){
                   		        insert_write(addr[numc], CYCLE_VAL, numc, ROB[numc].tail);
                                }
                                }
		      for(int c=0; c<NUM_CHANNELS; c++){
			if(write_queue_length[c] == WQ_CAPACITY)
			{
			  writeqfull = 1;
			  break;
			}
		      }
		}
		else {
		  printf("Panic.  Poor trace format. \n");
		  return -1;
		}
	      }
	      ROB[numc].tail = (ROB[numc].tail +1) % ROBSIZE;
	      ROB[numc].inflight++;
	      fetched[numc]++;
	      num_fetch++;

	      /* Done consuming one line of the trace file.  Read in the next. */
	      if (fgets(newstr,MAXTRACELINESIZE,tif[numc])) {
		inst_comp++;
	        if (sscanf(newstr,"%d %c",&nonmemops[numc],&opertype[numc]) > 0) {
		  if (opertype[numc] == 'R') {
		    if (sscanf(newstr,"%d %c %Lx %Lx",&nonmemops[numc],&opertype[numc],&addr[numc],&instrpc[numc]) < 1) {
		      printf("Panic.  Poor trace format.\n");
		      return -4;
		    }
		  }
		  else {
		    if (opertype[numc] == 'W') {
		      if (sscanf(newstr,"%d %c %Lx",&nonmemops[numc],&opertype[numc],&addr[numc]) < 1) {
		        printf("Panic.  Poor trace format.\n");
		        return -3;
		      }
		    }
		    else {
		      printf("Panic.  Poor trace format.\n");
		      return -2;
		    }
		  }
		}
		else {
		  printf("Panic.  Poor trace format.\n");
		  return -1;
		}
                if(config_param==0) // PARSEC
                addr[numc]=addr[numc]>>6; //As the cache line is 64 bytes

                phy_addr=os_v2p_lineaddr(os,addr[numc],numc);
                addr[numc]=phy_addr;
                addr[numc]=addr[numc]<<6; //As the cache line is 64 bytes for parsec and spec

                inst_comp=nonmemops[numc]+inst_comp;
	
	      }
	      else {
	        if (ROB[numc].inflight == 0) {
	          num_done++;
	          if (!time_done[numc]) time_done[numc] = CYCLE_VAL;
	        }
	        ROB[numc].tracedone=1;
	        break;  /* Break out of the while loop fetching instructions. */
	      }
	      
	  }  /* Done consuming the next rd or wr. */

	} /* One iteration of the fetch while loop done. */
      } /* Closing brace for if(trace not done). */
      else { /* Input trace is done.  Check to see if all inflight instrs have finished. */
        if (ROB[numc].inflight == 0) {
	  num_done++;
	  if (!time_done[numc]) time_done[numc] = CYCLE_VAL;
	}
      }
    } /* End of for loop that goes through all cores. */


    if (num_done == NUMCORES) {
      /* Traces have been consumed and in-flight windows are empty.  Must confirm that write queues have been drained. */
      for (numchwr=0;numchwr<NUM_CHANNELS;numchwr++) {
        if (write_queue_length[numchwr]) break;
      }
      if (numchwr == NUM_CHANNELS) expt_done=1;  /* All traces have been consumed and the write queues are drained. */
    }

    /* Printing details for testing.  Remove later. */
    //printf("Cycle: %lld\n", CYCLE_VAL);
    //for (numc=0; numc < NUMCORES; numc++) {
     // printf("C%d: Inf %d : Hd %d : Tl %d : Comp %lld : type %c : addr %x : TD %d\n", numc, ROB[numc].inflight, ROB[numc].head, ROB[numc].tail, ROB[numc].comptime[ROB[numc].head], ROB[numc].optype[ROB[numc].head], ROB[numc].mem_address[ROB[numc].head], ROB[numc].tracedone);
    //}

    CYCLE_VAL++;  /* Advance the simulation cycle. */
    if(inst_comp>1000000)
    {
        fflush(stdout);
        inst_comp=0;
        Mcount++;
        printf(".");
        if(Mcount>0)
        {
          if(Mcount%100==0)
          {
            printf("\t%lu M",Mcount);
            if(Mcount%1000==0)
            {
              printf("--Billion");
            }
            printf("\n");
          }
        }
    }	
  }


  /* Code to make sure that the write queue drain time is included in
     the execution time of the thread that finishes last. */
  maxtd = time_done[0];
  maxcr = 0;
  for (numc=1; numc < NUMCORES; numc++) {
    if (time_done[numc] > maxtd) {
      maxtd = time_done[numc];
      maxcr = numc;
    }
  }
  time_done[maxcr] = CYCLE_VAL;

  core_power = 0;
  for (numc=0; numc < NUMCORES; numc++) {
    /* A core has peak power of 10 W in a 4-channel config.  Peak power is consumed while the thread is running, else the core is perfectly power gated. */
    core_power = core_power + (10*((float)time_done[numc]/(float)CYCLE_VAL));
  }
  if (NUM_CHANNELS == 1) {
    /* The core is more energy-efficient in our single-channel configuration. */
    core_power = core_power/2.0 ;
  }

  printf("Done with loop. Printing stats.\n");
  printf("Cycles %lld\n", CYCLE_VAL);
  total_time_done = 0;

  for (numc=0; numc < NUMCORES; numc++) {
    printf("Done: Core %d: Fetched %lld : Committed %lld : At time : %lld\n", numc, fetched[numc], committed[numc], time_done[numc]);
    total_time_done += time_done[numc];
    total_inst_fetched = total_inst_fetched + fetched[numc];
  }
  printf("\nUSIMM_CYCLES          \t : %lld\n",total_time_done);
  printf("\nUSIMM_INST            \t : %lld\n",total_inst_fetched);
  printf("\nUSIMM_ROBF_STALLS     \t : %lld\n",robf_stalls);
  printf("\nUSIMM_ROBN_STALLS     \t : %lld\n",robn_stalls);
  printf("\nUSIMM_WRQF_STALLS     \t : %lld\n",wrqf_stalls);
  printf("\nUSIMM_WRQN_STALLS     \t : %lld\n",wrqn_stalls);
  printf("Num reads merged: %lld\n",num_read_merge);
  printf("Num writes merged: %lld\n",num_write_merge);
  //printf("Num reads colliding with refresh: %d\n", read_collide_refresh);//There is no support for this information as of now
  /* Print all other memory system stats. */
  scheduler_stats();
  print_cache_stats(L3Cache);
  print_stats();  

  /*Print Cycle Stats*/
  for(int c=0; c<NUM_CHANNELS; c++)
	  for(int r=0; r<NUM_RANKS ;r++)
		  calculate_power(c,r,0,chips_per_rank);

	printf ("\n#-------------------------------------- Power Stats ----------------------------------------------\n");
	printf ("Note:  1. termRoth/termWoth is the power dissipated in the ODT resistors when Read/Writes terminate \n");
	printf ("          in other ranks on the same channel\n");
	printf ("#-------------------------------------------------------------------------------------------------\n\n");


  /*Print Power Stats*/
	float total_system_power =0;
  for(int c=0; c<NUM_CHANNELS; c++)
	  for(int r=0; r<NUM_RANKS ;r++)
		  total_system_power += calculate_power(c,r,1,chips_per_rank);

        printf ("\n#-------------------CITADEL POWER STATS----------------------------------------------------------------\n");
        printf("\nUSIMM_BCK            \t : %.3f\n",tot_background);
        printf("\nUSIMM_ACT            \t : %.3f\n",tot_act);
        printf("\nUSIMM_RD             \t : %.3f\n",tot_read);
        printf("\nUSIMM_WR             \t : %.3f\n",tot_write);
        printf("\nUSIMM_RDT            \t : %.3f\n",tot_rdt);
        printf("\nUSIMM_WRT            \t : %.3f\n",tot_wrt);
        printf("\nUSIMM_REF            \t : %.3f\n",tot_ref);
        printf("\nUSIMM_RNK            \t : %.3f\n",tot_rnk);

        printf ("\n#-------------------ORIGINAL POWER STATS----------------------------------------------------------------\n");


	if (NUM_CHANNELS == 4) {  /* Assuming that this is 4channel.cfg  */
	  printf ("Total memory system power = %f W\n",total_system_power/1000);
	  printf("Miscellaneous system power = 40 W  # Processor uncore power, disk, I/O, cooling, etc.\n");
	  printf("Processor core power = %f W  # Assuming that each core consumes 10 W when running\n",core_power);
	  printf("Total system power = %f W # Sum of the previous three lines\n", 40 + core_power + total_system_power/1000);
	  printf("Energy Delay product (EDP) = %2.9f J.s\n", (40 + core_power + total_system_power/1000)*(float)((double)CYCLE_VAL/(double)3200000000) * (float)((double)CYCLE_VAL/(double)3200000000));
	}
	else {  /* Assuming that this is 1channel.cfg  */
	  printf ("Total memory system power = %f W\n",total_system_power/1000);
	  printf("Miscellaneous system power = 10 W  # Processor uncore power, disk, I/O, cooling, etc.\n");  /* The total 40 W misc power will be split across 4 channels, only 1 of which is being considered in the 1-channel experiment. */
	  printf("Processor core power = %f W  # Assuming that each core consumes 5 W\n",core_power);  /* Assuming that the cores are more lightweight. */
	  printf("Total system power = %f W # Sum of the previous three lines\n", 10 + core_power + total_system_power/1000);
	  printf("Energy Delay product (EDP) = %2.9f J.s\n", (10 + core_power + total_system_power/1000)*(float)((double)CYCLE_VAL/(double)3200000000) * (float)((double)CYCLE_VAL/(double)3200000000));
	}
/*free_rand(NUMCORES);
free(ROB);
for(int i = 0; i < NUMCORES; i++){
     free(tif[i]);
     free(rand_table[i]);
}
free(tif);
free(rand_table);
free(committed);
free(fetched);
free(time_done);
free(nonmemops);
free(opertype);
free(addr);
free(instrpc);
free(prefixtable);*/
return 0;
}
