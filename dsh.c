#include "dsh.h"

/*
 * Notes for myself
 * $PATH, chdir (directory)
 * job_is_stopped() and job_is_completed() (jobstatus)
 * dsh.log, use O_CREAT
 * gcc (c filename) -o devil
 */

void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */

/* Sets the process group id for a given job and process */
int set_child_pgid(job_t *j, process_t *p)
{
    if (j->pgid < 0) /* first child: use its pid for job pgid */
        j->pgid = p->pid;
    return(setpgid(p->pid,j->pgid));
}

/* Creates the context for a new child by setting the pid, pgid and tcsetpgrp */

void new_child(job_t *j, process_t *p, bool fg)
{
    /* establish a new process group, and put the child in
     * foreground if requested
     */
    
    /* Put the process into the process group and give the process
     * group the terminal, if appropriate.  This has to be done both by
     * the dsh and in the individual child processes because of
     * potential race conditions.
     * */
    
    p->pid = getpid();
    
    /* also establish child process group in child to avoid race (if parent has not done it yet). */
    set_child_pgid(j, p);
    
    if(fg) // if fg is set
		seize_tty(j->pgid); // assign the terminal
    
    /* Set the handling for job control signals back to the default. */
    signal(SIGTTOU, SIG_DFL);
}

job_t * joblist=NULL;
static int log;

/* Spawning a process with job control. fg is true if the
 * newly-created process is to be placed in the foreground.
 * (This implicitly puts the calling process in the background,
 * so watch out for tty I/O after doing this.) pgid is -1 to
 * create a new job, in which case the returned pid is also the
 * pgid of the new job.  Else pgid specifies an existing job's
 * pgid: this feature is used to start the second or
 * subsequent processes in a pipeline.
 * */



