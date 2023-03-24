/* run.c - Process creation via fork(). fork a series of children
 *  and then call functions through array of pointers in each child.
 *  child processes will run in no particular order.  sequential execution
 *  of child processes is not enforced - i.e. course execution granularity
 *  and good concurrency for small design.
 *
 * Copyright (C) 2016. Bryan R. Hinton
 *
 */
#include "libmproc.h"

/** Process packets in packet buffer
 *
 * @param arg
 */
void *processpackets(void *arg)
{
  struct timespec rmtp;
  struct timespec rqtp;
  struct pktbuf *pkbuf;
  struct packet *pkt;
  long nsecs;

  /* check for NULL argument */
  if(arg == NULL)
	  pthread_exit(0);

  /* cast argument to pointer to packet buffer */
  pkbuf = (struct pktbuf *)arg;

  for(;;) {

	  /* process packet in packet buffer */
	pkt = pkt_process(pkbuf);
    if(pkt == NULL) {
        pthread_exit(0);

    }

    rqtp.tv_sec = 0;
    rqtp.tv_nsec = 50000000*pkt->mlt;
    logerr(MLOGSTDERR,"pkt->mlt=%u\n",pkt->mlt);

    /* sleep for tv_nsec nanoseconds to simulate some work */
    nanosleep(&rqtp, &rmtp);

    free(pkt);
  }
}

/**
 *
 * @param arg
 */
void *insertpackets(void *arg)
{
   struct packet *pkt;
   int idx;

   struct pktbuf *pkbuf = (struct pktbuf *)arg;

   /* seed random number generator */
   srand(time(NULL));

/* insert 1000 packets into the packet buffer */
for(idx = 0; idx < 1000; idx++) {

	pkt = (struct packet *)malloc(sizeof(struct packet));

    /* set the packet processing simulation multiplier to 3 */
    pkt->mlt=random()%3;

    /* insert packet in the packet buffer */
    if(pkt_queue(pkbuf,pkt) != 0) {

    	   /* error so exit thread */
    	   pthread_exit(0);
       }

    }
}

