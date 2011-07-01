/*----------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Ben Buhrow. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

       				   --bbuhrow@gmail.com 7/1/10
----------------------------------------------------------------------*/

#include "soe.h"

uint64 spSOE(uint64 *primes, uint64 lowlimit, uint64 *highlimit, int count)
{
	/*
	if count == 1, then the primes are simply counted, and not 
	explicitly calculated and saved in *primes.
	*/

	//variables used for the wheel
	uint64 numclasses,prodN,startprime;
	uint32 bucket_depth;

	//variables for blocking up the line structures
	uint64 numflags, numbytes, numlinebytes, bucket_alloc, large_bucket_alloc;

	//misc
	uint64 i,j,k,it=0,num_p=0;
	uint64 i1,i2,i3;
	uint32 sp;
	int pchar;
	uint64 allocated_bytes = 0;

	//structure of static info
	soe_staticdata_t sdata;

	//thread data holds all data needed during sieving
	thread_soedata_t *thread_data;		//an array of thread data objects
	uint64 *locprimes, *mergeprimes;

	//timing
	double t;
	struct timeval tstart, tstop;
	TIME_DIFF *	difference;

	//*********************** BEGIN ******************************//

	sdata.orig_hlimit = *highlimit;
	sdata.orig_llimit = lowlimit;

	if (*highlimit - lowlimit < 1000000)
		*highlimit = lowlimit + 1000000;

	if (*highlimit - lowlimit > 1000000000000ULL)
	{
		printf("range too big\n");
		return 0;
	}

	//more efficient to sieve using mod210 when the range is big
	if ((*highlimit - lowlimit) > 400000000000ULL)
	{
		numclasses=5760;
		prodN=30030;
		startprime=6;
	}	
	else if ((*highlimit - lowlimit) > 40000000000ULL)
	{
		numclasses=480;
		prodN=2310;
		startprime=5;
	}	
	else if ((*highlimit - lowlimit) > 4000000000ULL)
	{
		numclasses=48;
		prodN=210;
		startprime=4;		
	}
	else if ((*highlimit - lowlimit) > 100000000)
	{
		numclasses=8;
		prodN=30;
		startprime=3;
	}
	else
	{
		numclasses=2;
		prodN=6;
		startprime=2;
	}

	sdata.numclasses = numclasses;
	sdata.prodN = prodN;
	sdata.startprime = startprime;

	//create the selection masks
	for (i=0;i<BITSINBYTE;i++)
		nmasks[i] = ~masks[i];

	//store the primes used to sieve the rest of the flags
	//with the max sieve range set by the size of uint32, the number of primes
	//needed is fixed.
	//find the bound of primes we'll need to sieve with to get to limit
	if (*highlimit > 4000000000000000000ULL)
	{
		printf("input too high\n");
		return 0;
	}

	sdata.pbound = (uint64)(sqrt((int64)(*highlimit)) + 1);

	//give these some initial storage
	locprimes = (uint64 *)calloc(1,sizeof(uint64));
	mergeprimes = (uint64 *)calloc(1,sizeof(uint64));

	//get primes to sieve with
	if (VFLAG > 2)
		gettimeofday(&tstart, NULL);
	
	if (sdata.pbound > 1000000)
	{
		// we need a lot of sieving primes... recurse using the fast routine
		// get temporary 64 bit prime storage
		// first estimate how many primes we think we'll find
		j = sdata.pbound;
		k = (uint64)((double)j/log((double)j)*1.2);
		locprimes = (uint64 *)realloc(locprimes,k * sizeof(uint64));	
		if (locprimes == NULL)
		{
			printf("error allocating locprimes\n");
			exit(-1);
		}

		// recursion
		sp = (uint32)spSOE(locprimes,0,&j,0);

		//check if we've found too many
		if (sp > MAXSIEVEPRIMECOUNT)
		{
			printf("input too high\n");
			free(locprimes);
			free(mergeprimes);
			return 0;
		}
		else
		{
			//allocate the sieving prime array
			sdata.sieve_p = (uint32 *)malloc(sp * sizeof(uint32));
			allocated_bytes += sp * sizeof(uint32);
			if (sdata.sieve_p == NULL)
			{
				printf("error allocating sieve_p\n");
				exit(-1);
			}
			else
			{
				if (VFLAG > 2)
					printf("allocated %u bytes for sieving primes\n",sp * (uint32)sizeof(uint32));
			}
			
		}

		// copy into the 32 bit sieving prime array
		for (k=0; k<sp; k++)
		{
			if (locprimes[k] == 0)
				printf("found prime == 0 in locprimes at location %" PRIu64 "\n",k);
			sdata.sieve_p[k] = (uint32)locprimes[k];
		}

	}
	else	
	{
		//base case, maxP <= 1000000.  Need at most 78498 primes
		sdata.sieve_p = (uint32 *)malloc(78498 * sizeof(uint32));
		allocated_bytes += 78498 * sizeof(uint32);
		if (sdata.sieve_p == NULL)
		{
			printf("error allocating sieve_p\n");
			exit(-1);
		}
		else
		{
			if (VFLAG > 2)
				printf("allocated %u bytes for sieving primes\n",78498 * (uint32)sizeof(uint32));
		}
		sp = tiny_soe(sdata.pbound,sdata.sieve_p);

	}

	if (count)
	{
		locprimes = (uint64 *)realloc(locprimes,1 * sizeof(uint64));
	}
	else
	{
		//allocate two arrays used for merging together primes found in different lines 
		//that will be used later.
		j = *highlimit - lowlimit;
		k = (uint64)((double)j/log((double)j)*1.2);
		if (VFLAG > 2)
		{
			printf("estimating storage for primes up to %" PRIu64 "\n",j);
			printf("allocating merge prime storage for %u primes\n",k);
		}
		locprimes = (uint64 *)realloc(locprimes,k * sizeof(uint64));
		mergeprimes = (uint64 *)realloc(mergeprimes,k * sizeof(uint64));
		if (locprimes == NULL)
		{
			printf("could not allocate storage for locprimes\n");
			exit(-1);
		}
		if (mergeprimes == NULL)
		{
			printf("could not allocate storage for mergeprimes\n");
			exit(-1);
		}
	
	}

	if (VFLAG > 2)
	{
		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);

		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);

		printf("elapsed time for seed primes = %6.4f\n",t);
	}
	
	sdata.pboundi = sp;

	//allocate the residue classes.  
	sdata.rclass = (uint32 *)malloc(numclasses * sizeof(uint32));
	allocated_bytes += numclasses * sizeof(uint32);

	//find the residue classes
	k=0;
	for (i=1;i<prodN;i++)
	{
		if (spGCD(i,(fp_digit)prodN) == 1)
		{
			sdata.rclass[k] = (uint32)i;
			//printf("%u ",i);
			k++;
		}
	}
	
	//temporarily set lowlimit to the first multiple of numclasses*prodN < lowlimit
	lowlimit = (lowlimit/(numclasses*prodN))*(numclasses*prodN);
	sdata.lowlimit = lowlimit;

	//reallocate flag structure for wheel and block sieving
	//starting at lowlimit, we need a flag for every 'numresidues' numbers out of 'prodN' up to 
	//limit.  round limit up to make this a whole number.
	numflags = (*highlimit - lowlimit)/prodN;
	numflags += ((numflags % prodN) != 0);
	numflags *= numclasses;

	//since we can pack 8 flags in a byte, we need numflags/8 bytes allocated.
	numbytes = numflags / BITSINBYTE + ((numflags % BITSINBYTE) != 0);

	//since there are N lines to sieve over, each line will contain (numflags/8)/N bytes
	//so round numflags/8 up to the nearest multiple of N
	numlinebytes = numbytes/numclasses + ((numbytes % numclasses) != 0);

	//we want an integer number of blocks, so round up to the nearest multiple of blocksize bytes
	i = 0;
	while (1)
	{
		i += BLOCKSIZE;
		if (i > numlinebytes)
			break;
	}
	numlinebytes = i;

	//all this rounding has likely changed the desired high limit.  compute the new highlimit.
	//the orignial desired high limit is already recorded so the proper count will be returned.
	*highlimit = (uint64)((uint64)numlinebytes * (uint64)prodN * (uint64)BITSINBYTE + lowlimit);
	sdata.highlimit = *highlimit;
	sdata.numlinebytes = numlinebytes;

	//a block consists of BLOCKSIZE bytes of flags
	//which holds FLAGSIZE flags.
	sdata.blocks = numlinebytes/BLOCKSIZE;

	//each flag in a block is spaced prodN integers apart.  record the resulting size of the 
	//number line encoded in each block.
	sdata.blk_r = FLAGSIZE*prodN;
	it=0;

	//this could perhaps be multithreaded, but it probably isn't necessary.
	//find all xGCDs of prime with prodN.  These are used when finding offsets
	sdata.root = (int *)malloc(sdata.pboundi * sizeof(int));
	allocated_bytes += sdata.pboundi * sizeof(uint32);
	if (sdata.root == NULL)
	{
		printf("error allocating roots\n");
		exit(-1);
	}
	else
	{
		if (VFLAG > 2)
			printf("allocated %u bytes for roots\n",(uint32)(sdata.pboundi * sizeof(uint32)));
	}

	sdata.lower_mod_prime = (uint32 *)malloc(sdata.pboundi * sizeof(uint32));
	allocated_bytes += sdata.pboundi * sizeof(uint32);
	if (sdata.lower_mod_prime == NULL)
	{
		printf("error allocating lower mod prime\n");
		exit(-1);
	}
	else
	{
		if (VFLAG > 2)
			printf("allocated %u bytes for lower mod prime\n",
			(uint32)sdata.pboundi * (uint32)sizeof(uint32));
	}

	if (sdata.pboundi > BUCKETSTARTI)
	{
		//then we have primes bigger than BUCKETSTARTP - need to bucket sieve
		uint64 flagsperline = numlinebytes * 8;
		uint64 num_hits = 0;
		uint64 hits_per_bucket;
		
		for (i=BUCKETSTARTI; i<sdata.pboundi; i++)
		{
			//condition to see if the current prime only hits the sieve interval once
			if ((sdata.sieve_p[i] * sdata.prodN) > (sdata.blk_r * sdata.blocks))
				break;
			num_hits += ((uint32)flagsperline / sdata.sieve_p[i] + 1);
		}

		if (0)
		{
			uint64 tmphits = 0;
			int ii;
			//test of the number of hits in the real number line of
			//all bucket sieve primes
			for (ii=BUCKETSTARTI; ii<sdata.pboundi; ii++)			
				tmphits += ((sdata.highlimit - sdata.lowlimit) / (uint64)sdata.sieve_p[ii] + 1);

			printf("hits in real number line = %" PRIu64 "\n",tmphits);
		}

		//assume hits are evenly distributed among buckets.
		hits_per_bucket = num_hits / sdata.blocks;

		//add some margin
		hits_per_bucket = (uint64)((double)hits_per_bucket * 1.10);

		//set the bucket allocation amount
		bucket_alloc = hits_per_bucket;

		num_hits = 0;
		//printf("%u primes above large prime threshold\n",(uint32)(sdata.pboundi - i));
		for (; i<sdata.pboundi; i++)
			num_hits++;

		//assume hits are evenly distributed among buckets.
		hits_per_bucket = num_hits / sdata.blocks;

		//add some margin
		hits_per_bucket = (uint64)((double)hits_per_bucket * 1.1);

		//set the bucket allocation amount, with a minimum of at least 25000
		//because small allocation amounts may violate the uniformity assumption
		//of hits per bucket
		if (num_hits > 0)
			large_bucket_alloc = MAX(hits_per_bucket,50000);
		else
			large_bucket_alloc = 0;

		bucket_depth = (uint32)(sdata.pboundi - BUCKETSTARTI);
	}
	else
	{
		bucket_depth = 0;
	}

	//uncomment this to disable bucket sieving
	//bucket_depth = 0;

	thread_data = (thread_soedata_t *)malloc(THREADS * sizeof(thread_soedata_t));
	allocated_bytes += THREADS * sizeof(thread_soedata_t);
	for (i=0; i<THREADS; i++)
	{
		thread_soedata_t *thread = thread_data + i;

		//allocate a bound for each block
		thread->ddata.pbounds = (uint64 *)malloc(
			sdata.blocks * sizeof(uint64));
		allocated_bytes += sdata.blocks * sizeof(uint64);

		thread->ddata.pbounds[0] = sdata.pboundi;

		//we'll need to store the offset into the next block for each prime
		//actually only need those primes less than BUCKETSTARTP since bucket sieving
		//doesn't use the offset array.
		j = MIN(sp,BUCKETSTARTI);
		thread->ddata.offsets = (uint32 *)malloc(j * sizeof(uint32));
		allocated_bytes += j * sizeof(uint32);
		if (thread->ddata.offsets == NULL)
		{
			printf("error allocating offsets\n");
			exit(-1);
		}
		else
		{
			if (VFLAG > 2)
				printf("allocated %u bytes for offsets\n",(uint32)(j * sizeof(uint32)));
		}

		//allocate the line for this thread
		thread->ddata.line = (uint8 *)malloc(numlinebytes * sizeof(uint8));
		allocated_bytes += numlinebytes * sizeof(uint8);
		if (thread->ddata.line == NULL)
		{
			printf("error allocating sieve line\n");
			exit(-1);
		}
		else
		{
			if (VFLAG > 2)
				printf("allocated %u bytes for sieve line\n",
				(uint32)numlinebytes * (uint32)sizeof(uint8));
		}

#ifdef DO_SPECIAL_COUNT
		//allocate an array so we can count intervals smaller than the sieve line
		j = (sdata.orig_hlimit - sdata.orig_llimit) / 1000000000;
		j += ((sdata.orig_hlimit - sdata.orig_llimit) % 1000000000 > 0);
		thread->ddata.num_special_bins = j;
		thread->ddata.special_count = (uint32 *)calloc(j, sizeof(uint32));
		sdata.num_special_bins = j;
		sdata.special_count = (uint32 *)calloc(j, sizeof(uint32));
		allocated_bytes += j * 2 * sizeof(uint32);
		if (thread->ddata.special_count == NULL)
		{
			printf("error allocating special line count\n");
			exit(-1);
		}
		else
		{
			if (VFLAG > 2)
				printf("allocated %u bytes for special line count\n",(uint32)(j * sizeof(uint32)));
		}
#endif

		if (bucket_depth > BUCKET_BUFFER)
		{			
			//create a bucket for each block
			thread->ddata.sieve_buckets = (soe_bucket_t **)malloc(
				sdata.blocks * sizeof(soe_bucket_t *));
			allocated_bytes += sdata.blocks * sizeof(soe_bucket_t *);

			if (thread->ddata.sieve_buckets == NULL)
			{
				printf("error allocating buckets\n");
				exit(-1);
			}
			else
			{
				if (VFLAG > 2)
					printf("allocated %u bytes for bucket bases\n",
					(uint32)sdata.blocks * (uint32)sizeof(soe_bucket_t *));
			}

			if (large_bucket_alloc > 0)
			{
				thread->ddata.large_sieve_buckets = (uint32 **)malloc(
					sdata.blocks * sizeof(uint32 *));
				allocated_bytes += sdata.blocks * sizeof(uint32 *);

				if (thread->ddata.large_sieve_buckets == NULL)
				{
					printf("error allocating large buckets\n");
					exit(-1);
				}
				else
				{
					if (VFLAG > 2)
						printf("allocated %u bytes for large bucket bases\n",
						(uint32)sdata.blocks * (uint32)sizeof(uint32 *));
				}
			}

			//create a hit counter for each bucket
			thread->ddata.bucket_hits = (uint32 *)malloc(
				sdata.blocks * sizeof(uint32));
			allocated_bytes += sdata.blocks * sizeof(uint32);
			if (thread->ddata.bucket_hits == NULL)
			{
				printf("error allocating hit counters\n");
				exit(-1);
			}
			else
			{
				if (VFLAG > 2)
					printf("allocated %u bytes for hit counters\n",
					(uint32)sdata.blocks * (uint32)sizeof(uint32));
			}

			if (large_bucket_alloc > 0)
			{
				thread->ddata.large_bucket_hits = (uint32 *)malloc(
					sdata.blocks * sizeof(uint32));
				allocated_bytes += sdata.blocks * sizeof(uint32);
				if (thread->ddata.large_bucket_hits == NULL)
				{
					printf("error allocating large hit counters\n");
					exit(-1);
				}
				else
				{
					if (VFLAG > 2)
						printf("allocated %u bytes for large hit counters\n",
						(uint32)sdata.blocks * (uint32)sizeof(uint32));
				}
			}

			//each bucket must be able to hold a hit from every prime used above BUCKETSTARTP.
			//this is overkill, because every prime will not hit every bucket when
			//primes are greater than FLAGSIZE.  but we always write to the end of each
			//bucket so the depth probably doesn't matter much from a cache access standpoint,
			//just from a memory capacity standpoint, and we shouldn't be in any danger
			//of that as long as the input range is managed (should be <= 10B).
			thread->ddata.bucket_depth = bucket_depth;
			thread->ddata.bucket_alloc = bucket_alloc;
			thread->ddata.bucket_alloc_large = large_bucket_alloc;

			for (j = 0; j < sdata.blocks; j++)
			{
				thread->ddata.sieve_buckets[j] = (soe_bucket_t *)malloc(
					bucket_alloc * sizeof(soe_bucket_t));
				allocated_bytes += bucket_alloc * sizeof(soe_bucket_t);

				if (thread->ddata.sieve_buckets[j] == NULL)
				{
					printf("error allocating buckets\n");
					exit(-1);
				}			

				thread->ddata.bucket_hits[j] = 0;

				if (large_bucket_alloc > 0)
				{
					thread->ddata.large_sieve_buckets[j] = (uint32 *)malloc(
						large_bucket_alloc * sizeof(uint32));
					allocated_bytes += large_bucket_alloc * sizeof(uint32);

					if (thread->ddata.large_sieve_buckets[j] == NULL)
					{
						printf("error allocating large buckets\n");
						exit(-1);
					}	

					thread->ddata.large_bucket_hits[j] = 0;
				}
							
			}

			if (VFLAG > 2)
				printf("allocated %u bytes for buckets\n",
				(uint32)sdata.blocks * (uint32)bucket_alloc * (uint32)sizeof(soe_bucket_t));

			if (VFLAG > 2)
				printf("allocated %u bytes for large buckets\n",
				(uint32)sdata.blocks * (uint32)large_bucket_alloc * (uint32)sizeof(uint32));

		}	
		else
			thread->ddata.bucket_depth = 0;

		//this threads' count of primes in its' line
		thread->linecount = 0;
		//share the common static data structure
		thread->sdata = sdata;
	}

	gettimeofday (&tstart, NULL);
	getRoots(&sdata);

	if (VFLAG > 2)
	{
		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);

		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);

		printf("elapsed time for computing roots = %6.4f\n",t);
	}

	if (VFLAG > 2)
	{	
		printf("sieving range %" PRIu64 " to %" PRIu64 "\n",lowlimit,*highlimit);
		printf("using %" PRIu64 " primes, max prime = %" PRIu64 "  \n",sdata.pboundi,sdata.pbound);
		printf("using %" PRIu64 " residue classes\n",numclasses);
		printf("lines have %" PRIu64 " bytes and %" PRIu64 " flags\n",numlinebytes,numlinebytes * 8);
		printf("lines broken into = %" PRIu64 " blocks of size %u\n",sdata.blocks,BLOCKSIZE);
		printf("blocks contain %u flags and cover %" PRIu64 " primes\n", FLAGSIZE, sdata.blk_r);
		if (bucket_depth > BUCKET_BUFFER)
		{
			printf("bucket sieving %u primes > %u\n",bucket_depth,BUCKETSTARTP);
			printf("allocating space for %" PRIu64 " hits per bucket\n",bucket_alloc);
#ifdef DO_LARGE_BUCKETS
			printf("allocating space for %u hits per large bucket\n",large_bucket_alloc);
#endif
		}
		printf("using %" PRIu64 " bytes for sieving storage\n",allocated_bytes);
	}

	/* activate the threads one at a time. The last is the
	   master thread (i.e. not a thread at all). */

	for (i = 0; i < THREADS - 1; i++)
		start_soe_worker_thread(thread_data + i, 0);

	start_soe_worker_thread(thread_data + i, 1);

	//main sieve, line by line
	k = 0;	//count total lines processed
	pchar = 0;
	num_p = 0;
	
	while (k < numclasses)
	{
		fflush(stdout);
		//assign lines to the threads
		j = 0;	//how many lines to process this pass
		for (i = 0; ((int)i < THREADS) && (k < numclasses); i++, k++)
		{
			thread_soedata_t *t = thread_data + i;

			t->current_line = (uint32)k;
			j++;
		}

		//process the lines
		for (i = 0; i < j; i++) 
		{
			thread_soedata_t *t = thread_data + i;

			if (i == j - 1) {
				
				if (count)
				{
					sieve_line(t);
#ifdef DO_SPECIAL_COUNT
					count_line_special(t);
#else
					count_line(t);
#endif
				}
				else
				{
					sieve_line(t);
					primes_from_lineflags(t);
				}
			}
			else {
				if (count)
					t->command = SOE_COMMAND_SIEVE_AND_COUNT;
				else
					t->command = SOE_COMMAND_SIEVE_AND_COMPUTE;
#if defined(WIN32) || defined(_WIN64)
				SetEvent(t->run_event);
#else
				pthread_cond_signal(&t->run_cond);
				pthread_mutex_unlock(&t->run_lock);
#endif
			}
		}

		//wait for each thread to finish
		for (i = 0; i < j; i++) {
			thread_soedata_t *t = thread_data + i;

			if (i < j - 1) {
#if defined(WIN32) || defined(_WIN64)
				WaitForSingleObject(t->finish_event, INFINITE);
#else
				pthread_mutex_lock(&t->run_lock);
				while (t->command != SOE_COMMAND_WAIT)
					pthread_cond_wait(&t->run_cond, &t->run_lock);
#endif
			}
		}

		//printf a progress report if counting
		if (count && VFLAG >= 0)
		{
			//don't print status if computing primes, because lots of routines within
			//yafu do this and they don't want this side effect
			for (i = 0; i<pchar; i++)
				printf("\b");
			pchar = printf("%d%%",(int)((double)k / (double)(numclasses) * 100.0));
			fflush(stdout);
		}

		
		if (count)
		{
#ifdef DO_SPECIAL_COUNT
			for (i = 0; i < j; i++)
			{
				int ix;
				num_p += thread_data[i].linecount;
				
				for (ix=0; ix < sdata.num_special_bins; ix++)
					sdata.special_count[ix] += thread_data[i].ddata.special_count[ix];
			}

#else
			for (i = 0; i < j; i++)
				num_p += thread_data[i].linecount;
#endif
		}
		else
		{
			//accumulate the primes from each line
			for (i=0; i< j; i++)
			{
				//uint64 start_index = num_p;
				uint64 linecount = thread_data[i].linecount;
				uint64 index;
				
				if (linecount == 0)
				{
					printf("found no primes in line\n");
					continue;
				}
				
				if (num_p == 0)
				{
					//add the first primes to the mergelist
					for (index = 0; index < linecount; index++)
					{
						mergeprimes[index] = thread_data[i].ddata.primes[index];
						locprimes[index] = thread_data[i].ddata.primes[index];
					}

				}
				else
				{
					//merge this lines primes (linecount), with the previous merge (num_p)
					//this lines primes are ordered, and so is the previous merged list.

					//increase size of mergeprimes
					i1 = i2 = i3 = 0;
					while ((i1 < num_p) && (i2 < linecount)) {
						if (locprimes[i1] < thread_data[i].ddata.primes[i2])
							mergeprimes[i3++] = locprimes[i1++];
						else
							mergeprimes[i3++] = thread_data[i].ddata.primes[i2++];
					}
					while (i1 < num_p)
						mergeprimes[i3++] = locprimes[i1++];
					while (i2 < linecount)
						mergeprimes[i3++] = thread_data[i].ddata.primes[i2++];

					for (i1=0; i1<(num_p + linecount); i1++)
					{
						locprimes[i1] = mergeprimes[i1];
					}

				}

				num_p += linecount;
				//then free the line's primes
				free(thread_data[i].ddata.primes);

			}
		}
	}

	//stop the worker threads and free stuff not needed anymore
	for (i=0; i<THREADS - 1; i++)
	{
		stop_soe_worker_thread(thread_data + i, 0);
		free(thread_data[i].ddata.offsets);
	}
	stop_soe_worker_thread(thread_data + i, 1);
	free(thread_data[i].ddata.offsets);

	if (count && VFLAG >= 0)
	{
		//don't print status if computing primes, because lots of routines within
		//yafu do this and they don't want this side effect
		for (i = 0; i<pchar; i++)
			printf("\b");
	}

	if (count)
	{
		//add in relevant sieving primes not captured in the flag arrays
		if (sdata.pbound > lowlimit)
		{
			i=0;
			while (i<thread_data[0].ddata.pbounds[0])
			{ 
				if (sdata.sieve_p[i] > lowlimit)
				{
					num_p++;
#ifdef DO_SPECIAL_COUNT
					sdata.special_count[0]++;
#endif
				}
				i++;
			}
		}
	}
	else
	{
		//the sieve primes are not in the line array, so they must be added
		//in if necessary
		//uint64 start_index = num_p;
		it=0;
		
		//first count them
		if (sdata.pbound > lowlimit)
		{
			i=0;
			while (i<thread_data[0].ddata.pbounds[0])
			{ 
				if (sdata.sieve_p[i] > lowlimit)
					it++;
				i++;
			}
		}

		//merge the sieve primes (it), with the previous merge (num_p)
		//this lines primes are ordered, and so is the previous merged list.
		i1 = i2 = i3 = 0;
		while ((i1 < num_p) && (i2 < it)) {
			if (locprimes[i1] < sdata.sieve_p[i2])
				mergeprimes[i3++] = locprimes[i1++];
			else
				mergeprimes[i3++] = sdata.sieve_p[i2++];
		}
		while (i1 < num_p)
			mergeprimes[i3++] = locprimes[i1++];
		while (i2 < it)
			mergeprimes[i3++] = sdata.sieve_p[i2++];

		num_p += it;

		//copy the local mergeprimes into the output primes array
		memcpy(primes,mergeprimes,num_p * sizeof(uint64));

	}

	//printf("maximum bucket usage = %u\n",max_bucket_usage);
