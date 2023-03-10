#include "wsunit.hpp"

#include <signal.h>



unit::unit(string name) : name_(name), state(DOWN), running_pid(0) { }

string unit::name     (void) { return              name_            ; }
string unit::term_name(void) { return "\x1b[34m" + name_ + "\x1b[0m"; }
path   unit::dir      (void) { return    confdir / name_            ; }

bool   unit::running (void) { return state == IN_START || state == IN_RUN || state == UP || state == IN_STOP; }
bool   unit::ready   (void) { return                                         state == UP                    ; }

bool unit::needed(void) {
	if (in_shutdown)
		return false;

	else if (exists(statedir / "wanted" / name_))
		return true;

	for (auto& p : revdeps)
		if (with_weak_ptr(p, false, [](shared_ptr<unit> p){ return p->needed(); })) return true;

	return false;
}

bool unit::can_start(void) {
	for (auto& p : deps)
		if (with_weak_ptr(p, false, [](shared_ptr<unit> p){ return !p->ready(); })) return false;

	return true;
}

bool unit::can_stop(void) {
	for (auto& p : revdeps)
		if (with_weak_ptr(p, false, [](shared_ptr<unit> p){ return p->running(); })) return false;

	return true;
}

bool unit::has_start_script(void) { auto p = dir() / "start"; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_run_script  (void) { auto p = dir() / "run"  ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_stop_script (void) { auto p = dir() / "stop" ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }


path unit::confdir ;
path unit::statedir;
bool unit::in_shutdown;


void unit::start_step(shared_ptr<unit> u, bool& changed) {
	switch (u->state) {
		case DOWN:
			exec_start_script(u) || exec_run_script(u) || (u->state = UP);
			changed = true;
		break;

		case IN_START:
			exec_run_script(u) || (u->state = UP);
			changed = true;
		break;

		case IN_RUN:
			u->state = UP;
			changed = true;
		break;

		case UP:
		case IN_STOP:
		break;
	}
	switch (u->state) {
		case IN_START:
		case IN_RUN:
			log::note(u->term_name() + " is now running");
		break;

		case UP:
			log::note(u->term_name() + " is now ready");
		break;

		default:
		break;
	}
}

void unit::stop_step(shared_ptr<unit> u, bool& changed) {
	switch (u->state) {
		case DOWN:
		case IN_START:
		break;

		case IN_RUN:
			// TODO

		case UP:
			if (u->running_pid) kill(u->running_pid, SIGTERM);
			else exec_stop_script(u) || (u->state = DOWN);
			changed = true;
		break;

		case IN_STOP:
			u->state = DOWN;
			changed = true;
		break;
	}
	switch (u->state) {
		case IN_STOP:
			log::note(u->term_name() + " is no longer ready");
		break;

		case DOWN:
			log::note(u->term_name() + " is no longer running");
		break;

		default:
		break;
	}
}


bool unit::exec_start_script(shared_ptr<unit> u) {
	if (!u->has_start_script()) return false;
	log::note(u->term_name() + ": exec start script");
	pid_t pid = fork_exec(u->dir() / "start");
	if (pid > 0) {
		term_add(pid, on_start_exit, u);
		u->running_pid = pid;
		u->state = IN_START;
	}
	return true;
}

bool unit::exec_run_script(shared_ptr<unit> u) {
	if (!u->has_run_script()) return false;
	log::note(u->term_name() + ": exec run script");
	pid_t pid = fork_exec(u->dir() / "run");
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
	log::note(u->term_name() + ": exec stop script");
	pid_t pid = fork_exec(u->dir() / "stop");
	if (pid > 0) {
		term_add(pid, on_stop_exit, u);
		u->running_pid = pid;
		u->state = IN_STOP;
	}
	return true;
}


void unit::on_start_exit(pid_t pid, shared_ptr<unit> u, int status) {
	u->running_pid = 0;

	bool _;

	if (WIFEXITED(status)) {
		log::note(u->term_name() + ": start script exited with code " + to_string(WEXITSTATUS(status)));
		if (WEXITSTATUS(status) == 0) start_step(u, _);
		else exec_start_script(u);
	}
	else if (WIFSIGNALED(status)) {
		log::note(u->term_name() + ": start script terminated by signal SIG" + sigabbrev_np(WTERMSIG(status)) + "(" + to_string(WTERMSIG(status)) + ")");
		exec_start_script(u);
	}
	else {
		log::warn(u->term_name() + ": start script in unexpected state");
		term_add(pid, on_start_exit, u);
	}

	depgraph::queue_step();
}

void unit::on_run_exit(pid_t pid, shared_ptr<unit> u, int status) {
	u->running_pid = 0;

	bool _;

	if (WIFEXITED(status)) {
		log::note(u->term_name() + ": run script exited with code " + to_string(WEXITSTATUS(status)));
		stop_step(u, _);
	}
	else if (WIFSIGNALED(status)) {
		log::note(u->term_name() + ": run script terminated by signal SIG" + sigabbrev_np(WTERMSIG(status)) + "(" + to_string(WTERMSIG(status)) + ")");
		stop_step(u, _);
	}
	else {
		log::warn(u->term_name() + ": start script in unexpected state");
		term_add(pid, on_run_exit, u);
	}

	// TODO: restart?

	depgraph::queue_step();
}

void unit::on_stop_exit(pid_t pid, shared_ptr<unit> u, int status) {
	u->running_pid = 0;

	bool _;

	if (WIFEXITED(status)) {
		log::note(u->term_name() + ": stop script exited with code " + to_string(WEXITSTATUS(status)));
		if (WEXITSTATUS(status) == 0) stop_step(u, _);
		else exec_stop_script(u);
	}
	else if (WIFSIGNALED(status)) {
		log::note(u->term_name() + ": stop script terminated by signal SIG" + sigabbrev_np(WTERMSIG(status)) + "(" + to_string(WTERMSIG(status)) + ")");
		exec_stop_script(u);
	}
	else {
		log::warn(u->term_name() + ": stop script in unexpected state");
		term_add(pid, on_stop_exit, u);
	}
	// TODO: restart?

	depgraph::queue_step();
}
