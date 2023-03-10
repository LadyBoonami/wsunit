#include "wsunit.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// Logging -------------------------------------------------------------------------------------------------------------

bool log::verbose;
void log::debug(string s) { if (verbose) cerr << "[ \x1b[90mdebug\x1b[0m   ] " + s + "\n"; }
void log::note (string s) {              cerr << "[ \x1b[36mnote\x1b[0m    ] " + s + "\n"; }
void log::warn (string s) {              cerr << "[ \x1b[33mwarning\x1b[0m ] " + s + "\n"; }
void log::err  (string s) {              cerr << "[ \x1b[31merror\x1b[0m   ] " + s + "\n"; }
void log::fatal(string s) {              cerr << "[ \x1b[41mfatal\x1b[0m   ] " + s + "\n"; }

// Constructors --------------------------------------------------------------------------------------------------------

unit::unit(string name) : name_(name), state(DOWN), running_pid(0) { }

// Properties ----------------------------------------------------------------------------------------------------------

string unit::name    (void) { return           name_; }
path   unit::confpath(void) { return confdir / name_; }

bool   unit::running (void) { return state == IN_START || state == IN_RUN || state == UP || state == IN_STOP; }
bool   unit::ready   (void) { return                                         state == UP                    ; }

bool unit::needed(void) {
	if (in_shutdown) {
		if (name_ == "shutdown") return true;
	}
	else if (exists(statedir / "wanted" / name_))
		return true;

	for (auto& p : revdeps) {
		auto p_ = p.lock();
		if (p_ && p_->needed()) return true;
	}
	return false;
}

bool unit::can_start(void) {
	for (auto& p : deps) {
		auto p_ = p.lock();
		if (p_ && !p_->ready()) return false;
	}
	return true;
}

bool unit::can_stop(void) {
	for (auto& p : revdeps) {
		auto p_ = p.lock();
		if (p_ && p_->running()) return false;
	}
	return true;
}

