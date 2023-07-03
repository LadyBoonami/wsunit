#include <errno.h>
#include <iostream>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "unittool.hpp"

class runas : public tool {
	public:
		runas(void) = default;
		virtual string name (void) { return "runas"; }
		virtual string descr(void) { return "run command as different user"; }
		virtual string usage(void) { return "unittool runas <user> <command> [command args ...]"; }
		virtual ~runas(void) = default;

		virtual void main(int argc, char** argv);
};

void runas::main(int argc, char** argv) {
	if (argc < 4) {
		cerr << "usage: " << usage() << endl;
		exit(1);
	}

	struct passwd* pw = getpwnam(argv[2]);
	if (!pw) {
		cerr << "could not find user: " << argv[2] << endl;
		exit(1);
	}

	if (setgid(pw->pw_gid) != 0) { perror("failed to set gid"); exit(1); }
	if (setuid(pw->pw_uid) != 0) { perror("failed to set uid"); exit(1); }

	execvp(argv[3], argv + 3);

	perror("failed to exec");
	exit(1);
}

void add_runas(void) { tool::add(make_shared<runas>()); }