#ifdef DO_SPECIAL_COUNT
	//print the special counts
	for (i=0; i<sdata.num_special_bins; i++)
	{
		if (sdata.special_count[i] > 0)
			printf("count in range %d = %u\n",(int)i,sdata.special_count[i]);
	}
#endif

	for (i=0; i<THREADS; i++)
	{
		thread_soedata_t *thread = thread_data + i;
		free(thread->ddata.pbounds);
		free(thread->ddata.line);
#ifdef DO_SPECIAL_COUNT
		free(thread->ddata.special_count);
#endif
	}

	if (bucket_depth > BUCKET_BUFFER)
	{
		for (i=0; i< THREADS; i++)
		{
			thread_soedata_t *thread = thread_data + i;

			free(thread->ddata.bucket_hits);
#ifdef DO_LARGE_BUCKETS
			free(thread->ddata.large_bucket_hits);
#endif
			for (j=0; j < thread->sdata.blocks; j++)
			{
				free(thread->ddata.sieve_buckets[j]);
#ifdef DO_LARGE_BUCKETS
				free(thread->ddata.large_sieve_buckets[j]);
#endif
			}
			free(thread->ddata.sieve_buckets);
#ifdef DO_LARGE_BUCKETS
			free(thread->ddata.large_sieve_buckets);
#endif
		}

	}

	free(locprimes);
	free(mergeprimes);
	free(sdata.root);
	free(sdata.lower_mod_prime);