bool unit::has_start_script(void) { auto p = confdir / name_ / "start"; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_run_script  (void) { auto p = confdir / name_ / "run"  ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_stop_script (void) { auto p = confdir / name_ / "stop" ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }

// Settings ------------------------------------------------------------------------------------------------------------

path unit::confdir ;
path unit::statedir;
bool unit::in_shutdown;

// Depgraph ------------------------------------------------------------------------------------------------------------

void unit::refresh_depgraph(void) {
	depgraph_del_old_units();
	depgraph_add_new_units();
	depgraph_del_old_deps ();
	depgraph_add_new_deps ();
}

void unit::start_stop_units(void) {
	for (auto& [n, u] : units)
		if (u->needed()) start(u); else stop(u);
}

map<string, shared_ptr<unit>> unit::units;

void unit::depgraph_del_old_units(void) {
	auto it = units.begin();

	while (it != units.end())
		if (is_directory(confdir / it->first))
			++it;

		else if (it->second->running()) {
			// remove all links to the graph, unit will be safely stopped as it is no longer needed(), and then remain
			// idle in the graph until the next refresh

			log::debug("unlink old unit " + it->second->name() + " from depgraph");
			it->second->deps.clear();
			it->second->revdeps.clear();
		}
		else {
			log::debug("remove old unit " + it->second->name() + " from depgraph");
			it = units.erase(it);
		}
}

void unit::depgraph_add_new_units(void) {
	for (directory_entry& d : directory_iterator(confdir)) {
		string n = d.path().filename().string();
		if (units.count(n) == 0) {
			log::debug("add new unit " + n + " to depgraph");
			units.emplace(n, shared_ptr<unit>(new unit(n)));
		}
	}
}

void unit::depgraph_del_old_deps(void) {
	for (auto& [n, u] : units) {
		{
			auto it = u->deps.begin();
			while (it != u->deps.end()) {
				auto du = it->lock();
				if (du && is_directory(du->confpath()) && (exists(u->confpath() / "deps" / du->name()) || exists(du->confpath() / "revdeps" / u->name()))) ++it;
				else {
					log::debug("remove old dep " + n + " -> " + du->name() + " from depgraph");
					it = u->deps.erase(it);
				}
			}
		}

		{
			auto it = u->revdeps.begin();
			while (it != u->revdeps.end()) {
				auto ru = it->lock();
				if (ru && is_directory(ru->confpath()) && (exists(ru->confpath() / "deps" / u->name()) || exists(u->confpath() / "revdeps" / ru->name()))) ++it;
				else {
					log::debug("remove old dep " + ru->name() + " <- " + n + " from depgraph");
					it = u->deps.erase(it);
				}
			}
		}
	}
}

void unit::depgraph_add_new_deps(void) {
	for (auto& [n, u] : units) {
		path up = u->confpath();
		path dp = up / "deps";
		path rp = up / "revdeps";

		if (is_directory(dp))
			for (directory_entry& d : directory_iterator(dp))
				adddep(d.path().filename().string(), n);

		if (is_directory(rp))
			for (directory_entry& r : directory_iterator(rp))
				adddep(n, r.path().filename().string());
	}
}

void unit::adddep(string fst, string snd) {
	if (units.count(fst) == 0) {
		log::warn("could not add dependency between " + fst + " and " + snd + ": unit " + fst + " not found, ignoring...");
		return;
	}

	if (units.count(snd) == 0) {
		log::warn("could not add dependency between " + fst + " and " + snd + ": unit " + snd + " not found, ignoring...");
		return;
	}

	shared_ptr<unit> a = units.at(fst);
	shared_ptr<unit> b = units.at(snd);

	if (!contains(a->revdeps, b->name())) { log::debug("add dep " + snd + " <- " + fst + " to depgraph"); a->revdeps.emplace_back(b); }
	if (!contains(b->   deps, a->name())) { log::debug("add dep " + fst + " -> " + snd + " to depgraph"); b->   deps.emplace_back(a); }
}

// State Transitions ---------------------------------------------------------------------------------------------------

void unit::start(shared_ptr<unit> u) {
	log::debug("add unit " + u->name() + " to start queue");
	to_start.push_back(u);
	start_stop_step();
}

void unit::stop(shared_ptr<unit> u) {
	log::debug("add unit " + u->name() + " to stop queue");
	to_stop.push_back(u);
	start_stop_step();
}

deque<weak_ptr<unit>> unit::to_start;
deque<weak_ptr<unit>> unit::to_stop ;

void unit::start_stop_step(void) {
	stop_step();
	start_step();
}

void unit::start_step(void) {
	log::debug("start queue (length " + to_string(to_start.size()) + "):");
	for (auto& p : to_start) {
		auto p_ = p.lock();
		if (p_) log::debug(" - " + p_->name());
		else    log::debug(" - <stale>");
	}
	log::debug("begin start queue handling");
	auto it = to_start.begin();
	while (it != to_start.end()) {
		auto u = it->lock();
		string reason = "?";

		if (!u) {
			reason = "stale unit";
			goto drop;
		}

		switch (u->state) {
			case IN_START:
			case IN_RUN:
				reason = "already starting";
				goto drop;

			case UP:
				reason = "already started";
				goto drop;

			case IN_STOP:
				reason = "currently stopping";
				goto keep;

			case DOWN:
			break;
		}

		if (!u->can_start()) {
			reason = "waiting for dependencies to be ready";
			goto keep;
		}

		start_step(u);
		reason = "now starting";
		goto drop;

	drop:
		log::debug("drop unit " + (u ? u->name() : string("?")) + " from start queue: " + reason);
		it = to_start.erase(it);
		continue;

	keep:
		log::debug("keep unit " + (u ? u->name() : string("?")) + " in start queue: " + reason);
		++it;
		continue;
	}
	log::debug("end start queue handling");
}

void unit::stop_step(void) {
	log::debug("stop queue (length " + to_string(to_stop.size()) + "):");
	for (auto& p : to_stop) {
		auto p_ = p.lock();
		if (p_) log::debug(" - " + p_->name());
		else    log::debug(" - <stale>");
	}
	log::debug("begin stop queue handling");
	auto it = to_stop.begin();
	while (it != to_stop.end()) {
		auto u = it->lock();
		string reason = "?";

		if (!u) {
			reason = "stale unit";
			goto drop;
		}

		switch (u->state) {
			case IN_START:
			case IN_RUN:
				reason = "currently starting";
				goto keep;

			case UP:
			break;

			case IN_STOP:
				reason = "already stopping";
				goto drop;

			case DOWN:
				reason = "already stopped";
				goto drop;
		}

		if (!u->can_stop()) {
			reason = "waiting for reverse dependencies to go down";
			goto keep;
		}

		stop_step(u);
		reason = "now stopping";
		goto drop;

	drop:
		log::debug("drop unit " + (u ? u->name() : string("?")) + " from stop queue: " + reason);
		it = to_stop.erase(it);
		continue;

	keep:
		log::debug("keep unit " + (u ? u->name() : string("?")) + " in stop queue: " + reason);
		++it;
		continue;
	}
	log::debug("end stop queue handling");
}

void unit::start_step(shared_ptr<unit> u) {
	switch (u->state) {
		case DOWN:
			exec_start_script(u) || exec_run_script(u) || (u->state = UP);
		break;

		case IN_START:
			exec_run_script(u) || (u->state = UP);
		break;

		case IN_RUN:
			u->state = UP;
		break;

		case UP:
		case IN_STOP:
		break;
	}
	write_state();
	start_stop_step();
}

void unit::stop_step(shared_ptr<unit> u) {
	switch (u->state) {
		case DOWN:
		case IN_START:
		break;

		case IN_RUN:
			// TODO

		case UP:
			if (u->running_pid) kill(u->running_pid, SIGTERM);
			else exec_stop_script(u) || (u->state = DOWN);
		break;

		case IN_STOP:
			u->state = DOWN;
		break;
	}
	write_state();
	start_stop_step();
}

bool unit::exec_start_script(shared_ptr<unit> u) {
	if (!u->has_start_script()) return false;
	log::debug(u->name() + ": running start script");
	pid_t pid = fork_exec(confdir / u->name() / "start");
	if (pid > 0) {
		term_add(pid, on_start_exit, u);
		u->running_pid = pid;
		u->state = IN_START;
	}
	return true;
}

bool unit::exec_run_script(shared_ptr<unit> u) {
	if (!u->has_run_script()) return false;
	log::debug(u->name() + ": running run script");
	pid_t pid = fork_exec(confdir / u->name() / "run");
	if (pid > 0) {
		term_add(pid, on_run_exit, u);
		// TODO: check handling
		u->running_pid = pid;
		u->state = UP;
	}
	return true;
}

bool unit::exec_stop_script(shared_ptr<unit> u) {
	if (!u->has_stop_script()) return false;
	log::debug(u->name() + ": running stop script");
	pid_t pid = fork_exec(confdir / u->name() / "stop");
	if (pid > 0) {
		term_add(pid, on_stop_exit, u);
		u->running_pid = pid;
		u->state = IN_STOP;
	}
	return true;
}

// #pragma GCC diagnostic push
// #pragma GCC diagnostic ignored "-Wunused-parameter"
void unit::on_start_exit(pid_t pid, shared_ptr<unit> u, int status) {
	u->running_pid = 0;

	if (WIFEXITED(status)) {
		log::debug(u->name() + ": start script exited with code " + to_string(WEXITSTATUS(status)));
		if (WEXITSTATUS(status) == 0) start_step(u);
		else exec_start_script(u);
	}
	else if (WIFSIGNALED(status)) {
		log::debug(u->name() + ": start script terminated by signal " + to_string(WTERMSIG(status)));
		exec_start_script(u);
	}
	else {
		log::warn(u->name() + ": start script in unexpected state");
		term_add(pid, on_start_exit, u);
	}
}

void unit::on_run_exit(pid_t pid, shared_ptr<unit> u, int status) {
	u->running_pid = 0;

	if (WIFEXITED(status)) {
		log::debug(u->name() + ": run script exited with code " + to_string(WEXITSTATUS(status)));
		stop_step(u);
	}
	else if (WIFSIGNALED(status)) {
		log::debug(u->name() + ": run script terminated by signal " + to_string(WTERMSIG(status)));
		stop_step(u);
	}
	else {
		log::warn(u->name() + ": start script in unexpected state");
		term_add(pid, on_run_exit, u);
	}
	// TODO: restart?
}

void unit::on_stop_exit(pid_t pid, shared_ptr<unit> u, int status) {
	u->running_pid = 0;

	if (WIFEXITED(status)) {
		log::debug(u->name() + ": stop script exited with code " + to_string(WEXITSTATUS(status)));
		if (WEXITSTATUS(status) == 0) stop_step(u);
		else exec_stop_script(u);
	}
	else if (WIFSIGNALED(status)) {
		log::debug(u->name() + ": stop script terminated by signal " + to_string(WTERMSIG(status)));
		exec_stop_script(u);
	}
	else {
		log::warn(u->name() + ": stop script in unexpected state");
		term_add(pid, on_stop_exit, u);
	}
	// TODO: restart?
}
// #pragma GCC diagnostic pop

void unit::write_state(void) {
	log::debug("update state files");

	ofstream o(statedir / "state.dot");
	o << "digraph {" << endl;
	for (auto& [n, u] : units) {
		o << "\t\"" << n << "\";" << endl;
		for (auto& d : u->deps) {
			auto d_ = d.lock();
			if (d_) o << "\t\"" << n << "\" -> \"" << d_->name() << "\";" << endl;
		}
		for (auto& r : u->revdeps) {
			auto r_ = r.lock();
			if (r_) o << "\t\"" << r_->name() << "\" -> \"" << n << "\";" << endl;
		}

		auto rf = statedir / "running" / u->name();

		if (u->state != DOWN && !is_regular_file(rf)) ofstream(rf).flush();
		if (u->state == DOWN &&  is_regular_file(rf)) remove(rf);

		rf = statedir / "ready" / u->name();

		if (u->state == UP && !is_regular_file(rf)) ofstream(rf).flush();
		if (u->state != UP &&  is_regular_file(rf)) remove(rf);

		// TODO: pid file?
	}
	o << "}" << endl;
	o.close();
}

// Process Management --------------------------------------------------------------------------------------------------

pid_t fork_exec(const path& p) {
	log::debug("fork_exec " + p.string());

	sigset_t sigs;
	assert(sigemptyset(&sigs) == 0);
	assert(sigaddset(&sigs, SIGUSR1) == 0);
	assert(sigaddset(&sigs, SIGUSR2) == 0);
	assert(sigaddset(&sigs, SIGCHLD) == 0);
	assert(sigaddset(&sigs, SIGTERM) == 0);
	assert(sigaddset(&sigs, SIGINT ) == 0);
	assert(sigprocmask(SIG_UNBLOCK, &sigs, 0) == 0);

	pid_t pid = fork();

	if (pid == 0) {
		execl(p.c_str(), p.c_str(), (char*) NULL);
		log::warn("could not start " + p.string() + ": " + strerror(errno));
		exit(1);
	}

	else if (pid < 0)
		log::warn("could not start " + p.string() + ": " + strerror(errno));

	return pid;
}

map<pid_t, pair<term_handler, shared_ptr<unit>>> term_map;

void term_add(pid_t pid, term_handler h, shared_ptr<unit> u) {
	assert(term_map.count(pid) == 0);
	term_map.emplace(pid, pair<term_handler, shared_ptr<unit>>(h, u));
}

void term_handle(pid_t pid, int status) {
	if (term_map.count(pid) > 0) {
		log::debug("handle termination of child process " + to_string(pid));
		auto [h, u] = term_map.at(pid);
		term_map.erase(pid);
		h(pid, u, status);
	}
	else
		log::debug("ignore termination of child process " + to_string(pid));
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
		int signum;
		assert(sigwait(&sigs, &signum) == 0);
		switch (signum) {
			case SIGUSR1:
				log::debug("received SIGUSR1, recalculate set of needed units");
				unit::start_stop_units();
			break;

			case SIGUSR2:
				log::debug("received SIGUSR1, refresh dependency graph and recalculate set of needed units");
				unit::refresh_depgraph();
				unit::start_stop_units();
			break;

			case SIGCHLD: {
				log::debug("received SIGCHLD, handle terminating children");
				int status;
				pid_t pid;
				while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
					term_handle(pid, status);

			} break;

			case SIGTERM:
				log::debug("received SIGTERM, switch to shutdown unit");
				unit::in_shutdown = true;
				unit::start_stop_units();
			break;

			case SIGINT:
				log::debug("received SIGINT, switch to shutdown unit");
				unit::in_shutdown = true;
				unit::start_stop_units();
			break;

			default:
			break;
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 3) {
		cerr << "Usage: " << argv[0] << " <config dir> <state dir>" << endl;
		return 1;
	}

	log::verbose   = true;

	unit::confdir  = argv[1];
	unit::statedir = argv[2];

	if (!is_directory(unit::confdir              )) create_directory(unit::confdir              );
	if (!is_directory(unit::confdir  / "shutdown")) create_directory(unit::confdir  / "shutdown");
	if (!is_directory(unit::statedir             )) create_directory(unit::statedir             );
	if (!is_directory(unit::statedir / "wanted"  )) create_directory(unit::statedir / "wanted"  );
	if (!is_directory(unit::statedir / "running" )) create_directory(unit::statedir / "running" );
	if (!is_directory(unit::statedir / "ready"   )) create_directory(unit::statedir / "ready"   );

	unit::refresh_depgraph();
	unit::start_stop_units();

	signal_loop();

	return 0;
}

bool contains(const vector<weak_ptr<unit>>& v, const string& name) {
	for (auto& d : v) {
		auto d_ = d.lock();
		if (d_ && name == d_->name()) {
			return true;
		}
	}
	return false;
}