int destroypktbuf(struct pktbuf *pkbuf) {

	int ret;

   	/* destroy condition variable */
    ret = pthread_cond_destroy(&pkbuf->pktremoved);

    /* pthread_cond_destroy failed */
    if(ret != 0) {

    	errno = ret;
    	logerr(MLOGSTDERR, "pthread_cond_destroy");

	    /* free packet buffer */
	    free(pkbuf);
	    return(EXIT_FAILURE);
	}

   	/* destroy condition variable */
    ret = pthread_cond_destroy(&pkbuf->pktavail);

    /* pthread_cond_destroy failed */
    if(ret != 0) {

    	errno = ret;
    	logerr(MLOGSTDERR, "pthread_cond_destroy");

	    /* free packet buffer */
	    free(pkbuf);
	    return(EXIT_FAILURE);
	}

	/* destroy mutex */
	ret = pthread_mutex_destroy(&pkbuf->lck);

	/* pthread_mutex_destroy failed */
	if(ret != 0) {

		errno = ret;
		logerr(MLOGSTDERR, "pthread_mutex_destroy");

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	return(EXIT_SUCCESS);
}

/**
 *
 * @param secs
 * @param nsecs
 * @return
 */
int fcna(time_t secs, long nsecs)
{

	pthread_t i1; /* packet insertion thread */
	pthread_t p1; /* packet processing thread */
	pthread_t p2; /* packet processing thread */
	int ret;		    /* pthread return val */
	struct pktbuf *pkbuf;    /* packet buffer */

	/* print function name to stderr */
	logerr(MLOGSTDERR, __FUNCTION__);

	/* allocate packet buffer */
	pkbuf = (struct pktbuf *) malloc(sizeof(struct pktbuf));

	if(pkbuf == NULL)
		return (EXIT_FAILURE); /* error allocating memory */

	/* initialize mutex */
	ret = pthread_mutex_init(&pkbuf->lck,NULL);

	/* pthread_mutex_init failed */
	if(ret != 0) {

		/* handle error if pthread_mutex_init() failed */
		errno = ret;
		logerr(MLOGSTDERR, "pthread_mutex_init");

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	/* initialize synchronization primitives */
	ret = pthread_cond_init(&pkbuf->pktremoved,NULL);

	/* pthread_cond_init failed */
	if(ret != 0) {

		/* handle error if pthread_cond_init() failed */
		errno = ret;
		logerr(MLOGSTDERR, "pthread_cond_init");

		ret = pthread_mutex_destroy(&pkbuf->lck);

		/* pthread_mutex_destroy failed */
		if(ret != 0) {

			errno = ret;
			logerr(MLOGSTDERR, "pthread_mutex_destroy");
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	ret = pthread_cond_init(&pkbuf->pktavail,NULL);

	/* pthread_cond_init failed */
	if(ret != 0) {

		/* handle error if pthread_cond_init() failed */
		errno = ret;
		logerr(MLOGSTDERR, "pthread_cond_init");

		/* destroy condition variable */
		ret = pthread_cond_destroy(&pkbuf->pktremoved);

		/* pthread_cond_destroy failed */
		if(ret != 0) {

			errno = ret;
			logerr(MLOGSTDERR, "pthread_mutex_destroy");

			/* free packet buffer */
			free(pkbuf);
			return(EXIT_FAILURE);
		}

		/* destroy mutex */
		ret = pthread_mutex_destroy(&pkbuf->lck);

		/* pthread_mutex_destroy failed */
		if(ret != 0) {

			errno = ret;
			logerr(MLOGSTDERR, "pthread_mutex_destroy");
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	/* initialize packet index to zero */
	pkbuf->pktbeginidx = 0;

	/* initialize number of packets to zero */
	pkbuf->numpkts = 0;

	/* create packet insertion thread */
	ret = pthread_create(&i1, NULL, insertpackets, (void *)pkbuf);

	/* pthread_create failed */
	if(ret != 0) {

		/* handle error if pthread_create() failed */
		errno = ret;
		logerr(MLOGSTDERR, "pthread_create");

		/* destroy mutex and condition variables */
		if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

			/* exit immediately */
			return(EXIT_FAILURE);
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	/* create packet processing thread */
	ret = pthread_create(&p1, NULL, processpackets, (void *)pkbuf);

	/* pthread_create failed */
	if(ret != 0) {

		/* handle error if pthread_create() failed */
		errno = ret;
		logerr(MLOGSTDERR, "pthread_create");

		/* kill the consumer thread */
		ret = pthread_cancel(i1);

		/* handle error if pthread_cancel() failed */
		if(ret != 0) { /* something went very wrong */

			errno = ret;
			logerr(MLOGSTDERR, "pthread_cancel");
			free(pkbuf);
			return(EXIT_FAILURE);
		}

		/* destroy mutex and condition variables */
		if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

			/* exit immediately */
			return(EXIT_FAILURE);
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	/* create packet processing thread */
	ret = pthread_create(&p2, NULL, processpackets, (void *)pkbuf);

	/* pthread_create failed */
	if(ret != 0) {

		/* handle error if pthread_create() failed */
		errno = ret;
		logerr(MLOGSTDERR, "pthread_create");

		/* kill the first producer thread */
		ret = pthread_cancel(p1);

		/* handle error if pthread_cancel() failed */
		if(ret != 0) { /* something went very wrong */

			errno = ret;
			logerr(MLOGSTDERR, "pthread_cancel");
			free(pkbuf);
			return(EXIT_FAILURE);
		}

		/* kill the consumer thread */
		ret = pthread_cancel(i1);

		/* handle error if pthread_cancel() failed */
		if(ret != 0) { /* something went very wrong */

			errno = ret;
			logerr(MLOGSTDERR, "pthread_cancel");

			free(pkbuf);
			return(EXIT_FAILURE);
		}

		/* destroy mutex and condition variables */
		if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

			/* exit immediately */
			return(EXIT_FAILURE);
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}


	/* wait for packet generation and process threads */
	ret = pthread_join(i1,NULL);

	/* pthread_join failed */
	if(ret != 0) {

		errno = ret;
		logerr(MLOGSTDERR, "pthread_join");

		/* destroy mutex and condition variables */
		if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

			/* exit immediately */
			return(EXIT_FAILURE);
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	/* wait for packet generation and process threads */
	ret = pthread_join(p1,NULL);

	/* pthread_join failed */
	if(ret != 0) {

		errno = ret;
		logerr(MLOGSTDERR, "pthread_join");

		/* destroy mutex and condition variables */
		if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

			/* exit immediately */
			return(EXIT_FAILURE);
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	ret = pthread_join(p2,NULL);

	/* pthread_join failed */
	if(ret != 0) {

		errno = ret;
		logerr(MLOGSTDERR, "pthread_join");

		/* destroy mutex and condition variables */
		if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

			/* exit immediately */
			return(EXIT_FAILURE);
		}

		/* free packet buffer */
		free(pkbuf);
		return(EXIT_FAILURE);
	}

	/* destroy mutex and condition variables */
	if(destroypktbuf(pkbuf) == EXIT_FAILURE) {

		/* exit immediately */
		return(EXIT_FAILURE);
	}

	/* free the packet buffer */
	free(pkbuf);

	return (EXIT_SUCCESS);
}

/**
 *
 * @param secs
 * @param nsecs
 * @return
 */
int fcnb(time_t secs, long nsecs)
{
    struct timespec rqtp;
    struct timespec rmtp;
    int ret;
    int idx;

    rqtp.tv_sec = secs;
    rqtp.tv_nsec = nsecs;

    /* print function name to stderr */
    logerr(MLOGSTDERR, __FUNCTION__);

    /* function input validation - validate arguments */
    if(chkargs(secs, nsecs) < 0)
        return (EXIT_FAILURE);

    /* feature test macro for nanosleep */
#if _POSIX_C_SOURCE >= 199309L

    for(idx = 0; idx < 1000; idx++) {

     	   ret = nanosleep(&rqtp, &rmtp);

     	    /* nanosleep returned -1 and set errno to something
     	     * other than interrupted */
     	    if((ret == -1) && (errno != EINTR)) {

     	        logerr(MLOGSTDERR | MLOGERRNO, "nanosleep");
     	        return(EXIT_FAILURE);
     	    }
     }
#endif

    return (EXIT_SUCCESS);
}

/**
 *
 * @param secs
 * @param nsecs
 * @return
 */
int fcnc(time_t secs, long nsecs)
{
    struct timespec rqtp;
    struct timespec rmtp;
    int ret;
    int idx;

    rqtp.tv_sec = secs;
    rqtp.tv_nsec = nsecs;
    /* print function name to stderr */
    logerr(MLOGSTDERR, __FUNCTION__);

    /* function input validation - validate arguments */
    if(chkargs(secs, nsecs) < 0)
        return (EXIT_FAILURE);

    /* feature test macro for nanosleep */
#if _POSIX_C_SOURCE >= 199309L

    for(idx = 0; idx < 1000; idx++) {

     	   ret = nanosleep(&rqtp, &rmtp);

     	    /* nanosleep returned -1 and set errno to something
     	     * other than interrupted */
     	    if((ret == -1) && (errno != EINTR)) {

     	        logerr(MLOGSTDERR | MLOGERRNO, "nanosleep");
     	        return(EXIT_FAILURE);
     	    }
     }

#endif

    return (EXIT_SUCCESS);
}

/**
 *
 * @param secs
 * @param nsecs
 * @return
 */
int fcnd(time_t secs, long nsecs)
{

    struct timespec rqtp;
    struct timespec rmtp;
    int ret;
    int idx;

    rqtp.tv_sec = secs;
    rqtp.tv_nsec = nsecs*1000000;
    /* print function name to stderr */
    logerr(MLOGSTDERR, __FUNCTION__);

    /* function input validation - validate arguments */
    if(chkargs(secs, nsecs) < 0)
        return (EXIT_FAILURE);

    /* feature test macro for nanosleep */
#if _POSIX_C_SOURCE >= 199309L

    for(idx = 0; idx < 1000; idx++) {

    	   ret = nanosleep(&rqtp, &rmtp);

    	    /* nanosleep returned -1 and set errno to something
    	     * other than interrupted */
    	    if((ret == -1) && (errno != EINTR)) {

    	        logerr(MLOGSTDERR | MLOGERRNO, "nanosleep");
    	        return(EXIT_FAILURE);
    	    }
    }


#endif

    return (EXIT_SUCCESS);
}

/**
 *
 * @param secs
 * @param nsecs
 * @return
 */
int fcne(time_t secs, long nsecs)
{
    /* print function name to stderr */
    logerr(MLOGSTDERR, __FUNCTION__);

    /* function input validation - validate arguments */
    if(chkargs(secs, nsecs) < 0)
        return (EXIT_FAILURE);

    /* feature test macro for nanosleep */
#if _POSIX_C_SOURCE >= 199309L

#endif

    return (EXIT_SUCCESS);
}



/* declare and initialize an array of pointers to functions */
int (*fcnsar[])() = { fcna, fcnb, fcnc, fcnd, fcne };
static const int arlen = 5;

/**
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char **argv)
{
    pid_t pid;
    siginfo_t infop;
    char flags;
    int wstatus;
    int ret;
    int idx;

    /* zero out infop */
    memset(&infop, 0, sizeof(siginfo_t));

    /* verify that program is executed with 0 args */
    if(argc != 1) {

        /* return failure to shell */
        return (EXIT_FAILURE);
    }

    /* verify that program name in argv[0] is null terminated in argv[1] */
    if(argv[argc] != NULL) {

        /* return failure to shell */
        return (EXIT_FAILURE);
    }


    /* iterate through array of function pointers, fork a child,
     * and call each function in the child */
    for(idx = 0; idx < arlen; ++idx) {

        /* fork a child process */
        /* there is no guarantee from the scheduler that the child will run
         * before the parent or the parent before the child */

        if((pid = fork()) < 0) {

            logerr(MLOGSTDERR | MLOGERRNO, "fork");

            /* error forking process */
            exit(EXIT_FAILURE);

        } else if (pid == 0) {

            /* child process - execute function */
            (*fcnsar[idx])(1, 1);
            exit(EXIT_SUCCESS);

        } else { }
    }

    /* parent process */
    /* wait outside of above loop so that all of the child processes start up
     * waiting in the child creation loop would force execution order of child processes
     * which defeats the purpose of forking multiple children */
    for(;;) {

        /* feature test macro for using waitid */
#if defined _XOPEN_SOURCE || defined _POSIX_C_SOURCE || _BSD_SOURCE

        /* wait for any child process by calling waitid with P_ALL
         * second argument is ignored if P_ALL is passed as first.
         * All available options are set in the fourth argument.
         * Zero out infop.si_pid before each call in case there were no children in
         * a waitable state and waitid returned 0. */
        infop.si_pid = 0;
        ret = waitid(P_ALL, pid, &infop, WEXITED | WSTOPPED | WCONTINUED);

        if(ret == -1) {

            /* waitid error */
            /* if errno equals ECHILD, then there are no more children
             * to wait for */
            if(errno == ECHILD) {

                logerr(MLOGSTDERR | MLOGERRNO, "waitid");
                exit(EXIT_SUCCESS);
            } else {

                logerr(MLOGSTDERR | MLOGERRNO, "exit");
                exit(EXIT_FAILURE);
            }
        } else {

            /* si_pid is set when a child changes state */
            if(infop.si_pid > 0) {

                switch(infop.si_code) {

                case CLD_EXITED:
                    logerr(MLOGSTDERR | MLOGERRNO, "child with pid=%d exited. signal=%d. exit status=%d",
                           infop.si_pid, infop.si_signo, infop.si_status );
                    break;

                case CLD_KILLED:
                    logerr(MLOGSTDERR | MLOGERRNO, "child with pid=%d killed. signal=%d", infop.si_pid,
                           infop.si_signo);
                    break;

                case CLD_DUMPED:
                    logerr(MLOGSTDERR | MLOGERRNO, "child with pid=%d dumped. signal=%d", infop.si_pid,
                           infop.si_signo);
                    break;

                case CLD_STOPPED:
                    logerr(MLOGSTDERR | MLOGERRNO, "child with pid=%d stopped. signal=%d", infop.si_pid,
                           infop.si_signo);
                    break;

                case CLD_CONTINUED:
                    logerr(MLOGSTDERR | MLOGERRNO, "child with pid=%d continued. signal=%d", infop.si_pid,
                           infop.si_signo);
                    break;

                case CLD_TRAPPED:
                    logerr(MLOGSTDERR | MLOGERRNO, "child with pid=%d trapped. signal=%d", infop.si_pid,
                           infop.si_signo);
                    break;

                default:
                    break;
                }
            }
        }

        /* otherwise use waitpid */
#else
        /* wait for any child process.
         * waitpid() returns when a child has changed state - terminated,
         * stopped, or resumed. waitpid() will return if an untraced or traced
         * child is stopped or if a stopped child has resumed execution via
         * receipt of a SIGCONT signal */
        if ((ret = waitpid(-1, &wstatus, WCONTINUED)) < 0) {

            /* waitpid error */
            logerr(MLOGSTDERR | MLOGERRNO, "waitpid");
            exit(EXIT_FAILURE);
        } else if (ret == 0) {

            /* child exists but has not changed state */
        } else {


            /* child terminated normally */
            if(WIFEXITED(wstatus)) {

                logerr(MLOGSTDERR | MLOGERRNO, "child returned=%d", WEXITSTATUS(wstatus));
            }

            /* child terminated by a signal */
            if(WIFSIGNALED(wstatus)) {

                logerr(MLOGSTDERR | MLOGERRNO, "child terminated via delivery of signal=%d\n",
                       WTERMSIG(wstatus));
#if defined WCOREDUMP
                if(WCOREDUMP(wstatus)) {

                    logerr(MLOGSTDERR | MLOGERRNO, "core dump");
                }
#endif
            }

            /* child process was stopped by a signal */
            if(WIFSTOPPED(wstatus)) {

                logerr(MLOGSTDERR | MLOGERRNO, "child stopped via delivery of signalno=%d",
                       WTERMSIG(wstatus));
            }
            /* child process was resumed by SIGCONT signal */
            if(WIFCONTINUED(wstatus)) {

                logerr(MLOGSTDERR | MLOGERRNO, "child resumed via delivery of SIGCONT signal");
            }
        }
#endif /* end feature test macro for waitid */
    }

    return (EXIT_SUCCESS);
}
