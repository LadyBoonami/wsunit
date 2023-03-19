#pragma once

#include <deque>
#include <map>
#include <memory>

#include <sys/types.h>

#include <boost/filesystem.hpp>

using namespace std;
using namespace boost;
using namespace boost::filesystem;

class unit {
	friend class depgraph;

	private:
		unit(string name);

	public:
		string name     (void);
		string term_name(void);
		path   dir      (void);
		bool   running  (void);
		bool   ready    (void);

		bool wanted   (void);
		bool needed   (void);
		bool masked   (void);
		bool can_start(void);
		bool can_stop (void);
		bool restart  (void);

		bool has_start_script(void);
		bool has_run_script  (void);
		bool has_stop_script (void);

		static path confdir;
		static path statedir;
		static bool in_shutdown;

		enum state_t { DOWN, IN_START, IN_RUN, UP, IN_STOP };
		static string state_descr     (state_t state);
		static string term_state_descr(state_t state);

	private:
		const string name_;
		state_t state;
		vector<weak_ptr<unit>> deps;
		vector<weak_ptr<unit>> revdeps;
		pid_t running_pid;

		void set_state(state_t state);

	private:
		static void start_step(shared_ptr<unit> u, bool& changed);
		static void stop_step (shared_ptr<unit> u, bool& changed);

		static bool exec_start_script(shared_ptr<unit> u);
		static bool exec_run_script  (shared_ptr<unit> u);
		static bool exec_stop_script (shared_ptr<unit> u);

		static void on_start_exit(pid_t pid, shared_ptr<unit> u, int status);
		static void on_run_exit  (pid_t pid, shared_ptr<unit> u, int status);
		static void on_stop_exit (pid_t pid, shared_ptr<unit> u, int status);
};

class depgraph {
	friend class unit;

	public:
		static void refresh(void);
		static void start_stop_units(void);

		static void start(shared_ptr<unit> u);
		static void stop (shared_ptr<unit> u);

		static void queue_step(void);

		static void report(void);

	private:
		static map<string, shared_ptr<unit>> units;

		static void del_old_units(void);
		static void add_new_units(void);
		static void del_old_deps (void);
		static void add_new_deps (void);

		static void adddep(string fst, string snd);


		static deque<weak_ptr<unit>> to_start;
		static deque<weak_ptr<unit>> to_stop ;

		static void start_step(bool& changed);
		static void stop_step (bool& changed);

		static void write_state(void);
};

class log {
	public:
		static bool verbose;
		static void debug(string s);
		static void note (string s);
		static void warn (string s);
		static void err  (string s);
		static void fatal(string s);
};

bool contains(const vector<weak_ptr<unit>>& v, const string& name);

template <class T, class F, class R> R with_weak_ptr(const weak_ptr<T>& wp, R def, F fn) {
	shared_ptr<T> sp = wp.lock();
	if (!sp) return def;
	return fn(sp);
}

string signal_string(int signum);

typedef void (*term_handler)(pid_t, shared_ptr<unit>, int);
void term_add(pid_t pid, term_handler h, shared_ptr<unit> u);
void term_handle(pid_t pid, int status);

pid_t fork_exec(const path& p);

void signal_loop(void);
void waitall(void);
int main(int argc, char** argv);