#ifdef DO_SPECIAL_COUNT
	free(sdata.special_count);
#endif
	free(thread_data);
	free(sdata.sieve_p);
	free(sdata.rclass);

	return num_p;
}

void primesum_check12(uint64 lower, uint64 upper, uint64 startmod, z *squaresum, z *sum)
{
	z mp1;
	//various counters
	uint64 n64;
	uint32 j;
	uint64 pcount = 0;
	
	//stuff for timing
	double t;
	struct timeval tstart, tstop;
	TIME_DIFF *	difference;

	//mananging batches
	uint64 inc, tmpupper=0;
	
	//tracking the modulus to check
	uint64 squaremod, summod;

	//logfile
	FILE *out;

	//for fast divisibility checks
	uint64 powof2sqr, powof2m1sqr, powof2sum, powof2m1sum;

	zInit(&mp1);

	//initialize both the sum and square modulus
	n64 = squaremod = summod = startmod;

	//set batch size based on input range
	if (upper - lower > 1000000000)
		inc = 1000000000;
	else
		inc = upper - lower;

	//paranoia.
	if (startmod == 0)
		startmod = 10;

	//count the number of factors of 2 in the modulus
	powof2sqr = 0;
	while ((n64 & 1) == 0)
	{
		n64 >>= 1;
		powof2sqr++;
	}
	//store this power minus 1, for fast remaindering
	powof2m1sqr = (1 << powof2sqr) - 1;

	//track of the powers of two in the sum and sqr modulus separately.
	powof2m1sum = powof2m1sqr;
	powof2sum = powof2sqr;

	//lets not print all this stuff to the screen...
	PRIMES_TO_SCREEN = 0;
	PRIMES_TO_FILE = 0;
	
	tmpupper = lower;
	while (tmpupper != upper)
	{
		//set the bounds for the next batch
		tmpupper = lower + inc; 
		if (tmpupper > upper)
			tmpupper =  upper;

		gettimeofday(&tstart, NULL);		

		//get a new batch of primes.
		n64 = soe_wrapper(lower,tmpupper,0);

		//keep a running sum of how many we've found
		pcount += n64;

		//status update
		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("\nfound %" PRIu64 " primes in range %" PRIu64 " to %" PRIu64 " in %6.4f sec\n",NUM_P,lower,tmpupper,t);
				
		gettimeofday(&tstart, NULL);
		//now add up the squares.  NUM_P is set by the soe_wrapper to the number of primes
		//found the last time it was called.
		for (j=0; j<NUM_P; j++)
		{
			//soe wrapper should truncate the prime array so that we don't have to do this,
			//but doublecheck that we don't include primes outside the requested batch.
			if ((PRIMES[j] > tmpupper) || (PRIMES[j] < lower))
				break;

#if defined(GCC_ASM64X) && !defined(ASM_ARITH_DEBUG)
			
			ASM_G (
				"addq %%rax, (%%rcx) \n\t"
				"adcq $0, 8(%%rcx) \n\t"
				"mulq %1		\n\t"
				"addq %%rax, (%%rbx)		\n\t"
				"adcq %%rdx, 8(%%rbx)		\n\t"
				"adcq $0, 16(%%rbx)	\n\t"
				: 
				: "b"(squaresum->val), "a"(PRIMES[j]), "c"(sum->val)
				: "rdx", "memory", "cc");				

#else
			sp642z(PRIMES[j],&mp1);
			zSqr(&mp1,&mp1);
			zAdd(squaresum,&mp1,squaresum);
			zShortAdd(sum,PRIMES[j],sum);

#endif
						
			//check if the squaresum is 0 modulo the current power of 10
			if ((squaresum->val[0] & powof2m1sqr) == 0)
			{
				//we have the right number of twos.  do a full check for divisibility.
				//first adjust the bigint size, since the ASM doesn't do this and zShortMod
				//would like size to be correct.
				squaresum->size = 3;
				while (squaresum->val[squaresum->size - 1] == 0)
				{
					squaresum->size--;
					if (squaresum->size == 0)
						break;
				}

				if (squaresum->size == 0)
					squaresum->size = 1;
				
				while (zShortMod(squaresum,squaremod) == 0)
				{
					out = fopen("sum_of_squares.csv","a");
					printf("**** %" PRIu64 " divides prime square sum up to %" PRIu64 " ****\n",squaremod,PRIMES[j]);
					fprintf(out,
						"**** %" PRIu64 " divides prime square sum up to %" PRIu64 ", sum is %s ****\n",
						squaremod,PRIMES[j],z2decstr(squaresum,&gstr1));
					fclose(out);
					//start looking for the next power of 10
					squaremod *= 10;
					//recompute the fast divisibility check
					powof2sqr++;
					powof2m1sqr = (1 << powof2sqr) - 1;
				}
			}			

			//now check the sum.
			if ((sum->val[0] & powof2m1sum) == 0)
			{
				//we have the right number of twos.  do a full check for divisibility.
				//adjust the size, since the ASM doesn't do this and zShortMod
				//would like size to be correct.
				sum->size = 2;
				while (sum->val[sum->size - 1] == 0)
				{
					sum->size--;
					if (sum->size == 0)
						break;
				}

				if (sum->size == 0)
					sum->size = 1;

				while (zShortMod(sum,summod) == 0)
				{
					out = fopen("sum_of_squares.csv","a");
					printf("**** %" PRIu64 " divides prime sum up to %" PRIu64 " ****\n",summod,PRIMES[j]);
					fprintf(out,
						"**** %" PRIu64 " divides prime sum up to %" PRIu64 ", sum is %s ****\n",
						summod,PRIMES[j],z2decstr(sum,&gstr1));
					fclose(out);
					summod *= 10;
					powof2sum++;
					powof2m1sum = (1 << powof2sum) - 1;
				}
			}
		}

		//all done with this batch.  status update again.  first get the sum
		//sizes right, for the benefit of z2decstr
		squaresum->size = 3;
		while (squaresum->val[squaresum->size - 1] == 0)
		{
			squaresum->size--;
			if (squaresum->size == 0)
				break;
		}

		if (squaresum->size == 0)
			squaresum->size = 1;

		sum->size = 2;
		while (sum->val[sum->size - 1] == 0)
		{
			sum->size--;
			if (sum->size == 0)
				break;
		}

		if (sum->size == 0)
			sum->size = 1;

		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("sum complete in %6.4f sec, squaresum = %s, sum = %s\n",
			t,z2decstr(squaresum,&gstr1),z2decstr(sum,&gstr2));
		
		out = fopen("sum_of_squares.csv","a");
		fprintf(out,"%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%s\n",
			upper,n64,pcount,z2decstr(sum,&gstr1),z2decstr(squaresum,&gstr2));
		fclose(out);
		
		//next range.
		lower = tmpupper;
	}

	zFree(&mp1);
	return;
}

