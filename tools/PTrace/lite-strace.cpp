#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <set>

#include <errno.h>
#include <fcntl.h>
#include <libunwind-ptrace.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace std;

void *PTRACE_OPT = (void *)(PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE);

int run_child(int argc, char **argv) {
  char *args[argc+1];
  memcpy(args, argv, argc * sizeof(char*));
  args[argc] = NULL;
  
  ptrace(PTRACE_TRACEME);
  kill(getpid(), SIGSTOP);
  return execvp(args[0], args);
}

static const int nerrors_max = 100;

int nerrors;
int verbose;
int print_names = 1;

enum
  {
    INSTRUCTION,
    SYSCALL,
    TRIGGER
  }
trace_mode = SYSCALL;
static unw_addr_space_t as;
static struct UPT_info *ui;
#define panic(args...)            \
  do { fprintf (stderr, args); ++nerrors; } while (0)

void do_backtrace(pid_t target_pid) {
  unw_word_t ip, sp, start_ip = 0, off;
  int n = 0, ret;
  unw_proc_info_t pi;
  unw_cursor_t c;
  char buf[512];
  size_t len;

  ui = (UPT_info *)_UPT_create (target_pid);
  ret = unw_init_remote(&c, as, ui);
  if (ret < 0)
    panic ("unw_init_remote() failed: ret=%d\n", ret);
  do {
    if ((ret = unw_get_reg(&c, UNW_REG_IP, &ip)) < 0 || (ret = unw_get_reg(&c, UNW_REG_SP, &sp)) < 0)
      panic("unw_get_reg/unw_get_proc_name() failed: ret=%d\n", ret);

    if (n == 0) start_ip = ip;
    buf[0] = '\0';
    
    if (print_names) unw_get_proc_name (&c, buf, sizeof (buf), &off);

    if (true /*verbose*/) {
      if (off) {
        len = strlen (buf);
        if (len >= sizeof (buf) - 32)
        len = sizeof (buf) - 32;
        sprintf (buf + len, "+0x%lx", (unsigned long) off);
      }
      printf ("%016lx %-32s (sp=%016lx)\n", (long) ip, buf, (long) sp);
    }

    if ((ret = unw_get_proc_info (&c, &pi)) < 0)
      panic ("unw_get_proc_info(ip=0x%lx) failed: ret=%d\n", (long) ip, ret);
    else if (true /*verbose*/)
      printf ("\tproc=%016lx-%016lx\n\thandler=%lx lsda=%lx",
      (long) pi.start_ip, (long) pi.end_ip,
      (long) pi.handler, (long) pi.lsda);

#if UNW_TARGET_IA64
    {
      unw_word_t bsp;
      if ((ret = unw_get_reg (&c, UNW_IA64_BSP, &bsp)) < 0)
        panic ("unw_get_reg() failed: ret=%d\n", ret);
      else if (verbose)
        printf (" bsp=%lx", bsp);
    }
#endif 

    if (verbose) printf ("\n");

    ret = unw_step (&c);
    if (ret < 0) {
      unw_get_reg (&c, UNW_REG_IP, &ip);
      panic ("FAILURE: unw_step() returned %d for ip=%lx (start ip=%lx)\n",
          ret, (long) ip, (long) start_ip);
    }

    if (++n > 64) {
      /* guard against bad unwind info in old libraries... */
      panic ("too deeply nested---assuming bogus unwind (start ip=%lx)\n",
        (long) start_ip);
      break;
    }
    
    if (nerrors > nerrors_max) {
      panic ("Too many errors (%d)!\n", nerrors);
      break;
    }
  } while (ret > 0);

  if (ret < 0)
    panic ("unwind failed with ret=%d\n", ret);

  if (verbose)
    printf ("================\n\n");
}

void  trace_syscall(int pid) {
  int syscall_num = ptrace(PTRACE_PEEKUSER, pid, sizeof(long)*ORIG_RAX);
  if (syscall_num == 0 || syscall_num == 1) {
      // || syscall_num == 9
      // || syscall_num == 17 || syscall_num == 18
      // || syscall_num == 20
      // || (syscall_num >= 42 && syscall_num <= 50)
      // || syscall_num == 54 || syscall_num == 62
      // || syscall_num == 68 || syscall_num == 69
      // || (syscall_num >= 82 && syscall_num <=95)
      // || syscall_num == 202 || syscall_num == 295 || syscall_num == 296) {
    fprintf(stderr, "%d %d\n", pid, syscall_num);
    do_backtrace(pid);
  }
}

void do_trace(pid_t child) {
  int pid, status, wait_errno, event;
  set<int> threads;

  waitpid(child, &status, 0);
  ptrace(PTRACE_SETOPTIONS, child, NULL, PTRACE_OPT);
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  threads.insert(child);

  while (1) {
    if (threads.size() == 0) return;

    pid = waitpid(-1, &status, __WALL);
    wait_errno = errno;
    if (pid < 0) {
      if (wait_errno == EINTR) continue;
      if (threads.size() == 0 && wait_errno == ECHILD) return;
      assert("Unknow waitpid error!" && false);
    }

    /* Is this the very first time we see this tracee stopped? */
    if (threads.count(pid) == 0) {
      if (!WIFSTOPPED(status)) {
        printf("Exit of unknown pid %u seen\n", pid);
        continue;
      }
      ptrace(PTRACE_SETOPTIONS, pid, NULL, PTRACE_OPT);
      threads.insert(pid);
    }

    if (WIFSIGNALED(status) || WIFEXITED(status)) {
      threads.erase(pid);
      continue;
    }
    if (!WIFSTOPPED(status)) {
      fprintf(stderr, "PANIC: pid %u not stopped\n", pid);
      threads.erase(pid);
      continue;
    }

    trace_syscall(pid);
    ptrace(PTRACE_SYSCALL, pid, 0, 0);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s prog args\n", argv[0]);
    exit(1);
  }
  
  as = unw_create_addr_space (&_UPT_accessors, 0);

  pid_t child = fork();
  if (child == 0) {
    return run_child(argc-1, argv+1);
  } else {
    do_trace(child);
  }
  return 0;
}