/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j)
{
    if(kill(-j->pgid, SIGCONT) < 0)
        perror("kill(SIGCONT)");
}
void spawn_job(job_t *j, bool fg)
{
    
    pid_t pid;
    process_t *p;

    int iterator; // To avoid having to use C99 mode, idk if necessary

    // I/O stuff
    int fdinput;
    int fdoutput;
        
    // Setting up pipes
    int i,k;
    i=0, k=0;
    process_t *q;
    for(q = j->first_process; q; q = q->next) i++;
    
    int pipefd[2*(i-1)];
    for(iterator=0; iterator<i-1; iterator++) pipe(pipefd+2*iterator);
    
    
    // Iterate through each process
    for(p = j->first_process; p; p = p->next) {
        
    /* YOUR CODE HERE? */
    /* Builtin commands are already taken care earlier */
        
        switch (pid = fork()) {
                
            case -1: /* fork failure */
                perror("fork");
                exit(EXIT_FAILURE);
                
            case 0: /* child process  */
                p->pid = getpid();
                new_child(j, p, fg);
                                                
                // input/output stuff
                if(p->ifile!=NULL) {
                    fdinput = open(p->ifile, O_RDWR);
                    if(fdinput < 0) {
                        perror("Cannot open output file\n");
                        exit(1);
                    }
                    else if(fdinput!=0 && k==1) dup2(fdinput, 0);
                }

                if(p->ofile!=NULL) {
                    fdoutput = open(p->ifile, O_RDWR | O_CREAT | O_EXCL,
                                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    if(fdoutput < 0) {
                        perror("Cannot open output file\n");
                        exit(1);
                    }
                    else if(fdinput!=0 && k==i) dup2(fdoutput, 1);
                }
                
                // pipes
                
                if(i>1){                    
                    if(k==0) {
                        dup2(pipefd[1], 1);
                    }

                    else if(k==i-1){
                        dup2(pipefd[2*k-2], 0);
                    }
                    
                    else{
                        dup2(pipefd[2*k-2], 0);
                        dup2(pipefd[2*k+1], 1);
                        printf("Hello\n");
                    }
                    
                    for(iterator=0; iterator<i; iterator++) {
                        close(pipefd[iterator]);
                        printf("close\n");
                    }
                }
                    
                if (j->pgid<0)
                    j->pgid=getpid();
                    p->pid = 0;
                    
                if (!setpgid(0,j->pgid) && fg) tcsetpgrp(STDIN_FILENO,j->pgid);


                if(execvp(p->argv[0],p->argv)<0){
                    perror("exec failed")  ;           
             }
                
            default: /* parent */
                /* establish child process group */

                k++;
                
                p->pid = pid;
                set_child_pgid(j, p);
                
                if(j->pgid<0) j->pgid = pid;
                setpgid(pid, j->pgid);

                /* YOUR CODE HERE?  Parent-side code for new process.  */
        }
        
        if(i>1){
            for(iterator=0; iterator<i; iterator++) {
                close(pipefd[iterator]);
            }
        }
        if(fdinput!=0) dup2(0, fdinput);
        if(fdoutput!=0) dup2(1, fdoutput);
        
        int status;
        
        if (fg) {
            int it;
            int pid;
            for(it=0;it<i;it++){
                bool asdf;
                int q; 
                process_t *y;
                switch (pid = waitpid(WAIT_ANY,&status,WUNTRACED)) {

                    case -1:
                        perror("waitpid");
                        p->status = status;
                        exit(EXIT_FAILURE);

                    case 0:
                        printf("Process still running\n");
                        p->status= status;
                        break;

                    default:
                        asdf= false;
                        q= 0;
                        for(y = j->first_process; y; y = y->next) {
                            ++q;
                            p = y;
                            if((y->pid)==pid) break;
                            if (q ==i ) asdf = true;
                        }
                        
                        if (asdf) {
                            --it;
                            continue;
                        }
                        p->status = status;
                        if (WIFSTOPPED(status)){ 
                            p->stopped = true;
                        } 
                        else{
                            p->completed = true;
                        }
                        break;
                    
                }
            }    
        }
        else {
        
            printf("Child in background\n");
            printf("PID: %d\n", pid);
        }
                   
    /* YOUR CODE HERE?  Parent-side code for new job.*/
    seize_tty(getpid()); // assign the terminal back to dsh
    
    }    

}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 */

char* check_status( job_t* job){
    char* status;
    
    if(job_is_completed(job)){
        status="COMPLETED";
    }else if(job_is_stopped(job)){
        status="STOPPED";
    }else{
        status="RUNNING";
    }
    return status;


}
void cleanup(){
    job_t*cur=joblist;
    while(cur!=NULL){
 
        if(job_is_completed(cur)&&cur->notified){
                     
            delete_job(cur,joblist);
          
            }else{            
                cur=cur->next;
              
            }        
            
    }

}
bool builtin_cmd(job_t *last_job, int argc, char **argv)
{
    
    /* check whether the cmd is a built in command
     */
    
    if (!strcmp(argv[0], "quit")) {
        /* Your code here */
        // Already done
        exit(EXIT_SUCCESS);
    }
    else if (!strcmp("jobs", argv[0])) {
        /* Your code here */
        last_job->notified=true;
        job_t* cur;
        
        for(cur=joblist;cur!=NULL;cur=cur->next){
                if(!(cur->pgid==-1 ||cur->notified)){                   
                      
                    if(cur->bg){
                        process_t* curp;
                        for(curp=cur->first_process;curp!=NULL;curp=curp->next){
                            int status;
                            if(waitpid(curp->pid, &status, WNOHANG)>0){
                                curp->status=status;
                                curp->completed=true;
                            }
                        }       
                    }

                
                char * status = check_status(cur);
                if(status=="COMPLETED"){
                    cur->notified=true;
                }
                printf("%u: %s %s \n",cur->pgid,status, cur->commandinfo);
            
                
            }
        }
        
        return true;
    }
    else if (!strcmp("cd", argv[0])) {
        /* Your code here */
        chdir(argv[1]);
        return true;
    }
    else if (!strcmp("bg", argv[0])) {
        /* Your code here */
        if(!argv[1])    {
            printf("no pgid\n");
            return true;
        }
        process_t* p;
        job_t * curj=NULL;
        job_t * it;
        for(it = joblist;it!=NULL;it=it->next){
            if(it->pgid==atoi(argv[1])){
                curj=it;
            }
        }
        if(curj==NULL){
            printf("did not recognize pgid\n");
            return true;

        }
       
       
        for(p=curj->first_process;p!=NULL;p=p->next){
            p->stopped=false;
        }
        curj->bg=true;
        printf("%s - %d\n",curj->first_process->argv[0], curj->pgid);

        continue_job(curj);
        return true;
    }
    else if (!strcmp("fg", argv[0])) {
        /* Your code here */
        if (!argv[1]){
            return true;
        }

        int pid = atoi(argv[1]);
        job_t * curj=NULL;
        job_t * it;
        for(it = joblist;it!=NULL;it=it->next){
            if(it->pgid==atoi(argv[1])){
                curj=it;
            }
        }
        if(curj==NULL){
            printf("did not recognize pgid\n");
            return true;

        }
        if(job_is_stopped(curj)){
            printf("continuing\n");
            continue_job(curj);
        }
        int status;

        if(waitpid(pid,&status,0)>0){
            printf("%d\n",pid);

        }
        process_t* x;

        for(x=curj->first_process;x;x=x->next){
            x->completed=true;
            x->status=status;

        }
        return true;
    }
    
    return false;       /* not a builtin command */
}

/* Build prompt messaage */
char* promptmsg(char* buf)
{
    char* dir = NULL;
    dir=getcwd(dir,0);
    
    int pid = getpid();
    
    /* Modify this to include pid */
    
    sprintf(buf, "dsh-%i:%s$ ", pid, dir);
    return buf;
}

// Log

void init_log() {
    log = open("dsh.log", O_WRONLY|O_APPEND|O_CLOEXEC|O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    dup2(log, STDERR_FILENO);
}

void close_log() {
    if (log > 0) {
        if (close(log) < 0) {
            perror("Unable to close dsh.log\n");
        }
    }
}

int main(){
    
    init_log();
	init_dsh();
	DEBUG("Successfully initialized\n");
        char buf[150];
	while(1) {
                job_t *j = NULL;
		if(!(j = readcmdline(promptmsg(buf)))) {
			if (feof(stdin)) { /* End of file (ctrl-d) */
				fflush(stdout);
                close_log(); 
				printf("\n");
				exit(EXIT_SUCCESS);
                        }
			continue; /* NOOP; user entered return or spaces with return */
		}
                if(endswith(j->first_process->argv[0],".c")){
                    j->first_process->argv[1]=j->first_process->argv[0];
                    j->first_process->argv[0]="gcc";
                    job_t* aout=(job_t *) malloc(sizeof(job_t));
                    
                    init_job(aout);
                    
                    process_t * aoutp=(process_t*)malloc(sizeof(process_t));
                   
                    init_process(aoutp);
                    
                    readprocessinfo(aoutp,"./a.out");
                    aout->first_process=aoutp;
                    job_t* last = find_last_job(j);
                    
                    last->next = aout;
                 }
        /* Only for debugging purposes to show parser output; turn off in the
         * final code */
        /*if(PRINT_INFO) print_job(j);*/
        
        /* Your code goes here */
        if(joblist==NULL){
            joblist=j;
        }
        else{
            job_t* cur;

            for(cur = joblist;cur->next!=NULL;cur=cur->next){
            		;  
            }
            cur->next=j;
        } 
        job_t* iter; 
        for(iter=j;iter!=NULL;iter=iter->next){
            if(!builtin_cmd(iter,iter->first_process->argc,iter->first_process->argv)){
                
            
               if(!j->bg){
                    spawn_job(iter,true);
                }
                else{
                    spawn_job(iter,false);
                }
            }
          
        }
            }
}
