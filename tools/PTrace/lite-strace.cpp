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
#include <map>
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

#define GET_FUN_NAME

#define USER_DATA_BASE 0x7f0000000000
void *PTRACE_OPT = (void *)(PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE);
static unw_addr_space_t as;

int run_child(int argc, char **argv) {
  char *args[argc+1];
  memcpy(args, argv, argc * sizeof(char*));
  args[argc] = NULL;
  
  ptrace(PTRACE_TRACEME);
  kill(getpid(), SIGSTOP);
  return execvp(args[0], args);
}

set<long> CallIP, LastFun;
#ifdef GET_FUN_NAME
map<long, string> Start2FunName;
#endif

void do_backtrace(pid_t target_pid, struct UPT_info * ui) {
  unw_word_t ip;
  unw_proc_info_t pi;
  unw_cursor_t c;
  long last_fun = 0;

  if (unw_init_remote(&c, as, ui) < 0) return;
  for (int _ = 0; _ < 64; ++_) {
    if (unw_get_reg(&c, UNW_REG_IP, &ip) < 0) break;
    if (ip < USER_DATA_BASE) {
      CallIP.insert(ip - 5);
      LastFun.insert(last_fun);
      return;
    }
    if (unw_get_proc_info (&c, &pi) < 0) break;
    last_fun = pi.start_ip;
    
#ifdef GET_FUN_NAME
    char buf[512]; unw_word_t off;
    buf[0] = '\0';
    unw_get_proc_name(&c, buf, sizeof(buf), &off);
    Start2FunName[last_fun] = buf;
#endif

    if (unw_step(&c) < 0) break;
  }
}

void  trace_syscall(int pid, struct UPT_info * ui) {
  int syscall_num = ptrace(PTRACE_PEEKUSER, pid, sizeof(long)*ORIG_RAX);
  if (syscall_num == 0 || syscall_num == 1
      || syscall_num == 9
      || syscall_num == 17 || syscall_num == 18
      || syscall_num == 20
      || (syscall_num >= 42 && syscall_num <= 50)
      || syscall_num == 54 || syscall_num == 62
      || syscall_num == 68 || syscall_num == 69
      || (syscall_num >= 82 && syscall_num <=95)
      || syscall_num == 202 || syscall_num == 295 || syscall_num == 296) {
    do_backtrace(pid, ui);
  }
}

void do_trace(pid_t child) {
  int pid, status;
  map<int, struct UPT_info *> threads;

  waitpid(child, &status, 0);
  ptrace(PTRACE_SETOPTIONS, child, NULL, PTRACE_OPT);
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  UPT_info *ui = (UPT_info *)_UPT_create (child);
  threads[child] = ui;

  while (!threads.empty()) {
    pid = waitpid(-1, &status, __WALL);
    if (pid < 0) continue;

    if (threads.count(pid) == 0) {
      if (!WIFSTOPPED(status)) continue;
      ptrace(PTRACE_SETOPTIONS, pid, NULL, PTRACE_OPT);

      UPT_info *ui = (UPT_info *)_UPT_create(pid);
      threads[pid] = ui;
    }

    if (WIFSIGNALED(status) || WIFEXITED(status) || !WIFSTOPPED(status)) {
      _UPT_destroy(threads[pid]);
      threads.erase(pid);
      continue;
    }

    trace_syscall(pid, threads[pid]);
    ptrace(PTRACE_SYSCALL, pid, 0, 0);
  }

  puts("=====Callq IP=====");
  for (auto i: CallIP) printf("%lx\n", i);
  puts("=====Called Function=====");
  for (auto i: LastFun) {
#ifdef GET_FUN_NAME
    printf("%lx %s\n", i, Start2FunName[i].c_str());
#else
    printf("%lx\n", i);
#endif
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

