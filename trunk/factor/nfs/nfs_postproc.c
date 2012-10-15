#include "nfs.h"
#include "gmp_xface.h"

uint32 do_msieve_filtering(fact_obj_t *fobj, msieve_obj *obj, ggnfs_job_t *job)
{
	FILE *tmp, *logfile;
	uint32 relations_needed;
	uint32 flags = 0;

	flags = flags | MSIEVE_FLAG_USE_LOGFILE;
	if (VFLAG > 0)
		flags = flags | MSIEVE_FLAG_LOG_TO_STDOUT;
	flags = flags | MSIEVE_FLAG_NFS_FILTER;
	obj->flags = flags;

	//msieve: filter
	if (VFLAG >= 0)
		printf("nfs: commencing msieve filtering\n");

	logfile = fopen(fobj->flogname, "a");
	if (logfile == NULL)
	{
		printf("fopen error: %s\n", strerror(errno));
		printf("could not open yafu logfile for appending\n");
	}
	else
	{
		logprint(logfile, "nfs: commencing msieve filtering\n");
		fclose(logfile);
	}

	tmp = fopen(fobj->nfs_obj.fbfile,"r");
	if (tmp == NULL)
		ggnfs_to_msieve(fobj, job);
	//else
	//	fclose(tmp);
	else // test if the fb file is for this job
	{
		mpz_t num, r;
		mpz_init(num);
		mpz_init(r);
		char line[GSTR_MAXSIZE];

		while (fgets(line,GSTR_MAXSIZE,tmp))
		{
			if (line[0] == 'N')
			{
				mpz_set_str(num, line + 2, 0);
				mpz_tdiv_r(r, fobj->nfs_obj.gmp_n, num);
				if (mpz_cmp_ui(r, 0) == 0) // match, do nothing
				{	
					fclose(tmp);
					break;
				}
				else
				{
					if (VFLAG > 0)
						printf("nfs: warning: .fb file didn't match current job, overwriting\n");
					fclose(tmp);
					ggnfs_to_msieve(fobj, job);
					break;
				}
			}
		}
		mpz_clear(r);
		mpz_clear(num);
	}

	printf("%s\n",obj->input); //mp_print(mpN, 10, NULL, gstr1.s));
	relations_needed = nfs_filter_relations(obj, fobj->nfs_obj.gmp_n);

	return relations_needed;
}


void extract_factors(factor_list_t *factor_list, fact_obj_t *fobj)
{
	int i;
	FILE *logfile;
	char c[4];

	// extract the factors
	for (i=0;i<factor_list->num_factors;i++)
	{
		mpz_t tmp;

		//init locals
		mpz_init(tmp);
		
		//convert the factor
		mp_t2gmp(&factor_list->final_factors[i]->factor,tmp);

		//divide it out
		mpz_tdiv_q(fobj->nfs_obj.gmp_n, fobj->nfs_obj.gmp_n, tmp);

		//check if its prime and log accordingly
		if (mpz_probab_prime_p(tmp, NUM_WITNESSES))
		{
			//need to convert to yafu bigint to store
			add_to_factor_list(fobj, tmp);
			strncpy(c,"prp",3);
		}
		else
		{
			add_to_factor_list(fobj, tmp);
			strncpy(c,"C",1);
		}

		logfile = fopen(fobj->flogname, "a");
		if (logfile == NULL)
		{
			printf("fopen error: %s\n", strerror(errno));
			printf("could not open yafu logfile for appending\n");
		}
		else
		{
			logprint(logfile, "%s%d = %s\n",c,
				gmp_base10(tmp), mpz_conv2str(&gstr1.s, 10, tmp));
			fclose(logfile);
		}		

		//free locals
		mpz_clear(tmp);
	}

	//log anything left over
	if (mpz_cmp_ui(fobj->nfs_obj.gmp_n, 1) > 0) 
	{
		char c[4];

		if (mpz_probab_prime_p(fobj->nfs_obj.gmp_n, NUM_WITNESSES))
		{
			add_to_factor_list(fobj, fobj->nfs_obj.gmp_n);
			strncpy(c,"prp",3);			
		}
		else
		{
			add_to_factor_list(fobj, fobj->nfs_obj.gmp_n);
			strncpy(c,"C",1);
		}
		
		logfile = fopen(fobj->flogname, "a");
		if (logfile == NULL)
		{
			printf("fopen error: %s\n", strerror(errno));
			printf("could not open yafu logfile for appending\n");
		}
		else
		{
			logprint(logfile, "%s%d = %s\n",c,
				gmp_base10(fobj->nfs_obj.gmp_n), 
				mpz_conv2str(&gstr1.s, 10, fobj->nfs_obj.gmp_n));
			fclose(logfile);
		}		

		mpz_set_ui(fobj->nfs_obj.gmp_n, 1);
	}

	return;
}