void primesum_check3(uint64 lower, uint64 upper, uint64 startmod, z *sum)
{
	z mp1;
	uint32 i=0;
	uint64 n64;
	uint32 j;
	uint64 count, tmpupper=0;
	
	double t;
	struct timeval tstart, tstop;
	TIME_DIFF *	difference;
	uint64 inc, summod, pcount = 0;

	FILE *out;
	uint64 powof2sum, powof2m1sum;

	zInit(&mp1);
	zClear(&mp1);

	//initialize both the sum and square modulus
	n64 = summod = startmod;

	if (upper - lower > 1000000000)
	{
		inc = 1000000000;
		count = (upper - lower) / inc;
	}
	else
	{
		inc = upper - lower;
		count = 0;
	}

	if (startmod == 0)
		startmod = 10;

	powof2sum = 0;
	//count the number of factors of 2 in the modulus
	while ((n64 & 1) == 0)
	{
		n64 >>= 1;
		powof2sum++;
	}
	powof2m1sum = (1 << powof2sum) - 1;

	PRIMES_TO_SCREEN = 0;
	PRIMES_TO_FILE = 0;
	
	//for each chunk
	for (i = 0; i < count; i++)
	{
		tmpupper = lower + inc; 
		gettimeofday(&tstart, NULL);				
		n64 = soe_wrapper(lower,tmpupper,0);
		pcount += n64;
		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("\nfound %" PRIu64 " primes in range %" PRIu64 " to %" PRIu64 " in %6.4f sec\n",NUM_P,lower,tmpupper,t);
		
		
		gettimeofday(&tstart, NULL);
		//now add up the squares
		for (j=0; j<NUM_P; j++)
		{

			if ((PRIMES[j] > tmpupper) || (PRIMES[j] < lower))
				break;

#if defined(GCC_ASM64X) && !defined(ASM_ARITH_DEBUG)
			
			/* 
				fast method to cube a number and sum with a 3-word fixed precision
				bigint

				                p
				*               p
				  ---------------
				          d     a
				*               p
				  ---------------
				  dpd dpa+apd apa
				+  s2    s1    s0
				  ---------------
				   s2    s1    s0


				where d,a are the high,low words of p^2,
				apd,apa are the high,low words of p * a and,
				dpd,dpa are the high,low words of p * d.
				we then sum these three words with our fixed precision cumulative sum s2,s1,s0:
				s0 = s0 + apa
				s1 = s1 + dpa + apd + carry
				s2 = s2 + dpd + carry
			*/


			ASM_G (
				"movq %1, %%rcx \n\t"			/* store prime */
				"mulq %%rcx		\n\t"			/* square it */
				"movq %%rax, %%r8 \n\t"			/* save p^2 lo (a) */
				"movq %%rdx, %%r9 \n\t"			/* save p^2 hi (d) */
				"mulq %%rcx	\n\t"				/* p * a */
				"movq %%rax, %%r10 \n\t"		/* save p*a lo (apa) */
				"movq %%rdx, %%r11 \n\t"		/* save p*a hi (apd) */
				"movq %%r9, %%rax \n\t"			/* p * d */
				"mulq %%rcx \n\t"				/* lo part in rax (dpa), hi in rdx (dpd) */
				"addq %%r10, (%%rbx) \n\t"		/* sum0 = sum0 + apa */
				"adcq %%rax, 8(%%rbx) \n\t"		/* sum1 = sum1 + dpa + carry */
				"adcq %%rdx, 16(%%rbx) \n\t"	/* sum2 = sum2 + dpd + carry */
				"addq %%r11, 8(%%rbx) \n\t"		/* sum1 = sum1 + apd */
				"adcq $0, 16(%%rbx)	\n\t"		/* sum2 = sum2 + carry */
				: 
				: "b"(sum->val), "a"(PRIMES[j])
				: "rcx", "rdx", "r8", "r9", "r10", "r11", "memory", "cc");			

#else
			sp642z(PRIMES[j],&mp1);
			zSqr(&mp1,&mp1);
			zShortMul(&mp1,PRIMES[j],&mp1);
			zAdd(sum,&mp1,sum);

#endif
						
			//check if the sum is 0 modulo the current power of 10
			if ((sum->val[0] & powof2m1sum) == 0)
			{
				//adjust the size, since the ASM doesn't do this and zShortMod
				//would like size to be correct.
				sum->size = 3;
				while (sum->val[sum->size - 1] == 0)
				{
					sum->size--;
					if (sum->size == 0)
						break;
				}

				if (sum->size == 0)
					sum->size = 1;

				//then we have the right number of twos.  check for divisibility.
				while (zShortMod(sum,summod) == 0)
				{
					out = fopen("sum_of_cubes.csv","a");
					printf("**** %" PRIu64 " divides prime cube sum up to %" PRIu64 ", sum = %s ****\n",
						summod,PRIMES[j],z2decstr(sum,&gstr1));
					fprintf(out,
						"**** %" PRIu64 " divides prime cube sum up to %" PRIu64 ", sum is %s ****\n",
						summod,PRIMES[j],z2decstr(sum,&gstr1));
					fclose(out);
					summod *= 10;
					powof2sum++;
					powof2m1sum = (1 << powof2sum) - 1;
				}
			}
		}

		sum->size = 3;
		while (sum->val[sum->size - 1] == 0)
		{
			sum->size--;
			if (sum->size == 0)
				break;
		}

		if (sum->size == 0)
			sum->size = 1;

		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("sum complete in %6.4f sec, sum = %s\n",
			t,z2decstr(sum,&gstr2));
		
		out = fopen("sum_of_cubes.csv","a");
		fprintf(out,"%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s\n",
			tmpupper,n64,pcount,z2decstr(sum,&gstr1));
		fclose(out);
		
		lower = tmpupper;
		tmpupper += inc;
	}

	//printf("done looping.  lower = %" PRIu64 ", upper = %" PRIu64 ", tmpupper = %" PRIu64 "\n",lower,upper,tmpupper);
	if (upper - lower > 0)
	{
		gettimeofday(&tstart, NULL);				
		n64 = soe_wrapper(lower,upper,0);
		pcount += n64;
		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("\nfound %" PRIu64 " primes in range %" PRIu64 " to %" PRIu64 " in %6.4f sec\n",NUM_P,lower,upper,t);		
		
		gettimeofday(&tstart, NULL);
		//now add up the squares
		for (j=0; j<NUM_P; j++)
		{

			if ((PRIMES[j] > upper) || (PRIMES[j] < lower))
				break;

#if defined(GCC_ASM64X) && !defined(ASM_ARITH_DEBUG)

			ASM_G (
				"movq %1, %%rcx \n\t"
				"mulq %%rcx		\n\t"
				"movq %%rax, %%r8 \n\t" /* pp lo (a) */
				"movq %%rdx, %%r9 \n\t" /* pp hi (d) */
				"mulq %%rcx		\n\t"
				"movq %%rax, %%r10 \n\t" /* ap lo */
				"movq %%rdx, %%r11 \n\t" /* ap hi */
				"movq %%r9, %%rax \n\t" 
				"mulq %%rcx \n\t" /* dp lo (rax), dp hi (rdx) */
				"addq %%r10, (%%rbx)		\n\t"
				"adcq %%rax, 8(%%rbx)		\n\t"
				"adcq %%rdx, 16(%%rbx)	\n\t" /* sum(2) += f + carry */
				"addq %%r11, 8(%%rbx) \n\t" /* sum(1) += e */
				"adcq $0, 16(%%rbx)	\n\t" /* sum(2) += f + carry */
				: 
				: "b"(sum->val), "a"(PRIMES[j])
				: "rcx", "rdx", "r8", "r9", "r10", "r11", "memory", "cc");					
				
#else
			sp642z(PRIMES[j],&mp1);
			zSqr(&mp1,&mp1);
			zShortMul(&mp1,PRIMES[j],&mp1);
			zAdd(sum,&mp1,sum);

#endif

			//printf("%d:%" PRIu64 ":%s\n",j,PRIMES[j],z2decstr(sum,&gstr1));
			//check if the sum is 0 modulo the current power of 10
			if ((sum->val[0] & powof2m1sum) == 0)
			{
				//adjust the size, since the ASM doesn't do this and zShortMod
				//would like size to be correct.
				sum->size = 3;
				while (sum->val[sum->size - 1] == 0)
				{
					sum->size--;
					if (sum->size == 0)
						break;
				}

				if (sum->size == 0)
					sum->size = 1;

				//then we have the right number of twos.  check for divisibility.
				while (zShortMod(sum,summod) == 0)
				{
					out = fopen("sum_of_cubes.csv","a");
					printf("**** %" PRIu64 " divides prime cube sum up to %" PRIu64 ", sum = %s ****\n",
						summod,PRIMES[j],z2decstr(sum,&gstr1));
					fprintf(out,
						"**** %" PRIu64 " divides prime cube sum up to %" PRIu64 ", sum is %s ****\n",
						summod,PRIMES[j],z2decstr(sum,&gstr1));
					fclose(out);
					summod *= 10;
					powof2sum++;
					powof2m1sum = (1 << powof2sum) - 1;
				}
			}

		}

		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("sum complete in %6.4f sec, sum = %s\n",
			t,z2decstr(sum,&gstr2));
	}

	zFree(&mp1);
	return;
}

