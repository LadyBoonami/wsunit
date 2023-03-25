#include "wsunitd.hpp"

#include <fstream>
#include <iostream>

#include <signal.h>
#include <sys/wait.h>



path confdir ;
path statedir;
path logdir  ;
bool in_shutdown;



void mkdirs(void) {
	if (!is_directory(confdir              )) create_directories(confdir              );
	if (!is_directory(confdir / "@default" )) create_directories(confdir / "@default" );
	if (!is_directory(confdir / "@shutdown")) create_directories(confdir / "@shutdown");
	if (!is_directory(statedir             )) create_directories(statedir             );
	if (!is_directory(statedir / "wanted"  )) create_directories(statedir / "wanted"  );
	if (!is_directory(statedir / "masked"  )) create_directories(statedir / "masked"  );
	if (!is_directory(statedir / "state"   )) create_directories(statedir / "state"   );
	if (!is_directory(logdir               )) create_directories(logdir               );
}

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

		depgraph::report();

		log::debug("wait for next signal");
		int signum;
		assert(sigwait(&sigs, &signum) == 0);

		switch (signum) {
			case SIGUSR1:
				log::note("received \x1b[36mSIGUSR1\x1b[0m, recalculate set of needed units");
				try { depgraph::start_stop_units(); }
				catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
			break;

			case SIGUSR2:
				log::note("received \x1b[36mSIGUSR2\x1b[0m, refresh dependency graph and recalculate set of needed units");
				try {
					depgraph::refresh();

					try { depgraph::start_stop_units(); }
					catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
				}
				catch (exception& ex) { log::err(string("failed to refresh dependency graph: ") + ex.what()); }
			break;

			case SIGCHLD: {
				log::debug("received \x1b[36mSIGCHLD\x1b[0m, handle zombies");
				try { waitall(); }
				catch (exception& ex) { log::err(string("failed to wait for children: ") + ex.what()); }
			} break;

			case SIGTERM:
				log::note("received \x1b[36mSIGTERM\x1b[0m, shut down");
				in_shutdown = true;
				try { depgraph::start_stop_units(); }
				catch (exception& ex) { log::err(string("failed to start/stop units: ") + ex.what()); }
			break;

			case SIGINT:
				log::note("received \x1b[36mSIGINT\x1b[0m, shut down");
				in_shutdown = true;
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
	char* tmp;

	tmp = getenv("WSUNIT_VERBOSE");
	log::verbose = tmp && *tmp;

	tmp = getenv("WSUNIT_CONFIG_DIR");
	if (!tmp) {
		log::fatal("WSUNIT_CONFIG_DIR environment variable is not set");
		return 1;
	}
	confdir = tmp;

	tmp = getenv("WSUNIT_STATE_DIR");
	if (!tmp) {
		log::fatal("WSUNIT_STATE_DIR environment variable is not set");
		return 1;
	}
	statedir = tmp;

	tmp = getenv("WSUNIT_LOG_DIR");
	if (!tmp) {
		log::fatal("WSUNIT_LOG_DIR environment variable is not set");
		return 1;
	}
	logdir = tmp;

	mkdirs();
	remove(logdir / "_");
	output_logfile("_");

	depgraph::refresh();
	ofstream(statedir / "wsunitd.pid") << getpid() << endl;
	depgraph::start_stop_units();

	signal_loop();

	return 0;
}
