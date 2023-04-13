#include "wsunitd.hpp"

#include <fstream>
#include <iostream>



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
	if (!is_directory(statedir / "pid"     )) create_directories(statedir / "pid"     );
	if (!is_directory(logdir               )) create_directories(logdir               );
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

	main_loop();

	return 0;
}