void primesum(uint64 lower, uint64 upper)
{
	z mp1,mp2,mp3,*squaresum,*sum;
	uint64 n64;
	uint32 j;
	uint64 tmpupper=0;
	
	double t;
	struct timeval tstart, tstop;
	TIME_DIFF *	difference;
	uint64 inc, pcount = 0;

	zInit(&mp1);
	zInit(&mp2);
	zInit(&mp3);
	zClear(&mp1);
	squaresum = &mp2;
	sum = &mp3;

	//set batch size based on input range
	if (upper - lower > 1000000000)
		inc = 1000000000;
	else
		inc = upper - lower;

	tmpupper = lower;
	while (tmpupper != upper)
	{
		//set the bounds for the next batch
		tmpupper = lower + inc; 
		if (tmpupper > upper)
			tmpupper =  upper;

		gettimeofday(&tstart, NULL);				
		n64 = soe_wrapper(lower,tmpupper,0);
		pcount += n64;
		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("\nfound %" PRIu64 " primes in range %" PRIu64 " to %" PRIu64 " in %6.4f sec\n",NUM_P,lower,tmpupper,t);
		
		
		gettimeofday(&tstart, NULL);
		//now add up the squares
		for (j=0; j<NUM_P; j++)
		{

			if ((PRIMES[j] > tmpupper) || (PRIMES[j] < lower))
				break;

#if defined(GCC_ASM64X) && !defined(ASM_ARITH_DEBUG)
			
			ASM_G (
				"addq %%rax, (%%rcx) \n\t"
				"adcq $0, 8(%%rcx) \n\t"
				"mulq %1		\n\t"
				"addq %%rax, (%%rbx)		\n\t"
				"adcq %%rdx, 8(%%rbx)		\n\t"
				"adcq $0, 16(%%rbx)	\n\t"
				: 
				: "b"(squaresum->val), "a"(PRIMES[j]), "c"(sum->val)
				: "rdx", "memory", "cc");				

#else
			sp642z(PRIMES[j],&mp1);
			zSqr(&mp1,&mp1);
			zAdd(squaresum,&mp1,squaresum);
			zShortAdd(sum,PRIMES[j],sum);

#endif
			
		}

		squaresum->size = 3;
		while (squaresum->val[squaresum->size - 1] == 0)
		{
			squaresum->size--;
			if (squaresum->size == 0)
				break;
		}

		if (squaresum->size == 0)
			squaresum->size = 1;

		sum->size = 2;
		while (sum->val[sum->size - 1] == 0)
		{
			sum->size--;
			if (sum->size == 0)
				break;
		}

		if (sum->size == 0)
			sum->size = 1;

		gettimeofday (&tstop, NULL);
		difference = my_difftime (&tstart, &tstop);
		t = ((double)difference->secs + (double)difference->usecs / 1000000);
		free(difference);
		printf("sum complete in %6.4f sec, sum = %s, squaresum = %s\n",
			t,z2decstr(sum,&gstr1),z2decstr(squaresum,&gstr2));

		lower = tmpupper;
		tmpupper += inc;
	}

	zFree(&mp1);
	zFree(&mp2);
	zFree(&mp3);
	return;
}


int residue_pattern_mod30[8][8] = {
	{1, 7, 11, 13, 17, 19, 23, 29},
	{7, 19, 17, 1, 29, 13, 11, 23},
	{11, 17, 1, 23, 7, 29, 13, 19},
	{13, 1, 23, 19, 11, 7, 29, 17},
	{17, 29, 7, 11, 19, 23, 1, 13},
	{19, 13, 29, 7, 23, 1, 17, 11},
	{23, 11, 13, 29, 1, 17, 19, 7},
	{29, 23, 19, 17, 13, 11, 7, 1}};

int diff_pattern_mod30[8][8] = {
	{0, 0, 0, 0, 0, 0, 0, 0}, //{18, 2, -14, 2, -14, 2, 18, -14},
	{5, 0, -3, 0, -3, 0, 5, -4},
	{7, 1, -5, 0, -5, 1, 7, -6},
	{8, 1, -6, 0, -6, 1, 8, -6},
	{10, 1, -8, 2, -8, 1, 10, -8},
	{11, 1, -9, 2, -9, 1, 11, -8},
	{13, 2, -11, 2, -11, 2, 13, -10},
	{18, 2, -14, 2, -14, 2, 18, -14}};

int scale_mod30[8] = 
	{18, 2, -14, 2, -14, 2, 18, -14};

int resID_mod30[30] = {
	-1, 0,-1,-1,-1,
	-1,-1, 1,-1,-1,
	-1, 2,-1, 3,-1,
	-1,-1, 4,-1, 5,
	-1,-1,-1, 6,-1,
	-1,-1,-1,-1, 7};

