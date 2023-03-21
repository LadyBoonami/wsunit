#include "wsunitd.hpp"

#include <iostream>

#include <fcntl.h>
#include <signal.h>



bool log::verbose;
void log::debug(string s) { if (verbose) cerr << "[ \x1b[90mdebug\x1b[0m   ] " + s + "\n"; }
void log::note (string s) {              cerr << "[ \x1b[36mnote\x1b[0m    ] " + s + "\n"; }
void log::warn (string s) {              cerr << "[ \x1b[33mwarning\x1b[0m ] " + s + "\n"; }
void log::err  (string s) {              cerr << "[ \x1b[31merror\x1b[0m   ] " + s + "\n"; }
void log::fatal(string s) {              cerr << "[ \x1b[41mfatal\x1b[0m   ] " + s + "\n"; }

pid_t fork_(void) {
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
	}

	else if (pid < 0)
		log::warn(string("could not fork: ") + strerror(errno));

	return pid;
}

void output_logfile(const string name) {
	int fd = open((logdir / name).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd == -1) {
		log::err(string("could not open log file ") + (logdir / name).c_str() + ": " + strerror(errno));
		exit(1);
	}
	if (dup2(fd, 1) == -1) {
		log::err(string("could not set stdout to log file ") + (logdir / name).c_str() + ": " + strerror(errno));
		exit(1);
	}
	if (dup2(fd, 2) == -1) {
		log::err(string("could not set stderr to log file ") + (logdir / name).c_str() + ": " + strerror(errno));
		exit(1);
	}
}

bool status_ok(shared_ptr<unit> u, const string scriptname, int status) {
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 0) {
			log::note(u->term_name() + ": " + scriptname + " script exited with code " + to_string(WEXITSTATUS(status)));
			return true;
		}
		else {
			log::warn(u->term_name() + ": " + scriptname + " script exited with code " + to_string(WEXITSTATUS(status)));
			return false;
		}
	}
	else if (WIFSIGNALED(status)) {
		log::warn(u->term_name() + ": " + scriptname + " script terminated by signal " + signal_string(WTERMSIG(status)));
		return false;
	}
	assert(false);
}

/*
pid_t fork_exec(const path& p, const string logname) {
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

		if (logname != "") {
			int fd = open((logdir / logname).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			if (fd == -1) {
				log::err(string("could not open log file ") + (logdir / logname).c_str() + ": " + strerror(errno));
				exit(1);
			}
			if (dup2(fd, 1) == -1) {
				log::err(string("could not set stdout to log file ") + (logdir / logname).c_str() + ": " + strerror(errno));
				exit(1);
			}
			if (dup2(fd, 2) == -1) {
				log::err(string("could not set stderr to log file ") + (logdir / logname).c_str() + ": " + strerror(errno));
				exit(1);
			}
		}

		execl(p.c_str(), p.c_str(), (char*) NULL);
		log::warn("could not start " + p.string() + ": " + strerror(errno));
		exit(1);
	}

	else if (pid < 0)
		log::warn("could not start " + p.string() + ": " + strerror(errno));

	return pid;
}
*/

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
