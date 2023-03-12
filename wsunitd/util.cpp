#include "wsunitd.hpp"

#include <iostream>

#include <signal.h>



bool log::verbose;
void log::debug(string s) { if (verbose) cerr << "[ \x1b[90mdebug\x1b[0m   ] " + s + "\n"; }
void log::note (string s) {              cerr << "[ \x1b[36mnote\x1b[0m    ] " + s + "\n"; }
void log::warn (string s) {              cerr << "[ \x1b[33mwarning\x1b[0m ] " + s + "\n"; }
void log::err  (string s) {              cerr << "[ \x1b[31merror\x1b[0m   ] " + s + "\n"; }
void log::fatal(string s) {              cerr << "[ \x1b[41mfatal\x1b[0m   ] " + s + "\n"; }

bool contains(const vector<weak_ptr<unit>>& v, const string& name) {
	for (auto& d : v)
		if (with_weak_ptr(d, false, [&name](shared_ptr<unit> u){ return name == u->name(); })) return true;

	return false;
}

pid_t fork_exec(const path& p) {
	log::debug("fork_exec " + p.string());

	pid_t pid = fork();

	if (pid == 0) {
		sigset_t sigs;
		assert(sigemptyset(&sigs) == 0);
		assert(sigaddset(&sigs, SIGUSR1) == 0);
		assert(sigaddset(&sigs, SIGUSR2) == 0);
		assert(sigaddset(&sigs, SIGCHLD) == 0);
		assert(sigaddset(&sigs, SIGTERM) == 0);
		assert(sigaddset(&sigs, SIGINT ) == 0);
		assert(sigprocmask(SIG_UNBLOCK, &sigs, 0) == 0);

		execl(p.c_str(), p.c_str(), (char*) NULL);
		log::warn("could not start " + p.string() + ": " + strerror(errno));
		exit(1);
	}

	else if (pid < 0)
		log::warn("could not start " + p.string() + ": " + strerror(errno));

	return pid;
}

string signal_string(int signum) {
	return string("SIG") + sigabbrev_np(signum) + " (" + to_string(signum) + ")";
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