int residue_pattern_mod210[48][48] = {
        {1, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 121, 127, 131, 137, 139, 143, 149, 151, 157, 163, 167, 169, 173, 179, 181, 187, 191, 193, 197, 199, 209},
        {11, 121, 143, 187, 209, 43, 109, 131, 197, 31, 53, 97, 163, 19, 41, 107, 151, 173, 29, 73, 139, 17, 61, 83, 127, 149, 193, 71, 137, 181, 37, 59, 103, 169, 191, 47, 113, 157, 179, 13, 79, 101, 167, 1, 23, 67, 89, 199},
        {13, 143, 169, 11, 37, 89, 167, 193, 61, 113, 139, 191, 59, 137, 163, 31, 83, 109, 187, 29, 107, 1, 53, 79, 131, 157, 209, 103, 181, 23, 101, 127, 179, 47, 73, 151, 19, 71, 97, 149, 17, 43, 121, 173, 199, 41, 67, 197},
        {17, 187, 11, 79, 113, 181, 73, 107, 209, 67, 101, 169, 61, 163, 197, 89, 157, 191, 83, 151, 43, 179, 37, 71, 139, 173, 31, 167, 59, 127, 19, 53, 121, 13, 47, 149, 41, 109, 143, 1, 103, 137, 29, 97, 131, 199, 23, 193},
        {19, 209, 37, 113, 151, 17, 131, 169, 73, 149, 187, 53, 167, 71, 109, 13, 89, 127, 31, 107, 11, 163, 29, 67, 143, 181, 47, 199, 103, 179, 83, 121, 197, 101, 139, 43, 157, 23, 61, 137, 41, 79, 193, 59, 97, 173, 1, 191},
        {23, 43, 89, 181, 17, 109, 37, 83, 11, 103, 149, 31, 169, 97, 143, 71, 163, 209, 137, 19, 157, 131, 13, 59, 151, 197, 79, 53, 191, 73, 1, 47, 139, 67, 113, 41, 179, 61, 107, 199, 127, 173, 101, 193, 29, 121, 167, 187},
        {29, 109, 167, 73, 131, 37, 1, 59, 23, 139, 197, 103, 67, 31, 89, 53, 169, 17, 191, 97, 61, 83, 199, 47, 163, 11, 127, 149, 113, 19, 193, 41, 157, 121, 179, 143, 107, 13, 71, 187, 151, 209, 173, 79, 137, 43, 101, 181},
        {31, 131, 193, 107, 169, 83, 59, 121, 97, 11, 73, 197, 173, 149, 1, 187, 101, 163, 139, 53, 29, 67, 191, 43, 167, 19, 143, 181, 157, 71, 47, 109, 23, 209, 61, 37, 13, 137, 199, 113, 89, 151, 127, 41, 103, 17, 79, 179},
        {37, 197, 61, 209, 73, 11, 23, 97, 109, 47, 121, 59, 71, 83, 157, 169, 107, 181, 193, 131, 143, 19, 167, 31, 179, 43, 191, 67, 79, 17, 29, 103, 41, 53, 127, 139, 151, 89, 163, 101, 113, 187, 199, 137, 1, 149, 13, 173},
        {41, 31, 113, 67, 149, 103, 139, 11, 47, 1, 83, 37, 73, 109, 191, 17, 181, 53, 89, 43, 79, 197, 151, 23, 187, 59, 13, 131, 167, 121, 157, 29, 193, 19, 101, 137, 173, 127, 209, 163, 199, 71, 107, 61, 143, 97, 179, 169},
        {43, 53, 139, 101, 187, 149, 197, 73, 121, 83, 169, 131, 179, 17, 103, 151, 113, 199, 37, 209, 47, 181, 143, 19, 191, 67, 29, 163, 1, 173, 11, 97, 59, 107, 193, 31, 79, 41, 127, 89, 137, 13, 61, 23, 109, 71, 157, 167},
        {47, 97, 191, 169, 53, 31, 103, 197, 59, 37, 131, 109, 181, 43, 137, 209, 187, 71, 143, 121, 193, 149, 127, 11, 199, 83, 61, 17, 89, 67, 139, 23, 1, 73, 167, 29, 101, 79, 173, 151, 13, 107, 179, 157, 41, 19, 113, 163},
        {53, 163, 59, 61, 167, 169, 67, 173, 71, 73, 179, 181, 79, 187, 83, 191, 193, 89, 197, 199, 97, 101, 103, 209, 1, 107, 109, 113, 11, 13, 121, 17, 19, 127, 23, 131, 29, 31, 137, 139, 37, 143, 41, 43, 149, 151, 47, 157},
        {59, 19, 137, 163, 71, 97, 31, 149, 83, 109, 17, 43, 187, 121, 29, 173, 199, 107, 41, 67, 1, 53, 79, 197, 13, 131, 157, 209, 143, 169, 103, 11, 37, 181, 89, 23, 167, 193, 101, 127, 61, 179, 113, 139, 47, 73, 191, 151},
        {61, 41, 163, 197, 109, 143, 89, 1, 157, 191, 103, 137, 83, 29, 151, 97, 131, 43, 199, 23, 179, 37, 71, 193, 17, 139, 173, 31, 187, 11, 167, 79, 113, 59, 181, 127, 73, 107, 19, 53, 209, 121, 67, 101, 13, 47, 169, 149},
        {67, 107, 31, 89, 13, 71, 53, 187, 169, 17, 151, 209, 191, 173, 97, 79, 137, 61, 43, 101, 83, 199, 47, 181, 29, 163, 11, 127, 109, 167, 149, 73, 131, 113, 37, 19, 1, 59, 193, 41, 23, 157, 139, 197, 121, 179, 103, 143},
        {71, 151, 83, 157, 89, 163, 169, 101, 107, 181, 113, 187, 193, 199, 131, 137, 1, 143, 149, 13, 19, 167, 31, 173, 37, 179, 43, 191, 197, 61, 67, 209, 73, 79, 11, 17, 23, 97, 29, 103, 109, 41, 47, 121, 53, 127, 59, 139},
        {73, 173, 109, 191, 127, 209, 17, 163, 181, 53, 199, 71, 89, 107, 43, 61, 143, 79, 97, 179, 197, 151, 23, 169, 41, 187, 59, 13, 31, 113, 131, 67, 149, 167, 103, 121, 139, 11, 157, 29, 47, 193, 1, 83, 19, 101, 37, 137},
        {79, 29, 187, 83, 31, 137, 191, 139, 193, 89, 37, 143, 197, 41, 199, 43, 149, 97, 151, 47, 101, 103, 209, 157, 53, 1, 107, 109, 163, 59, 113, 61, 167, 11, 169, 13, 67, 173, 121, 17, 71, 19, 73, 179, 127, 23, 181, 131},
        {83, 73, 29, 151, 107, 19, 97, 53, 131, 43, 209, 121, 199, 67, 23, 101, 13, 179, 47, 169, 37, 71, 193, 149, 61, 17, 139, 173, 41, 163, 31, 197, 109, 187, 143, 11, 89, 1, 167, 79, 157, 113, 191, 103, 59, 181, 137, 127},
        {89, 139, 107, 43, 11, 157, 61, 29, 143, 79, 47, 193, 97, 1, 179, 83, 19, 197, 101, 37, 151, 23, 169, 137, 73, 41, 187, 59, 173, 109, 13, 191, 127, 31, 209, 113, 17, 163, 131, 67, 181, 149, 53, 199, 167, 103, 71, 121},
        {97, 17, 1, 179, 163, 131, 83, 67, 19, 197, 181, 149, 101, 53, 37, 199, 167, 151, 103, 71, 23, 169, 137, 121, 89, 73, 41, 187, 139, 107, 59, 43, 11, 173, 157, 109, 61, 29, 13, 191, 143, 127, 79, 47, 31, 209, 193, 113},
        {101, 61, 53, 37, 29, 13, 199, 191, 167, 151, 143, 127, 103, 79, 71, 47, 31, 23, 209, 193, 169, 137, 121, 113, 97, 89, 73, 41, 17, 1, 187, 179, 163, 139, 131, 107, 83, 67, 59, 43, 19, 11, 197, 181, 173, 157, 149, 109},
        {103, 83, 79, 71, 67, 59, 47, 43, 31, 23, 19, 11, 209, 197, 193, 181, 173, 169, 157, 149, 137, 121, 113, 109, 101, 97, 89, 73, 61, 53, 41, 37, 29, 17, 13, 1, 199, 191, 187, 179, 167, 163, 151, 143, 139, 131, 127, 107},
        {107, 127, 131, 139, 143, 151, 163, 167, 179, 187, 191, 199, 1, 13, 17, 29, 37, 41, 53, 61, 73, 89, 97, 101, 109, 113, 121, 137, 149, 157, 169, 173, 181, 193, 197, 209, 11, 19, 23, 31, 43, 47, 59, 67, 71, 79, 83, 103},
        {109, 149, 157, 173, 181, 197, 11, 19, 43, 59, 67, 83, 107, 131, 139, 163, 179, 187, 1, 17, 41, 73, 89, 97, 113, 121, 137, 169, 193, 209, 23, 31, 47, 71, 79, 103, 127, 143, 151, 167, 191, 199, 13, 29, 37, 53, 61, 101},
        {113, 193, 209, 31, 47, 79, 127, 143, 191, 13, 29, 61, 109, 157, 173, 11, 43, 59, 107, 139, 187, 41, 73, 89, 121, 137, 169, 23, 71, 103, 151, 167, 199, 37, 53, 101, 149, 181, 197, 19, 67, 83, 131, 163, 179, 1, 17, 97},
        {121, 71, 103, 167, 199, 53, 149, 181, 67, 131, 163, 17, 113, 209, 31, 127, 191, 13, 109, 173, 59, 187, 41, 73, 137, 169, 23, 151, 37, 101, 197, 19, 83, 179, 1, 97, 193, 47, 79, 143, 29, 61, 157, 11, 43, 107, 139, 89},
        {127, 137, 181, 59, 103, 191, 113, 157, 79, 167, 1, 89, 11, 143, 187, 109, 197, 31, 163, 41, 173, 139, 17, 61, 149, 193, 71, 37, 169, 47, 179, 13, 101, 23, 67, 199, 121, 209, 43, 131, 53, 97, 19, 107, 151, 29, 73, 83},
        {131, 181, 23, 127, 179, 73, 19, 71, 17, 121, 173, 67, 13, 169, 11, 167, 61, 113, 59, 163, 109, 107, 1, 53, 157, 209, 103, 101, 47, 151, 97, 149, 43, 199, 41, 197, 143, 37, 89, 193, 139, 191, 137, 31, 83, 187, 29, 79},
        {137, 37, 101, 19, 83, 1, 193, 47, 29, 157, 11, 139, 121, 103, 167, 149, 67, 131, 113, 31, 13, 59, 187, 41, 169, 23, 151, 197, 179, 97, 79, 143, 61, 43, 107, 89, 71, 199, 53, 181, 163, 17, 209, 127, 191, 109, 173, 73},
        {139, 59, 127, 53, 121, 47, 41, 109, 103, 29, 97, 23, 17, 11, 79, 73, 209, 67, 61, 197, 191, 43, 179, 37, 173, 31, 167, 19, 13, 149, 143, 1, 137, 131, 199, 193, 187, 113, 181, 107, 101, 169, 163, 89, 157, 83, 151, 71},
        {143, 103, 179, 121, 197, 139, 157, 23, 41, 193, 59, 1, 19, 37, 113, 131, 73, 149, 167, 109, 127, 11, 163, 29, 181, 47, 199, 83, 101, 43, 61, 137, 79, 97, 173, 191, 209, 151, 17, 169, 187, 53, 71, 13, 89, 31, 107, 67},
        {149, 169, 47, 13, 101, 67, 121, 209, 53, 19, 107, 73, 127, 181, 59, 113, 79, 167, 11, 187, 31, 173, 139, 17, 193, 71, 37, 179, 23, 199, 43, 131, 97, 151, 29, 83, 137, 103, 191, 157, 1, 89, 143, 109, 197, 163, 41, 61},
        {151, 191, 73, 47, 139, 113, 179, 61, 127, 101, 193, 167, 23, 89, 181, 37, 11, 103, 169, 143, 209, 157, 131, 13, 197, 79, 53, 1, 67, 41, 107, 199, 173, 29, 121, 187, 43, 17, 109, 83, 149, 31, 97, 71, 163, 137, 19, 59},
        {157, 47, 151, 149, 43, 41, 143, 37, 139, 137, 31, 29, 131, 23, 127, 19, 17, 121, 13, 11, 113, 109, 107, 1, 209, 103, 101, 97, 199, 197, 89, 193, 191, 83, 187, 79, 181, 179, 73, 71, 173, 67, 169, 167, 61, 59, 163, 53},
        {163, 113, 19, 41, 157, 179, 107, 13, 151, 173, 79, 101, 29, 167, 73, 1, 23, 139, 67, 89, 17, 61, 83, 199, 11, 127, 149, 193, 121, 143, 71, 187, 209, 137, 43, 181, 109, 131, 37, 59, 197, 103, 31, 53, 169, 191, 97, 47},
        {167, 157, 71, 109, 23, 61, 13, 137, 89, 127, 41, 79, 31, 193, 107, 59, 97, 11, 173, 1, 163, 29, 67, 191, 19, 143, 181, 47, 209, 37, 199, 113, 151, 103, 17, 179, 131, 169, 83, 121, 73, 197, 149, 187, 101, 139, 53, 43},
        {169, 179, 97, 143, 61, 107, 71, 199, 163, 209, 127, 173, 137, 101, 19, 193, 29, 157, 121, 167, 131, 13, 59, 187, 23, 151, 197, 79, 43, 89, 53, 181, 17, 191, 109, 73, 37, 83, 1, 47, 11, 139, 103, 149, 67, 113, 31, 41},
        {173, 13, 149, 1, 137, 199, 187, 113, 101, 163, 89, 151, 139, 127, 53, 41, 103, 29, 17, 79, 67, 191, 43, 179, 31, 167, 19, 143, 131, 193, 181, 107, 169, 157, 83, 71, 59, 121, 47, 109, 97, 23, 11, 73, 209, 61, 197, 37},
        {179, 79, 17, 103, 41, 127, 151, 89, 113, 199, 137, 13, 37, 61, 209, 23, 109, 47, 71, 157, 181, 143, 19, 167, 43, 191, 67, 29, 53, 139, 163, 101, 187, 1, 149, 173, 197, 73, 11, 97, 121, 59, 83, 169, 107, 193, 131, 31},
        {181, 101, 43, 137, 79, 173, 209, 151, 187, 71, 13, 107, 143, 179, 121, 157, 41, 193, 19, 113, 149, 127, 11, 163, 47, 199, 83, 61, 97, 191, 17, 169, 53, 89, 31, 67, 103, 197, 139, 23, 59, 1, 37, 131, 73, 167, 109, 29},
        {187, 167, 121, 29, 193, 101, 173, 127, 199, 107, 61, 179, 41, 113, 67, 139, 47, 1, 73, 191, 53, 79, 197, 151, 59, 13, 131, 157, 19, 137, 209, 163, 71, 143, 97, 169, 31, 149, 103, 11, 83, 37, 109, 17, 181, 89, 43, 23},
        {191, 1, 173, 97, 59, 193, 79, 41, 137, 61, 23, 157, 43, 139, 101, 197, 121, 83, 179, 103, 199, 47, 181, 143, 67, 29, 163, 11, 107, 31, 127, 89, 13, 109, 71, 167, 53, 187, 149, 73, 169, 131, 17, 151, 113, 37, 209, 19},
        {193, 23, 199, 131, 97, 29, 137, 103, 1, 143, 109, 41, 149, 47, 13, 121, 53, 19, 127, 59, 167, 31, 173, 139, 71, 37, 179, 43, 151, 83, 191, 157, 89, 197, 163, 61, 169, 101, 67, 209, 107, 73, 181, 113, 79, 11, 187, 17},
        {197, 67, 41, 199, 173, 121, 43, 17, 149, 97, 71, 19, 151, 73, 47, 179, 127, 101, 23, 181, 103, 209, 157, 131, 79, 53, 1, 107, 29, 187, 109, 83, 31, 163, 137, 59, 191, 139, 113, 61, 193, 167, 89, 37, 11, 169, 143, 13},
        {199, 89, 67, 23, 1, 167, 101, 79, 13, 179, 157, 113, 47, 191, 169, 103, 59, 37, 181, 137, 71, 193, 149, 127, 83, 61, 17, 139, 73, 29, 173, 151, 107, 41, 19, 163, 97, 53, 31, 197, 131, 109, 43, 209, 187, 143, 121, 11},
        {209, 199, 197, 193, 191, 187, 181, 179, 173, 169, 167, 163, 157, 151, 149, 143, 139, 137, 131, 127, 121, 113, 109, 107, 103, 101, 97, 89, 83, 79, 73, 71, 67, 61, 59, 53, 47, 43, 41, 37, 31, 29, 23, 19, 17, 13, 11, 1},
};


