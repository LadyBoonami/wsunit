#pragma once

#include <deque>
#include <map>
#include <memory>

#include <sys/types.h>

#include <boost/filesystem.hpp>

using namespace std;
using namespace boost;
using namespace boost::filesystem;

extern path confdir;
extern path statedir;
extern path logdir;
extern bool in_shutdown;

class unit {
	public:
		unit(string name);

		string name     (void);
		string term_name(void);
		path   dir      (void);
		bool   running  (void);
		bool   ready    (void);

		bool wanted     (void);
		bool needed     (void);
		bool masked     (void);
		bool blocked    (void);
		bool can_start  (string* reason = 0);
		bool can_stop   (string* reason = 0);
		bool restart    (void);
		bool need_settle(void);

		bool has_logrot_script(void);
		bool has_start_script (void);
		bool has_run_script   (void);
		bool has_rdy_script   (void);
		bool has_stop_script  (void);

		enum state_t { DOWN, IN_LOGROT, IN_START, IN_RDY, UP, IN_RDY_ERR, IN_RUN, IN_STOP };
		enum state_t get_state(void);
		static string state_descr     (state_t state);
		static string term_state_descr(state_t state);

		static bool request_start(shared_ptr<unit> u, string* reason = 0);
		static bool request_stop (shared_ptr<unit> u, string* reason = 0);

	private:
		const string name_;
		state_t state;

		pid_t logrot_pid;
		pid_t  start_pid;
		pid_t    rdy_pid;
		pid_t    run_pid;
		pid_t   stop_pid;

		void set_state(state_t state);

	private:
		static void step_have_logrot (shared_ptr<unit> u);
		static void step_have_start  (shared_ptr<unit> u);
		static void step_have_run    (shared_ptr<unit> u);
		static void step_have_rdy    (shared_ptr<unit> u);
		static void step_active_rdy  (shared_ptr<unit> u);
		static void step_active_run  (shared_ptr<unit> u);
		static void step_have_stop   (shared_ptr<unit> u);
		static void step_have_restart(shared_ptr<unit> u);

		static void fork_logrot_script(shared_ptr<unit> u);
		static void fork_start_script (shared_ptr<unit> u);
		static void fork_run_script   (shared_ptr<unit> u);
		static void fork_rdy_script   (shared_ptr<unit> u);
		static void fork_stop_script  (shared_ptr<unit> u);

		static void kill_rdy_script(shared_ptr<unit> u);
		static void kill_run_script(shared_ptr<unit> u);

		static void on_logrot_exit(pid_t pid, shared_ptr<unit> u, int status);
		static void on_start_exit (pid_t pid, shared_ptr<unit> u, int status);
		static void on_rdy_exit   (pid_t pid, shared_ptr<unit> u, int status);
		static void on_run_exit   (pid_t pid, shared_ptr<unit> u, int status);
		static void on_stop_exit  (pid_t pid, shared_ptr<unit> u, int status);
};

class depgraph {
	public:
		static void refresh(void);
		static void start_stop_units(void);

		static void start(shared_ptr<unit> u, bool now = true);
		static void stop (shared_ptr<unit> u, bool now = true);

		static void queue_step(void);
		static bool is_settled(string* reason = 0);

		static void report(void);

		static vector<shared_ptr<unit>> get_deps   (string name);
		static vector<shared_ptr<unit>> get_revdeps(string name);

		class node {
			public:
				shared_ptr<unit> u;
				vector<weak_ptr<node>> deps;
				vector<weak_ptr<node>> revdeps;

				node(shared_ptr<unit> u) : u(u) {}
		};

	private:
		static bool contains(const vector<weak_ptr<node>>& v, const string& name);

		static map<string, shared_ptr<node>> nodes;

		static void del_old_units(void);
		static void add_new_units(void);
		static void del_old_deps (void);
		static void add_new_deps (void);
		static void verify_deps  (void);

		static void visit(const string& name, map<string, bool>& visited, deque<string>& trail);

		static void adddep(string fst, string snd);
		static void rmdep (string fst, string snd);


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

template <class T, class F, class R> R with_weak_ptr(const weak_ptr<T>& wp, R def, F fn) {
	shared_ptr<T> sp = wp.lock();
	if (!sp) return def;
	return fn(sp);
}

template <class C, class Fn> void filter(C& cont, Fn f) {
	auto it = cont.begin();
	while (it != cont.end())
		if (f(*it)) ++it;
		else it = cont.erase(it);
}

string signal_string(int signum);

typedef void (*term_handler)(pid_t, shared_ptr<unit>, int);
void term_add(pid_t pid, term_handler h, shared_ptr<unit> u);
void term_handle(pid_t pid, int status);

pid_t fork_(void);
void output_logfile(const string name);

bool status_ok(shared_ptr<unit> u, const string scriptname, int status);

void mkdirs(void);
void signal_loop(void);
void waitall(void);
int main(int argc, char** argv);
