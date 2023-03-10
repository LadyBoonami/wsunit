#include "wsunit.hpp"

#include <iostream>

#include <signal.h>
#include <sys/wait.h>



void signal_loop(void) {
	sigset_t sigs;
	assert(sigemptyset(&sigs) == 0);
	assert(sigaddset(&sigs, SIGUSR1) == 0);
	assert(sigaddset(&sigs, SIGUSR2) == 0);
	assert(sigaddset(&sigs, SIGCHLD) == 0);
	assert(sigaddset(&sigs, SIGTERM) == 0);
	assert(sigaddset(&sigs, SIGINT ) == 0);
	assert(sigprocmask(SIG_BLOCK, &sigs, 0) == 0);

	for (;;) {
		log::debug("check for pending zombies");
		waitall();

		log::debug("wait for next signal");
		int signum;
		assert(sigwait(&sigs, &signum) == 0);

		switch (signum) {
			case SIGUSR1:
				log::note("received SIGUSR1, recalculate set of needed units");
				depgraph::start_stop_units();
			break;

			case SIGUSR2:
				log::note("received SIGUSR2, refresh dependency graph and recalculate set of needed units");
				depgraph::refresh();
				depgraph::start_stop_units();
			break;

			case SIGCHLD: {
				log::debug("received SIGCHLD, handle zombies");
				waitall();
			} break;

			case SIGTERM:
				log::note("received SIGTERM, shut down");
				unit::in_shutdown = true;
				depgraph::start_stop_units();
			break;

			case SIGINT:
				log::note("received SIGINT, shut down");
				unit::in_shutdown = true;
				depgraph::start_stop_units();
			break;

			default:
				log::debug(string("ignore signal SIG") + sigabbrev_np(signum) + "(" + to_string(signum) + ")");
			break;
		}
	}
}

void waitall(void) {
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) term_handle(pid, status);
}

int main(int argc, char** argv) {
	if (argc != 3) {
		cerr << "Usage: " << argv[0] << " <config dir> <state dir>" << endl;
		return 1;
	}

	log::verbose   = false;
	unit::confdir  = argv[1];
	unit::statedir = argv[2];

	if (!is_directory(unit::confdir              )) create_directory(unit::confdir              );
	if (!is_directory(unit::statedir             )) create_directory(unit::statedir             );
	if (!is_directory(unit::statedir / "wanted"  )) create_directory(unit::statedir / "wanted"  );
	if (!is_directory(unit::statedir / "running" )) create_directory(unit::statedir / "running" );
	if (!is_directory(unit::statedir / "ready"   )) create_directory(unit::statedir / "ready"   );

	depgraph::refresh();
	depgraph::start_stop_units();

	signal_loop();

	return 0;
}
