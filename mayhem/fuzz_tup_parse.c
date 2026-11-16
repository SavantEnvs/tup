/*
 * File-input harness for tup's Tupfile parsers.
 *
 *   fuzz_tup_parse tupfile <input>   input is the text of a `Tupfile`
 *   fuzz_tup_parse lua <input>       input is the text of a `Tupfile.lua`
 *
 * Each run builds a throw-away tup repository (`tup init`) in a private temp dir, drops the input in as the
 * Tupfile next to a few fixture files (so globs and %-flags have something to match), and runs `tup parse`
 * in-process through tup's own main(). Process-per-input keeps every result independent of earlier inputs.
 *
 * Tupfiles can run commands (`run`, io.popen); the exec family is stubbed out below so a fuzzed Tupfile
 * cannot spawn anything on the host.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* tup's main() from src/tup/tup/main.c, renamed with objcopy at build time. */
int tup_main(int argc, char **argv);
int init_command(int argc, char **argv);

int execl(const char *p, const char *a, ...) { (void)p; (void)a; errno = EACCES; return -1; }
int execle(const char *p, const char *a, ...) { (void)p; (void)a; errno = EACCES; return -1; }
int execlp(const char *p, const char *a, ...) { (void)p; (void)a; errno = EACCES; return -1; }
int execv(const char *p, char *const a[]) { (void)p; (void)a; errno = EACCES; return -1; }
int execve(const char *p, char *const a[], char *const e[]) { (void)p; (void)a; (void)e; errno = EACCES; return -1; }
int execvp(const char *p, char *const a[]) { (void)p; (void)a; errno = EACCES; return -1; }
int system(const char *c) { (void)c; errno = EACCES; return -1; }
FILE *popen(const char *c, const char *m) { (void)c; (void)m; errno = EACCES; return NULL; }
int posix_spawn(pid_t *pid, const char *p, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
		char *const a[], char *const e[])
{
	(void)pid; (void)p; (void)fa; (void)at; (void)a; (void)e;
	return EACCES;
}
int posix_spawnp(pid_t *pid, const char *p, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
		 char *const a[], char *const e[])
{
	(void)pid; (void)p; (void)fa; (void)at; (void)a; (void)e;
	return EACCES;
}

#define MAX_INPUT (1 << 20)

static char workdir[4096];

static int rm_cb(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
	(void)sb; (void)flag; (void)ftw;
	remove(path);
	return 0;
}

static void cleanup(void)
{
	if(workdir[0] && chdir("/") == 0)
		nftw(workdir, rm_cb, 16, FTW_DEPTH | FTW_PHYS);
}

static int write_file(const char *name, const void *data, size_t len)
{
	FILE *f = fopen(name, "wb");
	if(!f)
		return -1;
	if(len && fwrite(data, 1, len, f) != len) {
		fclose(f);
		return -1;
	}
	return fclose(f);
}

static int write_str(const char *name, const char *text)
{
	return write_file(name, text, strlen(text));
}

int main(int argc, char **argv)
{
	const char *tmp;
	const char *tupfile_name;
	char *input;
	size_t len;
	FILE *in;
	char *init_argv[] = {"--no-sync", NULL};
	char *parse_argv[] = {"tup", "parse", NULL};
	int rc;

	if(argc != 3) {
		fprintf(stderr, "usage: %s tupfile|lua <input file>\n", argv[0]);
		return 2;
	}
	if(strcmp(argv[1], "tupfile") == 0)
		tupfile_name = "Tupfile";
	else if(strcmp(argv[1], "lua") == 0)
		tupfile_name = "Tupfile.lua";
	else {
		fprintf(stderr, "unknown mode '%s'\n", argv[1]);
		return 2;
	}

	in = fopen(argv[2], "rb");
	if(!in) {
		perror(argv[2]);
		return 2;
	}
	input = malloc(MAX_INPUT);
	if(!input)
		return 2;
	len = fread(input, 1, MAX_INPUT, in);
	fclose(in);

	tmp = getenv("TMPDIR");
	if(!tmp || !*tmp)
		tmp = "/tmp";
	snprintf(workdir, sizeof(workdir), "%s/tupfuzz.XXXXXX", tmp);
	if(!mkdtemp(workdir)) {
		perror("mkdtemp");
		return 2;
	}
	atexit(cleanup);
	if(chdir(workdir) < 0) {
		perror("chdir");
		return 2;
	}

	if(!getenv("TUPFUZZ_VERBOSE")) {
		stdout = fopen("quiet.out", "w");
		stderr = fopen("quiet.err", "w");
		if(!stdout || !stderr)
			return 2;
	}
	if(!freopen("/dev/null", "r", stdin))
		return 2;

	if(init_command(1, init_argv) != 0)
		return 2;

	if(mkdir("sub", 0777) < 0 || mkdir("sub/deep", 0777) < 0)
		return 2;
	if(write_str("foo.c", "int foo(void) { return 1; }\n") < 0 ||
	   write_str("bar.c", "int bar(void) { return 2; }\n") < 0 ||
	   write_str("bar.h", "int bar(void);\n") < 0 ||
	   write_str("sub/baz.c", "int baz(void) { return 3; }\n") < 0 ||
	   write_str("sub/deep/qux.c", "int qux(void) { return 4; }\n") < 0 ||
	   write_str("sub/Tupfile", ": foreach *.c |> gcc -c %f -o %o |> %B.o\n") < 0 ||
	   write_str("sub/script.sh", "echo hi\n") < 0 ||
	   write_file(tupfile_name, input, len) < 0)
		return 2;

	rc = tup_main(2, parse_argv);
	free(input);
	return rc == 0 ? 0 : 1;
}
