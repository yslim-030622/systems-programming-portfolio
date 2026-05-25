#include "wsh.h"
#include "dynamic_array.h"
#include "utils.h"
#include "hash_map.h"

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // fork, execv, access, dup2
#include <sys/wait.h>   // waitpid

// Global variables
int rc;
HashMap *alias_hm = NULL;              // alias table
static DynamicArray *history_da = NULL; // command history

/***************************************************
 * Helper Functions
 ***************************************************/
/**
 * @Brief Free any allocated global resources
 */
void wsh_free(void)
{
  // Free any allocated resources here
  if (alias_hm != NULL) { hm_free(alias_hm); alias_hm = NULL; }
  if (history_da != NULL) { da_free(history_da); history_da = NULL; }
}

/**
 * @Brief Cleanly exit the shell after freeing resources
 *
 * @param return_code The exit code to return
 */
void clean_exit(int return_code)
{
  wsh_free();
  exit(return_code);
}

/**
 * @Brief Print a warning message to stderr and set the return code
 *
 * @param msg The warning message format string
 * @param ... Additional arguments for the format string
 */
void wsh_warn(const char *msg, ...)
{
  va_list args;
  va_start(args, msg);

  vfprintf(stderr, msg, args);
  va_end(args);
  rc = EXIT_FAILURE;
}

/***************************************************
 * Parsing
 ***************************************************/
/**
 * @Brief Parse a command line into arguments without doing
 * any alias substitutions.
 * Handles single quotes to allow spaces within arguments.
 *
 * @param cmdline The command line to parse
 * @param argv Array to store the parsed arguments (must be preallocated)
 * @param argc Pointer to store the number of parsed arguments
 */
void parseline_no_subst(const char *cmdline, char **argv, int *argc)
{
  if (!cmdline)
  {
    *argc = 0;
    argv[0] = NULL;
    return;
  }
  char *buf = strdup(cmdline);
  if (!buf)
  {
    perror("strdup");
    clean_exit(EXIT_FAILURE);
  }
  /* Replace trailing newline with space */
  const size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = ' ';
  else
  {
    char *new_buf = realloc(buf, len + 2);
    if (!new_buf)
    {
      perror("realloc");
      free(buf);
      clean_exit(EXIT_FAILURE);
    }
    buf = new_buf;
    strcat(buf, " ");
  }

  int count = 0;
  char *p = buf;
  while (*p && *p == ' ')
    p++; /* skip leading spaces */

  while (*p)
  {
    char *token_start = p;
    char *token = NULL;
    if (*p == '\'')
    {
      token_start = ++p;
      token = strchr(p, '\'');
      if (!token)
      {
        /* Handle missing closing quote - Print `Missing closing quote` to stderr */
        wsh_warn(MISSING_CLOSING_QUOTE);
        free(buf);
        for (int i = 0; i < count; i++) free(argv[i]);
        *argc = 0;
        argv[0] = NULL;
        return;
      }
      *token = '\0';
      p = token + 1;
    }
    else
    {
      token = strchr(p, ' ');
      if (!token)
        break;
      *token = '\0';
      p = token + 1;
    }
    argv[count] = strdup(token_start);
    if (!argv[count]) {
      perror("strdup");
      for (int i = 0; i < count; i++) free(argv[i]);
      free(buf);
      clean_exit(EXIT_FAILURE);
    }
    count++;
    while (*p && (*p == ' '))
      p++;
  }
  argv[count] = NULL;
  *argc = count;
  free(buf);
}

/* ---------------------------------------------------------------------
 * utilities
 * -------------------------------------------------------------------*/

/* Break the command line into pieces whenever there is a '|',
   but ignore '|' if it is inside single quotes.
   Each segment is copied into a new malloc'd string.
   segs[] gets filled with those pieces, and we return how many we found.
   If there are too many segments or malloc fails, return -1. */
static int split_pipeline(const char *line, char **segs, int max_segs) {
  int nseg = 0;
  int in_single = 0;
  const char *start = line;
  for (const char *p = line; ; p++) {
    char c = *p;
    if (c == '\0') {
      /* handle the last chunk when we reach end of string */
      size_t L = p - start;
      char *seg = (char*)malloc(L + 1);
      if (!seg) { perror("malloc"); return -1; }
      memcpy(seg, start, L); seg[L] = '\0';
      if (nseg < max_segs) segs[nseg++] = seg;
      else { free(seg); return -1; }
      break;
    } else if (c == '\'') {
      /* flip the in_single flag whenever we see a quote */
      in_single = !in_single;
    } else if (c == '|' && !in_single) {
      /* found a pipeline separator outside quotes */
      size_t L = p - start;
      char *seg = (char*)malloc(L + 1);
      if (!seg) { perror("malloc"); return -1; }
      memcpy(seg, start, L); seg[L] = '\0';
      if (nseg < max_segs) segs[nseg++] = seg;
      else { free(seg); return -1; }
      start = p + 1; /* move start pointer to right after '|' */
    }
  }
  return nseg;
}

