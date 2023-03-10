#pragma once

#include <deque>
#include <map>
#include <memory>

#include <sys/types.h>

#include <boost/filesystem.hpp>

using namespace std;
using namespace boost;
using namespace boost::filesystem;

// Logging -------------------------------------------------------------------------------------------------------------

class log {
	public:
		static bool verbose;
		static void debug(string s);
		static void note (string s);
		static void warn (string s);
		static void err  (string s);
		static void fatal(string s);
};

class unit {
	// Constructors ----------------------------------------------------------------------------------------------------

	private:
		unit(string name);

	// Properties ------------------------------------------------------------------------------------------------------

	public:
		string name     (void);
		path   confpath (void);
		bool   running  (void);
		bool   ready    (void);

		bool needed   (void);
		bool can_start(void);
		bool can_stop (void);

		bool has_start_script(void);
		bool has_run_script  (void);
		bool has_stop_script (void);

	// Settings --------------------------------------------------------------------------------------------------------

	public:
		static path confdir;
		static path statedir;
		static bool in_shutdown;

	// Depgraph --------------------------------------------------------------------------------------------------------

	public:
		static void refresh_depgraph(void);
		static void start_stop_units(void);

	private:
		static map<string, shared_ptr<unit>> units;

		static void depgraph_del_old_units(void);
		static void depgraph_add_new_units(void);
		static void depgraph_del_old_deps (void);
		static void depgraph_add_new_deps (void);
		static void adddep(string fst, string snd);

	// Object State ----------------------------------------------------------------------------------------------------

	private:
		string name_;
		vector<weak_ptr<unit>> deps;
		vector<weak_ptr<unit>> revdeps;
		enum { DOWN, IN_START, IN_RUN, UP, IN_STOP } state;
		pid_t running_pid;

	// State Transitions -----------------------------------------------------------------------------------------------

	public:
		static void start(shared_ptr<unit> u);
		static void stop (shared_ptr<unit> u);

	private:
		static deque<weak_ptr<unit>> to_start;
		static deque<weak_ptr<unit>> to_stop ;

		static void start_stop_step(void);
		static void start_step     (void);
		static void stop_step      (void);

		static void start_step(shared_ptr<unit> u);
		static void stop_step (shared_ptr<unit> u);

		static bool exec_start_script(shared_ptr<unit> u);
		static bool exec_run_script  (shared_ptr<unit> u);
		static bool exec_stop_script (shared_ptr<unit> u);

		static void on_start_exit(pid_t pid, shared_ptr<unit> u, int status);
		static void on_run_exit  (pid_t pid, shared_ptr<unit> u, int status);
		static void on_stop_exit (pid_t pid, shared_ptr<unit> u, int status);

		static void write_state(void);
};

// Process Management --------------------------------------------------------------------------------------------------

typedef void (*term_handler)(pid_t, shared_ptr<unit>, int);
void term_add(pid_t pid, term_handler h, shared_ptr<unit> u);
void term_handle(pid_t pid, int status);

pid_t fork_exec(const path& p);

void signal_loop(void);
int main(int argc, char** argv);

bool contains(const vector<weak_ptr<unit>>& v, const string& name);