int diff_pattern_mod210[48][48] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {15, -6, -1, -6, 0, 4, -7, 5, -1, -6, -2, 4, 5, -6, 4, -1, -6, 4, 0, 3, 9, 0, -6, -2, -6, 0, 9, 3, 0, 4, -6, -1, 4, -6, 5, 4, -2, -6, -1, 5, -7, 4, 0, -6, -1, -6, 15, -8},
        {17, -7, -2, -6, -1, 4, -6, 5, -1, -8, -1, 5, 4, -7, 6, -1, -7, 4, -1, 5, 11, -1, -7, -2, -7, -1, 11, 5, -1, 4, -7, -1, 6, -7, 4, 5, -1, -8, -1, 5, -6, 4, -1, -6, -2, -7, 17, -8},
        {22, -10, 0, -9, -2, 7, -10, 6, -1, -10, -1, 7, 5, -8, 6, -2, -9, 7, -2, 7, 13, -1, -9, -2, -9, -1, 13, 7, -2, 7, -9, -2, 6, -8, 5, 7, -1, -10, -1, 6, -10, 7, -2, -9, 0, -10, 22, -10},
        {24, -10, -1, -11, -2, 7, -10, 8, -3, -10, -1, 6, 8, -10, 6, -1, -11, 8, -2, 6, 16, -1, -10, -2, -10, -1, 16, 6, -2, 8, -11, -1, 6, -10, 8, 6, -1, -10, -3, 8, -10, 7, -2, -11, -1, -10, 24, -10},
        {30, -13, -2, -13, -1, 8, -12, 7, -1, -13, -1, 8, 8, -12, 9, -3, -12, 8, -1, 8, 19, -2, -12, -2, -12, -2, 19, 8, -1, 8, -12, -3, 9, -12, 8, 8, -1, -13, -1, 7, -12, 8, -1, -13, -2, -13, 30, -12},
        {38, -17, -1, -17, -2, 11, -16, 11, -3, -15, -3, 11, 11, -16, 11, -3, -16, 11, -3, 12, 24, -2, -16, -4, -16, -2, 24, 12, -3, 11, -16, -3, 11, -16, 11, 11, -3, -15, -3, 11, -16, 11, -2, -17, -1, -17, 38, -16},
        {39, -16, -3, -17, -2, 11, -17, 11, -3, -15, -3, 11, 11, -16, 11, -3, -17, 12, -2, 11, 26, -3, -16, -4, -16, -3, 26, 11, -2, 12, -17, -3, 11, -16, 11, 11, -3, -15, -3, 11, -17, 11, -2, -17, -3, -16, 39, -16},
        {48, -20, -4, -19, -5, 15, -21, 15, -3, -21, -3, 14, 14, -21, 14, -3, -20, 14, -4, 14, 31, -4, -19, -4, -19, -4, 31, 14, -4, 14, -20, -3, 14, -21, 14, 14, -3, -21, -3, 15, -21, 15, -5, -19, -4, -20, 48, -20},
        {53, -22, -4, -23, -3, 15, -23, 17, -4, -22, -4, 16, 15, -23, 15, -3, -22, 15, -3, 15, 34, -4, -22, -4, -22, -4, 34, 15, -3, 15, -22, -3, 15, -23, 15, 16, -4, -22, -4, 17, -23, 15, -3, -23, -4, -22, 53, -22},
        {55, -24, -4, -23, -4, 17, -23, 15, -3, -24, -4, 16, 16, -23, 16, -3, -23, 15, -4, 17, 35, -4, -23, -4, -23, -4, 35, 17, -4, 15, -23, -3, 16, -23, 16, 16, -4, -24, -3, 15, -23, 17, -4, -23, -4, -24, 55, -22},
        {59, -25, -4, -25, -4, 17, -25, 17, -4, -26, -3, 17, 18, -27, 18, -4, -25, 17, -4, 18, 38, -4, -26, -2, -26, -4, 38, 18, -4, 17, -25, -4, 18, -27, 18, 17, -3, -26, -4, 17, -25, 17, -4, -25, -4, -25, 59, -24},
        {67, -28, -4, -30, -4, 20, -29, 20, -4, -30, -4, 20, 19, -28, 19, -4, -29, 20, -4, 18, 44, -4, -29, -4, -29, -4, 44, 18, -4, 20, -29, -4, 19, -28, 19, 20, -4, -30, -4, 20, -29, 20, -4, -30, -4, -28, 67, -28},
        {76, -33, -5, -31, -6, 23, -33, 23, -5, -33, -4, 21, 22, -32, 22, -4, -33, 22, -5, 22, 49, -5, -32, -6, -32, -5, 49, 22, -5, 22, -33, -4, 22, -32, 22, 21, -4, -33, -5, 23, -33, 23, -6, -31, -5, -33, 76, -32},
        {78, -34, -4, -33, -6, 23, -33, 22, -5, -33, -6, 24, 22, -33, 22, -5, -32, 23, -6, 22, 51, -5, -33, -6, -33, -5, 51, 22, -6, 23, -32, -5, 22, -33, 22, 24, -6, -33, -5, 22, -33, 23, -6, -33, -4, -34, 78, -32},
        {86, -36, -6, -37, -5, 25, -37, 25, -6, -36, -6, 25, 25, -37, 26, -7, -35, 25, -7, 26, 56, -6, -37, -6, -37, -6, 56, 26, -7, 25, -35, -7, 26, -37, 25, 25, -6, -36, -6, 25, -37, 25, -5, -37, -6, -36, 86, -36},
        {91, -38, -7, -38, -7, 27, -39, 27, -6, -38, -7, 27, 27, -40, 26, -5, -39, 26, -6, 27, 58, -5, -39, -6, -39, -5, 58, 27, -6, 26, -39, -5, 26, -40, 27, 27, -7, -38, -6, 27, -39, 27, -7, -38, -7, -38, 91, -38},
        {93, -39, -7, -40, -6, 27, -40, 28, -6, -39, -7, 27, 27, -39, 27, -7, -39, 26, -6, 28, 60, -6, -40, -6, -40, -6, 60, 28, -6, 26, -39, -7, 27, -39, 27, 27, -7, -39, -6, 28, -40, 27, -6, -40, -7, -39, 93, -38},
        {101, -43, -6, -43, -8, 30, -43, 30, -7, -43, -7, 30, 29, -42, 29, -8, -43, 30, -6, 28, 66, -7, -43, -6, -43, -7, 66, 28, -6, 30, -43, -8, 29, -42, 29, 30, -7, -43, -7, 30, -43, 30, -8, -43, -6, -43, 101, -42},
        {107, -46, -7, -45, -7, 30, -44, 30, -6, -46, -7, 32, 30, -45, 30, -7, -45, 32, -8, 31, 69, -7, -46, -6, -46, -7, 69, 31, -8, 32, -45, -7, 30, -45, 30, 32, -7, -46, -6, 30, -44, 30, -7, -45, -7, -46, 107, -44},
        {114, -48, -7, -50, -7, 34, -49, 33, -7, -48, -8, 32, 34, -49, 34, -8, -48, 32, -7, 33, 74, -8, -49, -6, -49, -8, 74, 33, -7, 32, -48, -8, 34, -49, 34, 32, -8, -48, -7, 33, -49, 34, -7, -50, -7, -48, 114, -48},
        {125, -52, -9, -53, -8, 37, -53, 36, -8, -53, -9, 36, 37, -53, 37, -10, -52, 36, -8, 36, 80, -9, -52, -8, -52, -9, 80, 36, -8, 36, -52, -10, 37, -53, 37, 36, -9, -53, -8, 36, -53, 37, -8, -53, -9, -52, 125, -54},
        {131, -55, -9, -55, -9, 39, -56, 37, -8, -55, -9, 38, 38, -55, 38, -9, -55, 37, -8, 37, 83, -8, -54, -10, -54, -8, 83, 37, -8, 37, -55, -9, 38, -55, 38, 38, -9, -55, -8, 37, -56, 39, -9, -55, -9, -55, 131, -56},
        {133, -56, -9, -56, -9, 39, -56, 38, -9, -56, -10, 39, 39, -56, 38, -9, -56, 38, -9, 38, 86, -8, -56, -10, -56, -8, 86, 38, -9, 38, -56, -9, 38, -56, 39, 39, -10, -56, -9, 38, -56, 39, -9, -56, -9, -56, 133, -56},
        {137, -58, -9, -58, -9, 39, -58, 40, -9, -58, -8, 39, 39, -58, 40, -9, -58, 40, -9, 40, 88, -10, -58, -8, -58, -10, 88, 40, -9, 40, -58, -9, 40, -58, 39, 39, -8, -58, -9, 40, -58, 39, -9, -58, -9, -58, 137, -58},
        {139, -59, -9, -59, -9, 39, -58, 41, -10, -59, -9, 40, 40, -59, 40, -9, -59, 41, -10, 41, 91, -10, -60, -8, -60, -10, 91, 41, -10, 41, -59, -9, 40, -59, 40, 40, -9, -59, -10, 41, -58, 39, -9, -59, -9, -59, 139, -58},
        {145, -62, -9, -61, -10, 41, -61, 42, -10, -61, -9, 42, 41, -61, 41, -8, -62, 42, -10, 42, 94, -9, -62, -10, -62, -9, 94, 42, -10, 42, -62, -8, 41, -61, 41, 42, -9, -61, -10, 42, -61, 41, -10, -61, -9, -62, 145, -60},
        {156, -66, -11, -64, -11, 44, -65, 45, -11, -66, -10, 46, 44, -65, 44, -10, -66, 46, -11, 45, 100, -10, -65, -12, -65, -10, 100, 45, -11, 46, -66, -10, 44, -65, 44, 46, -10, -66, -11, 45, -65, 44, -11, -64, -11, -66, 156, -66},
        {163, -68, -11, -69, -11, 48, -70, 48, -12, -68, -11, 46, 48, -69, 48, -11, -69, 46, -10, 47, 105, -11, -68, -12, -68, -11, 105, 47, -10, 46, -69, -11, 48, -69, 48, 46, -11, -68, -12, 48, -70, 48, -11, -69, -11, -68, 163, -70},
        {169, -71, -12, -71, -10, 48, -71, 48, -11, -71, -11, 48, 49, -72, 49, -10, -71, 48, -12, 50, 108, -11, -71, -12, -71, -11, 108, 50, -12, 48, -71, -10, 49, -72, 49, 48, -11, -71, -11, 48, -71, 48, -10, -71, -12, -71, 169, -72},
        {177, -75, -11, -74, -12, 51, -74, 50, -12, -75, -11, 51, 51, -75, 51, -11, -75, 52, -12, 50, 114, -12, -74, -12, -74, -12, 114, 50, -12, 52, -75, -11, 51, -75, 51, 51, -11, -75, -12, 50, -74, 51, -12, -74, -11, -75, 177, -76},
        {179, -76, -11, -76, -11, 51, -75, 51, -12, -76, -11, 51, 51, -74, 52, -13, -75, 52, -12, 51, 116, -13, -75, -12, -75, -13, 116, 51, -12, 52, -75, -13, 52, -74, 51, 51, -11, -76, -12, 51, -75, 51, -11, -76, -11, -76, 179, -76},
        {184, -78, -12, -77, -13, 53, -77, 53, -12, -78, -12, 53, 53, -77, 52, -11, -79, 53, -11, 52, 118, -12, -77, -12, -77, -12, 118, 52, -11, 53, -79, -11, 52, -77, 53, 53, -12, -78, -12, 53, -77, 53, -13, -77, -12, -78, 184, -78},
        {192, -80, -14, -81, -12, 55, -81, 56, -13, -81, -12, 54, 56, -81, 56, -13, -82, 55, -12, 56, 123, -13, -81, -12, -81, -13, 123, 56, -12, 55, -82, -13, 56, -81, 56, 54, -12, -81, -13, 56, -81, 55, -12, -81, -14, -80, 192, -82},
        {194, -81, -13, -83, -12, 55, -81, 55, -13, -81, -14, 57, 56, -82, 56, -14, -81, 56, -13, 56, 125, -13, -82, -12, -82, -13, 125, 56, -13, 56, -81, -14, 56, -82, 56, 57, -14, -81, -13, 55, -81, 55, -12, -83, -13, -81, 194, -82},
        {203, -86, -14, -84, -14, 58, -85, 58, -14, -84, -14, 58, 59, -86, 59, -14, -85, 58, -14, 60, 130, -14, -85, -14, -85, -14, 130, 60, -14, 58, -85, -14, 59, -86, 59, 58, -14, -84, -14, 58, -85, 58, -14, -84, -14, -86, 203, -86},
        {211, -89, -14, -89, -14, 61, -89, 61, -14, -88, -15, 61, 60, -87, 60, -14, -89, 61, -14, 60, 136, -14, -88, -16, -88, -14, 136, 60, -14, 61, -89, -14, 60, -87, 60, 61, -15, -88, -14, 61, -89, 61, -14, -89, -14, -89, 211, -90},
        {215, -90, -14, -91, -14, 61, -91, 63, -15, -90, -14, 62, 62, -91, 62, -15, -91, 63, -14, 61, 139, -14, -91, -14, -91, -14, 139, 61, -14, 63, -91, -15, 62, -91, 62, 62, -14, -90, -15, 63, -91, 61, -14, -91, -14, -90, 215, -92},
        {217, -92, -14, -91, -15, 63, -91, 61, -14, -92, -14, 62, 63, -91, 63, -15, -92, 63, -15, 63, 140, -14, -92, -14, -92, -14, 140, 63, -15, 63, -92, -15, 63, -91, 63, 62, -14, -92, -14, 61, -91, 63, -15, -91, -14, -92, 217, -92},
        {222, -94, -14, -95, -13, 63, -93, 63, -15, -93, -15, 64, 64, -93, 64, -15, -94, 64, -14, 64, 143, -14, -95, -14, -95, -14, 143, 64, -14, 64, -94, -15, 64, -93, 64, 64, -15, -93, -15, 63, -93, 63, -13, -95, -14, -94, 222, -94},
        {231, -98, -15, -97, -16, 67, -97, 67, -15, -99, -15, 67, 67, -98, 67, -15, -97, 66, -16, 67, 148, -15, -98, -14, -98, -15, 148, 67, -16, 66, -97, -15, 67, -98, 67, 67, -15, -99, -15, 67, -97, 67, -16, -97, -15, -98, 231, -98},
        {232, -97, -17, -97, -16, 67, -98, 67, -15, -99, -15, 67, 67, -98, 67, -15, -98, 67, -15, 66, 150, -16, -98, -14, -98, -16, 150, 66, -15, 67, -98, -15, 67, -98, 67, 67, -15, -99, -15, 67, -98, 67, -16, -97, -17, -97, 232, -98},
        {240, -101, -16, -101, -17, 70, -102, 71, -17, -101, -17, 70, 70, -102, 69, -15, -102, 70, -17, 70, 155, -16, -102, -16, -102, -16, 155, 70, -17, 70, -102, -15, 69, -102, 70, 70, -17, -101, -17, 71, -102, 70, -17, -101, -16, -101, 240, -102},
        {246, -104, -17, -103, -16, 71, -104, 70, -15, -104, -17, 72, 70, -104, 72, -17, -103, 70, -16, 72, 158, -17, -104, -16, -104, -17, 158, 72, -16, 70, -103, -17, 72, -104, 70, 72, -17, -104, -15, 70, -104, 71, -16, -103, -17, -104, 246, -104},
        {248, -104, -18, -105, -16, 71, -104, 72, -17, -104, -17, 71, 73, -106, 72, -16, -105, 71, -16, 71, 161, -17, -105, -16, -105, -17, 161, 71, -16, 71, -105, -16, 72, -106, 73, 71, -17, -104, -17, 72, -104, 71, -16, -105, -18, -104, 248, -104},
        {253, -107, -16, -108, -17, 74, -108, 73, -17, -106, -17, 73, 74, -107, 72, -17, -107, 74, -17, 73, 163, -17, -107, -16, -107, -17, 163, 73, -17, 74, -107, -17, 72, -107, 74, 73, -17, -106, -17, 73, -108, 74, -17, -108, -16, -107, 253, -106},
        {255, -108, -17, -108, -18, 74, -107, 73, -17, -108, -16, 74, 73, -108, 74, -17, -108, 74, -18, 75, 165, -18, -108, -16, -108, -18, 165, 75, -18, 74, -108, -17, 74, -108, 73, 74, -16, -108, -17, 73, -107, 74, -18, -108, -17, -108, 255, -106},
        {270, -114, -18, -114, -18, 78, -114, 78, -18, -114, -18, 78, 78, -114, 78, -18, -114, 78, -18, 78, 174, -18, -114, -18, -114, -18, 174, 78, -18, 78, -114, -18, 78, -114, 78, 78, -18, -114, -18, 78, -114, 78, -18, -114, -18, -114, 270, -114},
};

