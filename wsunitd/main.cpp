#include "wsunitd.hpp"

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
				try { depgraph::start_stop_units(); }
				catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
			break;

			case SIGUSR2:
				log::note("received SIGUSR2, refresh dependency graph and recalculate set of needed units");
				try {
					depgraph::refresh();

					try { depgraph::start_stop_units(); }
					catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
				}
				catch (exception& ex) { log::err(string("failed to refresh dependency graph: ") + ex.what()); }
			break;

			case SIGCHLD: {
				log::debug("received SIGCHLD, handle zombies");
				try { waitall(); }
				catch (exception& ex) { log::err(string("failed to wait for children: ") + ex.what()); }
			} break;

			case SIGTERM:
				log::note("received SIGTERM, shut down");
				unit::in_shutdown = true;
				try { depgraph::start_stop_units(); }
				catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
			break;

			case SIGINT:
				log::note("received SIGINT, shut down");
				unit::in_shutdown = true;
				try { depgraph::start_stop_units(); }
				catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
			break;

			default:
				log::debug("ignore signal " + signal_string(signum));
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
	if (!is_directory(unit::confdir / "@default" )) create_directory(unit::confdir / "@default" );
	if (!is_directory(unit::confdir / "@shutdown")) create_directory(unit::confdir / "@shutdown");
	if (!is_directory(unit::statedir             )) create_directory(unit::statedir             );
	if (!is_directory(unit::statedir / "wanted"  )) create_directory(unit::statedir / "wanted"  );
	if (!is_directory(unit::statedir / "masked"  )) create_directory(unit::statedir / "masked"  );
	if (!is_directory(unit::statedir / "state"   )) create_directory(unit::statedir / "state"   );

	depgraph::refresh();
	depgraph::start_stop_units();

	signal_loop();

	return 0;
}