/* Add one raw line to the command history after execution */
static void add_history_after_exec(const char *line_raw) {
  if (!line_raw) return;
  if (!history_da) history_da = da_create(8); // create history array if not initialized

  char *hist_copy = strdup(line_raw);
  if (!hist_copy) { perror("strdup"); return; }

  // trim trailing newline from fgets so history entries look clean
  size_t n = strlen(hist_copy);
  if (n > 0 && hist_copy[n - 1] == '\n') hist_copy[n - 1] = '\0';

  da_put(history_da, hist_copy);             // store the command in history
  free(hist_copy);                           // free temporary buffer
}

/* ---------------------------------------------------------------------
 * PATH / external command
 * -------------------------------------------------------------------*/

/* Figure out where the command executable lives.
   - If the command starts with '/' or '.', just check it directly.
   - Otherwise, go through the directories listed in PATH.
   - If a valid executable is found, return it. If we had to build
     the full path string, it is returned in *out_heap_path.
   - If nothing works, print the proper error and return NULL. */
static const char* resolve_exec_path(const char *cmd, char **out_heap_path) {
  *out_heap_path = NULL;

  // Case 1: command is already an absolute or relative path
  if (cmd[0] == '/' || cmd[0] == '.') {
    if (access(cmd, X_OK) == 0) return cmd;
    wsh_warn(CMD_NOT_FOUND, cmd);
    return NULL;
  }

  // Case 2: look through PATH environment variable
  const char *P = getenv("PATH");
  if (!P || !*P) {
    wsh_warn(EMPTY_PATH);
    return NULL;
  }

  char *dup = strdup(P);
  if (!dup) { perror("strdup"); return NULL; }

  // Loop over each directory in PATH
  char *save = NULL;
  for (char *dir = strtok_r(dup, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
    if (!*dir) continue;  // skip empty parts
    size_t L = strlen(dir) + 1 + strlen(cmd) + 1;
    char *full = (char *)malloc(L);
    if (!full) { perror("malloc"); free(dup); return NULL; }
    snprintf(full, L, "%s/%s", dir, cmd);
    if (access(full, X_OK) == 0) {
      *out_heap_path = full;  // caller must free this
      free(dup);
      return *out_heap_path;
    }
    free(full);
  }

  // Nothing found
  free(dup);
  wsh_warn(CMD_NOT_FOUND, cmd);
  return NULL;
}

/* ---------------------------------------------------------------------
 * Builtins
 * -------------------------------------------------------------------*/

/* Check if a command name matches one of the built-in shell commands.
   Returns 1 if it's a builtin, 0 otherwise. */
static int is_builtin(const char *name) {
  if (!name) return 0;
  return !strcmp(name, "exit")
      || !strcmp(name, "alias")
      || !strcmp(name, "unalias")
      || !strcmp(name, "which")
      || !strcmp(name, "path")
      || !strcmp(name, "cd")
      || !strcmp(name, "history");
}

/* Implementation of the "path" builtin.*/
static int bi_path(int argc, char **argv) {
  if (argc == 1) { //If no arguments: print the current PATH.
    const char *p = getenv("PATH");
    if (!p) p = "";
    printf("%s\n", p);
    fflush(stdout);
    return EXIT_SUCCESS;
  }
  if (argc == 2) { //If one argument: set PATH to that value
    if (setenv("PATH", argv[1], 1) != 0) { perror("setenv"); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
  }
  wsh_warn(INVALID_PATH_USE); //invalid usage -> print warning.
  return EXIT_FAILURE;
}

/* Implementation of the "alias" builtin.
   Behavior:
   - With no extra args: print all current aliases (sorted).
   - With arguments: define or overwrite an alias.
   - Only supports simple forms like "alias name =" or "alias name = value".
*/
static int bi_alias(int argc, char **argv) 
{
  if (argc == 1) {
    // No arguments -> print all stored aliases
    hm_print_sorted(alias_hm);
    fflush(stdout);
    return EXIT_SUCCESS;
  }

  /* Valid formats:
     1) alias name =                 -> creates an alias with an empty value
     2) alias name = <single_token>  -> creates/updates alias with that one value
     (Spaces in the command must already be handled as a single-quoted token) */
  if (argc >= 3 && strcmp(argv[2], "=") == 0) {
    const char *name = argv[1];

    if (argc == 3) {
      // Case: alias name =   (no value given)
      hm_put(alias_hm, name, "");
      return EXIT_SUCCESS;
    }

    if (argc == 4) {
      // Case: alias name = value
      hm_put(alias_hm, name, argv[3]);
      return EXIT_SUCCESS;
    }

    // If more than one token given -> invalid usage
    wsh_warn(INVALID_ALIAS_USE);
    return EXIT_FAILURE;
  }

  // If syntax doesn't match -> invalid usage
  wsh_warn(INVALID_ALIAS_USE);
  return EXIT_FAILURE;
}

// Remove an alias by name
static int bi_unalias(int argc, char **argv) {
  if (argc == 2) {
    // Only valid if exactly one argument is given (the alias name)
    hm_delete(alias_hm, argv[1]);  
    return EXIT_SUCCESS;
  }
  // Wrong usage (e.g., missing or too many arguments)
  wsh_warn(INVALID_UNALIAS_USE);
  return EXIT_FAILURE;
}

// Change directory
static int bi_cd(int argc, char **argv) {
  if (argc == 1) {
    // No argument -> go to HOME
    const char *h = getenv("HOME");
    if (!h || !*h) { 
      fputs(CD_NO_HOME, stderr); 
      return EXIT_FAILURE; 
    }
    if (chdir(h) != 0) { 
      perror("cd"); 
      return EXIT_FAILURE; 
    }
    return EXIT_SUCCESS;
  }
  if (argc == 2) {
    // One argument -> try to move into that directory
    if (chdir(argv[1]) != 0) { 
      perror("cd"); 
      return EXIT_FAILURE; 
    }
    return EXIT_SUCCESS;
  }
  // Anything else (like more than one argument) -> invalid
  wsh_warn(INVALID_CD_USE);
  return EXIT_FAILURE;
}

// Print where a command comes from (alias, builtin, or external)
static int bi_which(int argc, char **argv) {
  if (argc != 2) {
    // Needs exactly one argument
    wsh_warn(INVALID_WHICH_USE);
    return EXIT_FAILURE;
  }
  const char *name = argv[1];

  // First check if it's an alias
  char *av = hm_get(alias_hm, name);
  if (av) {
    printf(WHICH_ALIAS, name, av);
    fflush(stdout);
    return EXIT_SUCCESS;
  }

  // Then check if it's a builtin
  if (is_builtin(name)) {
    printf(WHICH_BUILTIN, name);
    fflush(stdout);
    return EXIT_SUCCESS;
  }

  // If it's an absolute or relative path, just test directly
  if (name[0] == '/' || name[0] == '.') {
    if (access(name, X_OK) == 0) {
      printf(WHICH_EXTERNAL, name, name);
      fflush(stdout);
      return EXIT_SUCCESS;
    } else {
      printf(WHICH_NOT_FOUND, name);
      fflush(stdout);
      return EXIT_FAILURE;
    }
  }

  // Otherwise, search through $PATH
  const char *P = getenv("PATH");
  if (!P || !*P) {
    printf(WHICH_NOT_FOUND, name);
    fflush(stdout);
    return EXIT_FAILURE;
  }

  char *dup = strdup(P);
  if (!dup) { perror("strdup"); return EXIT_FAILURE; }

  char *save = NULL;
  for (char *dir = strtok_r(dup, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
    if (!*dir) continue;
    size_t L = strlen(dir) + 1 + strlen(name) + 1;
    char *full = (char*)malloc(L);
    if (!full) { perror("malloc"); free(dup); return EXIT_FAILURE; }
    snprintf(full, L, "%s/%s", dir, name);
    if (access(full, X_OK) == 0) {
      printf(WHICH_EXTERNAL, name, full);
      fflush(stdout);
      free(full);
      free(dup);
      return EXIT_SUCCESS;
    }
    free(full);
  }
  free(dup);

  // Not found anywhere
  printf(WHICH_NOT_FOUND, name);
  fflush(stdout);
  return EXIT_FAILURE;
}

// Show history of commands entered
static int bi_history(int argc, char **argv) {
  if (!history_da) return EXIT_SUCCESS;

  if (argc == 1) {
    // No arguments -> print all history entries
    for (size_t i = 0; i < history_da->size; i++) {
      char *s = da_get(history_da, i);
      if (s) { printf("%s\n", s); }
    }
    fflush(stdout);
    return EXIT_SUCCESS;
  }

  if (argc == 2) {
    // One argument -> treat it as a number (nth command)
    char *endp = NULL;
    long v = strtol(argv[1], &endp, 10);
    if (!argv[1][0] || *endp != '\0' || v < 1 || (size_t)v > history_da->size) {
      // Invalid number or out of range
      fputs(HISTORY_INVALID_ARG, stderr);
      return EXIT_FAILURE;
    }
    char *s = da_get(history_da, (size_t)(v - 1));
    if (s) printf("%s\n", s);
    fflush(stdout);
    return s ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // More than one argument -> invalid usage
  wsh_warn(INVALID_HISTORY_USE);
  return EXIT_FAILURE;
}

static int try_run_builtin(int argc, char **argv)
{
  // If there's nothing typed (empty command), just return
  if (argc == 0) return 1; /* nothing */
  const char *name = argv[0];  // the command name

  // Match against all builtins one by one
  if (!strcmp(name, "exit")) {
    // exit should have no arguments, otherwise warn
    if (argc != 1) { 
      wsh_warn(INVALID_EXIT_USE); 
      return 1; 
    }
    // exit the shell using the last return code
    clean_exit(rc); 
  } else if (!strcmp(name, "alias")) {
    rc = bi_alias(argc, argv);   // run alias builtin
    return 1;
  } else if (!strcmp(name, "unalias")) {
    rc = bi_unalias(argc, argv); // run unalias builtin
    return 1;
  } else if (!strcmp(name, "which")) {
    rc = bi_which(argc, argv);   // run which builtin
    return 1;
  } else if (!strcmp(name, "path")) {
    rc = bi_path(argc, argv);    // run path builtin
    return 1;
  } else if (!strcmp(name, "cd")) {
    rc = bi_cd(argc, argv);      // run cd builtin
    return 1;
  } else if (!strcmp(name, "history")) {
    rc = bi_history(argc, argv); // run history builtin
    return 1;
  }

  // If it's not one of the builtins, return 0 so caller knows
  return 0;
}

/* This function checks if the first word of a command (argv[0]) is an alias.
   - If it's not an alias -> just copy argv into a new array and return it.
   - If it is an alias -> expand it into its real command, and then append the
     rest of the original arguments after it.
*/
static char **expand_alias_first_token(char **argv, int argc, int *out_argc)
{
  // Look up the alias table for argv[0].
  char *alias_cmd = hm_get(alias_hm, argv[0]);

  // Case 1: No alias found -> just duplicate the argv array as-is.
  if (!alias_cmd) {
    char **copy = (char**)malloc((argc + 1) * sizeof(char*));
    if (!copy) { perror("malloc"); *out_argc = 0; return NULL; }
    for (int i = 0; i < argc; i++) { copy[i] = strdup(argv[i]); }
    copy[argc] = NULL;
    *out_argc = argc;
    return copy;
  }

  // Case 2: Alias found -> parse the alias string into its own argv form.
  char *a_argv[MAX_ARGS + 1];
  int a_argc = 0;
  parseline_no_subst(alias_cmd, a_argv, &a_argc);

  // New argv will be alias expansion + rest of original arguments.
  int total = a_argc + (argc - 1);
  char **nargv = (char**)malloc((total + 1) * sizeof(char*));
  if (!nargv) {
    perror("malloc");
    for (int i = 0; i < a_argc; i++) free(a_argv[i]);
    *out_argc = 0;
    return NULL;
  }

  // Copy alias-expanded argv first.
  int k = 0;
  for (int i = 0; i < a_argc; i++) nargv[k++] = a_argv[i]; // take ownership of alias tokens

  // Then copy the original args (except argv[0], since alias replaces it).
  for (int i = 1; i < argc; i++) nargv[k++] = strdup(argv[i]);

  nargv[k] = NULL;
  *out_argc = k;
  return nargv;
}

/* ---------------------------------------------------------------------
 * External execution (single command)
 * -------------------------------------------------------------------*/
static void run_external(char **argv, int argc)
{
  char *heap = NULL;
  // Resolve the executable path for argv[0]; may allocate 'heap' if found in PATH
  const char *path = resolve_exec_path(argv[0], &heap);
  if (!path) { /* error already printed by resolver */
    // Couldn't resolve; free the argv we own and return
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    // Fork failed in parent
    perror("fork");
    rc = EXIT_FAILURE;
  } else if (pid == 0) {
    // Child: replace image with the target program
    execv(path, argv);
    // If execv returns, it's an error
    perror("execv");
    _exit(1);
  } else {
    // Parent: wait for child to finish and set rc based on its exit status
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
      perror("waitpid");
      rc = EXIT_FAILURE;
    } else {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) rc = EXIT_SUCCESS;
      else rc = EXIT_FAILURE;
    }
  }

  // Clean up path buffer if we allocated it, and the argv we own
  if (heap) free(heap);
  for (int i = 0; i < argc; i++) free(argv[i]);
  free(argv);
}

/* ---------------------------------------------------------------------
 * Pipelines
 * -------------------------------------------------------------------*/
#define MAX_SEGS 128

static void run_pipeline(char ***segarv, int *segargc, int nseg)
{
  int pipes[MAX_SEGS - 1][2];
  // Create N-1 pipes to connect N processes
  for (int i = 0; i < nseg - 1; i++) {
    if (pipe(pipes[i]) < 0) { perror("pipe"); rc = EXIT_FAILURE; return; }
  }

  pid_t pids[MAX_SEGS];
  pid_t last_pid = -1;

  for (int i = 0; i < nseg; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      // If a fork fails, mark failure and keep trying the rest
      perror("fork");
      rc = EXIT_FAILURE;
      continue;
    }
    if (pid == 0) {
      // Child sets up stdin/stdout to the right ends of the pipes
      if (i > 0) {
        if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) { perror("dup2"); _exit(1); }
      }
      if (i < nseg - 1) {
        if (dup2(pipes[i][1], STDOUT_FILENO) < 0) { perror("dup2"); _exit(1); }
      }
      // Close all pipe fds after dup so children don't keep extra copies open
      for (int k = 0; k < nseg - 1; k++) {
        close(pipes[k][0]);
        close(pipes[k][1]);
      }

      // Run builtin directly in the child, or exec an external
      if (is_builtin(segarv[i][0])) {
        int r = try_run_builtin(segargc[i], segarv[i]);
        (void)r; // r unused; rc already set
        _exit(rc == EXIT_SUCCESS ? 0 : 1);
      } else {
        char *heap = NULL;
        const char *path = resolve_exec_path(segarv[i][0], &heap);
        if (!path) _exit(1);
        execv(path, segarv[i]);
        // Only reached on error
        perror("execv");
        _exit(1);
      }
    } else {
      // Parent tracks pids and remembers the last one to compute final rc
      pids[i] = pid;
      if (i == nseg - 1) last_pid = pid;
    }
  }

  // Parent closes all pipe fds (no longer needed here)
  for (int i = 0; i < nseg - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  // Wait for all children; rc is taken from the last stage's exit status
  int status_last = 1;
  for (int i = 0; i < nseg; i++) {
    int st = 0;
    if (waitpid(pids[i], &st, 0) < 0) { perror("waitpid"); }
    if (pids[i] == last_pid) status_last = st;
  }
  if (WIFEXITED(status_last) && WEXITSTATUS(status_last) == 0) rc = EXIT_SUCCESS;
  else rc = EXIT_FAILURE;

  // Free argv arrays for each segment
  for (int i = 0; i < nseg; i++) {
    for (int j = 0; j < segargc[i]; j++) free(segarv[i][j]);
    free(segarv[i]);
  }
}

/* ---------------------------------------------------------------------
 * Run one input line (handles pipeline / single)
 * -------------------------------------------------------------------*/
static void run_line(const char *line_raw)
{
  if (!line_raw) return;

  /* Build segments (pipeline or single) */
  char *segments[MAX_SEGS];
  int nseg = split_pipeline(line_raw, segments, MAX_SEGS);
  if (nseg < 0) { rc = EXIT_FAILURE; add_history_after_exec(line_raw); return; }
  // nseg == number of '|' pieces. segments[i] are heap-allocated strings.

  if (nseg == 1) {
    // Simple command: parse into argv/argc
    char *argv0[MAX_ARGS + 1];
    int argc0 = 0;
    parseline_no_subst(segments[0], argv0, &argc0);
    free(segments[0]); // done with the raw segment string

    if (argc0 == 0) { add_history_after_exec(line_raw); return; } // blank line

    // Expand alias on first token (if any), preserving the rest of args
    int ex_argc = 0;
    char **ex_argv = expand_alias_first_token(argv0, argc0, &ex_argc);

    // free the originals produced by parseline_no_subst
    for (int i = 0; i < argc0; i++) free(argv0[i]);

    if (!ex_argv) { rc = EXIT_FAILURE; add_history_after_exec(line_raw); return; }

    // If the command is a builtin, run it directly; else exec external
    if (is_builtin(ex_argv[0])) {
      try_run_builtin(ex_argc, ex_argv);

      // free the expanded argv we allocated above
      for (int i = 0; i < ex_argc; i++) free(ex_argv[i]);
      free(ex_argv);

      add_history_after_exec(line_raw);
      return;
    }
    run_external(ex_argv, ex_argc);   // handles fork/exec/wait and frees argv
    add_history_after_exec(line_raw);
    return;
  }

  /* Pipeline path */
  // Allocate containers for each stage's argv/argc
  char ***segarv = (char***)calloc(nseg, sizeof(char**));
  int *segargc   = (int*)calloc(nseg, sizeof(int));
  if (!segarv || !segargc) {
    perror("calloc");
    for (int i = 0; i < nseg; i++) free(segments[i]);
    free(segarv); free(segargc);
    rc = EXIT_FAILURE;
    add_history_after_exec(line_raw);
    return;
  }

  int good = 1;
  for (int i = 0; i < nseg; i++) {
    // Parse each segment into tokens
    char *tmpv[MAX_ARGS + 1];
    int tmpc = 0;
    parseline_no_subst(segments[i], tmpv, &tmpc);
    free(segments[i]); // raw segment string no longer needed
    if (tmpc == 0) {
      wsh_warn(EMPTY_PIPE_SEGMENT);
      good = 0;
    }

    if (tmpc > 0) {
      // Apply alias expansion for the first token of this stage
      int ex_argc = 0;
      char **ex_argv = expand_alias_first_token(tmpv, tmpc, &ex_argc);

      // free the original tokens from parseline_no_subst
      for (int k = 0; k < tmpc; k++) free(tmpv[k]);

      if (!ex_argv) { good = 0; continue; }
      segarv[i] = ex_argv;
      segargc[i] = ex_argc;
    } else {
      // keep a valid empty placeholder to simplify cleanup logic
      segarv[i] = (char**)calloc(1, sizeof(char*));
      segargc[i] = 0;
    }
  }

  if (!good) {
    // cleanup argv memory for any segments we built
    for (int i = 0; i < nseg; i++) {
      for (int j = 0; j < segargc[i]; j++) free(segarv[i][j]);
      free(segarv[i]);
    }
    free(segarv); free(segargc);
    rc = EXIT_FAILURE;
    add_history_after_exec(line_raw);
    return;
  }

  // Fork/exec the pipeline and set rc based on the last stage's status
  run_pipeline(segarv, segargc, nseg);

  // Arrays of pointers freed inside run_pipeline; free the containers here
  free(segarv); free(segargc);
  add_history_after_exec(line_raw);
}

/* ---------------------------------------------------------------------
 * Modes of Execution
 * -------------------------------------------------------------------*/
void interactive_main(void)
{
  char line[MAX_LINE];

  for (;;) {
    fputs(PROMPT, stdout);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      if (ferror(stdin)) {                      // handle read error
        fputs("fgets error\n", stderr);
        clearerr(stdin);
      }
      break;   // stop if EOF or error
    }
    // No is_all_spaces() precheck; run_line handles empty lines
    run_line(line);                     // process the command line
  }
}

int batch_main(const char *script_file)
{
  FILE *fp = fopen(script_file, "r");
  if (!fp) { perror("fopen"); return EXIT_FAILURE; }

  char line[MAX_LINE];
  while (fgets(line, sizeof(line), fp)) {  // read each line from the file
    // No is_all_spaces() precheck; run_line handles empty lines
    run_line(line);                        // process the command line
  }

  if (ferror(fp)) { fclose(fp); return EXIT_FAILURE; } // check for read errors
  fclose(fp);
  return rc;
}

/* ---------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------*/
int main(int argc, char **argv)
{
  alias_hm = hm_create();
  setenv("PATH", "/bin", 1);
  if (argc > 2)
  {
    wsh_warn(INVALID_WSH_USE);
    return EXIT_FAILURE;
  }
  switch (argc)
  {
    case 1:
      interactive_main();
      break;
    case 2:
      rc = batch_main(argv[1]);
      break;
    default:
      break;
  }
  wsh_free();
  return rc;
}