int scale_mod210[48] = {270, -114, -18, -114, -18, 78, -114, 78, -18, -114, -18, 78, 78, -114, 78, -18, -114, 78, -18, 78, 174, -18, -114, -18, -114, -18, 174, 78, -18, 78, -114, -18, 78, -114, 78, 78, -18, -114, -18, 78, -114, 78, -18, -114, -18, -114, 270, -114};

int resID_mod210[210] = 
	{-1,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,1,-1,2,-1,-1,-1,3,-1,4,-1,-1,-1,5,-1,-1,-1,-1,-1,6,-1,7,-1,-1,-1,-1,-1,8,-1,-1,-1,9,-1,10,-1,-1,-1,11,-1,-1,-1,-1,-1,12,-1,-1,-1,-1,-1,13,-1,14,-1,-1,-1,-1,-1,15,-1,-1,-1,16,-1,17,-1,-1,-1,-1,-1,18,-1,-1,-1,19,-1,-1,-1,-1,-1,20,-1,-1,-1,-1,-1,-1,-1,21,-1,-1,-1,22,-1,23,-1,-1,-1,24,-1,25,-1,-1,-1,26,-1,-1,-1,-1,-1,-1,-1,27,-1,-1,-1,-1,-1,28,-1,-1,-1,29,-1,-1,-1,-1,-1,30,-1,31,-1,-1,-1,32,-1,-1,-1,-1,-1,33,-1,34,-1,-1,-1,-1,-1,35,-1,-1,-1,-1,-1,36,-1,-1,-1,37,-1,38,-1,-1,-1,39,-1,-1,-1,-1,-1,40,-1,41,-1,-1,-1,-1,-1,42,-1,-1,-1,43,-1,44,-1,-1,-1,45,-1,46,-1,-1,-1,-1,-1,-1,-1,-1,-1,47};


void test_soe(int upper)
{
	uint64 *flagblock64;
	uint8 *sieve;
	int *offsets;
	int *index;
	int *pres;
	int lower = 0;
	int num_elements;
	int modulus = 210; //30;
	int numres = 48;
	int startid = 4; //3;
	int prime;
	int progval;
	int primeres;
	int step;
	int id;		
	int count;
	int b,i,j,k;
	int num_bytes, extra_bits, lastp, lastres, num_blocks;
	uint8 mask;
	int scale_mult, leftover;
	int limit = (int)sqrt(upper) + 1;
	int **psteps;
	int *bbound;

	//timing
	double t,t2 = 0;
	struct timeval tstart, tstop;
	TIME_DIFF *	difference;

	num_elements = (upper - lower) / modulus * numres;
	num_blocks = num_elements / FLAGSIZE + (num_elements % FLAGSIZE > 0);
	extra_bits = num_blocks * FLAGSIZE - num_elements;

	bbound = (int *)malloc(num_blocks * sizeof(int));
	sieve = (uint8 *)malloc(32768 * sizeof(uint8));
	offsets = (int *)malloc(10000 * sizeof(int));
	index = (int *)malloc(10000 * sizeof(int));
	pres = (int *)malloc(10000 * sizeof(int));
	psteps = (int **)malloc(10000 * sizeof(int *));
	for (i=0; i<10000; i++)
		psteps[i] = (int *)malloc(numres * sizeof(int));

	gettimeofday (&tstart, NULL);

	//prepare aux info
	for (k=startid; k<10000; k++)
	{
		int p2res;
		prime = (int)PRIMES[k];
		index[k] = 0;
		
		pres[k] = prime % modulus;
		scale_mult = prime / modulus;
		p2res = (prime * prime) % modulus;		
		
		if (modulus == 30)
		{
			id = resID_mod30[p2res];
			offsets[k] = (prime * prime) / modulus * numres + id;
			for (i=0; i<numres; i++)
			{
				psteps[k][i] = diff_pattern_mod30[resID_mod30[pres[k]]][i] + scale_mod30[i] * scale_mult;
				if (residue_pattern_mod30[resID_mod30[pres[k]]][i] == p2res)
					index[k] = i;
			}
			
		}
		else
		{
			id = resID_mod210[p2res];
			offsets[k] = (prime * prime) / modulus * numres + id;
			for (i=0; i<numres; i++)
			{
				psteps[k][i] = diff_pattern_mod210[resID_mod210[pres[k]]][i] + scale_mod210[i] * scale_mult;
				if (residue_pattern_mod210[resID_mod210[pres[k]]][i] == p2res)
					index[k] = i;
			}
		}

		if (prime > limit)
			break;
	}

	gettimeofday (&tstop, NULL);
	difference = my_difftime (&tstart, &tstop);
	t = ((double)difference->secs + (double)difference->usecs / 1000000);
	free(difference);

	printf("init took %6.4f sec\n",t);
	t2 += t;

	gettimeofday (&tstart, NULL);
	count = 0;
	for (b = 0; b < num_blocks; b++)
	{
		uint32 highmask = 0x3ff;
		memset(sieve, 255, 32768 * sizeof(uint8));
		k = startid;
		prime = (int)PRIMES[k];
		if (b == 0)
			sieve[0] = 0x7f;

		while (prime <= limit)
		{			
			progval = offsets[k]; 
			j = index[k];
			
			if (prime < 8192)
			{
				while (progval < FLAGSIZE)
				{
					if (j >= numres)
					{
						j = 0;
						break;
					}
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
				}

				while (progval < FLAGSIZE >> 1)
				{
					if (j >= numres)
						j = 0;
					//j &= 0x7;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
				}

				while (progval < FLAGSIZE)
				{
					if (j >= numres)
						j = 0;
					//j &= 0x7;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
				}

			}
			else
			{
				while (progval < FLAGSIZE)
				{
					if (j >= numres)
						j = 0;
					//j &= 0x7;
					sieve[progval >> 3] &= masks[progval & 7];
					step = prime + psteps[k][j]; 
					progval += step;		
					j++;
				}
			}
			offsets[k] = progval - FLAGSIZE;
			index[k] = j;

			k++;
			prime = (int)PRIMES[k];
		}

		if (b == (num_blocks - 1))
		{
			for (i = 0; i<extra_bits / 8; i++)
				sieve[32768-i-1] = 0;
			j = i;
			for (i=0; i<extra_bits % 8; i++)
				sieve[j] &= masks[i & 7];
		}

		flagblock64 = (uint64 *)sieve;
		for (i=0;i<4096;i++)
		{
			/* Convert to 64-bit unsigned integer */    
			uint64 x = flagblock64[i];
	    
			/*  Employ bit population counter algorithm from Henry S. Warren's
			 *  "Hacker's Delight" book, chapter 5.   Added one more shift-n-add
			 *  to accomdate 64 bit values.
			 */
			x = x - ((x >> 1) & 0x5555555555555555ULL);
			x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
			x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
			x = x + (x >> 8);
			x = x + (x >> 16);
			x = x + (x >> 32);

			count += (x & 0x000000000000003FULL);
		}
	}

	gettimeofday (&tstop, NULL);
	difference = my_difftime (&tstart, &tstop);
	t = ((double)difference->secs + (double)difference->usecs / 1000000);
	free(difference);

	//count += 4;
	printf("sieving and counting took %6.4f sec\nfound %d primes in %6.4f sec\n",t,count+startid,t+t2);

	free(sieve);
	free(offsets);
	free(index);
	free(pres);
	for (i = 0; i<10000; i++)
		free(psteps[i]);
	free(psteps);

	return;
}






