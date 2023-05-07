#include "wsunitd.hpp"

#include <fstream>

#include <signal.h>



unit::unit(string name) : name_(name), state(DOWN), logrot_pid(0), start_pid(0), rdy_pid(0), run_pid(0), stop_pid(0) {
	ofstream(statedir / "state" / name_) << "down" << endl;
}

string unit::name     (void) { return              name_            ; }
string unit::term_name(void) { return "\x1b[34m" + name_ + "\x1b[0m"; }
path   unit::dir      (void) { return    confdir / name_            ; }

bool   unit::running (void) { return state != DOWN; }
bool   unit::ready   (void) { return state == UP  ; }

bool unit::wanted(void) {
	if (in_shutdown) {
		if (name_ == "@shutdown") return true;
	}
	else {
		if (name_ == "@default") return true;
		if (exists(statedir / "wanted" / name_)) return true;
	}

	return false;
}

bool unit::needed(void) {
	if (wanted()) return true;
	for (auto& p : depgraph::get_revdeps(name_)) if (p->needed()) return true;
	return false;
}

bool unit::masked(void) {
	return exists(statedir / "masked" / name_);
}

bool unit::blocked(void) {
	if (masked()) return true;
	for (auto& p : depgraph::get_deps(name_)) if (p->blocked()) return true;
	return false;
}

bool unit::can_start(string* reason) {
	if (need_settle() && !depgraph::is_settled(reason)) return false;
	for (auto& p : depgraph::get_deps(name_))
		if (!p->ready()) {
			if (reason) *reason = "waiting for " + p->term_name() + " to be ready";
			return false;
		}
	return true;
}

bool unit::can_stop(string* reason) {
	for (auto& p : depgraph::get_revdeps(name_))
		if (p->running()) {
			if (reason) *reason = "waiting for " + p->term_name() + " to stop running";
			return false;
		}
	return true;
}

bool unit::restart(void) {
	return exists(dir() / "restart");
}

bool unit::need_settle(void) {
	return name_ == "@shutdown" || exists(dir() / "start-wait-settled");
}

bool unit::has_logrot_script(void) {
	auto p = dir() / "logrotate";
	if (is_regular_file(p) && access(p.c_str(), X_OK) == 0) return true;
	p = confdir / "logrotate";
	if (is_regular_file(p) && access(p.c_str(), X_OK) == 0) return true;
	return false;
}

bool unit::has_start_script(void) { auto p = dir() / "start"    ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_run_script  (void) { auto p = dir() / "run"      ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_rdy_script  (void) { auto p = dir() / "ready"    ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }
bool unit::has_stop_script (void) { auto p = dir() / "stop"     ; return is_regular_file(p) && access(p.c_str(), X_OK) == 0; }

enum unit::state_t unit::get_state(void) { return state; }

#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wmisleading-indentation"

	string unit::state_descr(state_t state) {
		if (state == UP  ) return "ready"  ;
		if (state != DOWN) return "running";
		                   return "down"   ;
	}

	string unit::term_state_descr(state_t state) {
		if (state == UP  ) return "\x1b[32mready\x1b[0m"  ;
		if (state != DOWN) return "\x1b[33mrunning\x1b[0m";
		                   return "\x1b[31mdown\x1b[0m"   ;
	}

#pragma GCC diagnostic pop



bool unit::request_start(string* reason) {
	switch (state) {
		case DOWN:
			if (blocked()) {
				if (reason) *reason = "unit blocked";
				return false;
			}
			if (!can_start(reason))
				return false;

			if (reason) *reason = "now starting";
			step_have_logrot();
			return true;

		case IN_LOGROT:
		case IN_START:
		case IN_RDY:
			if (reason) *reason = "already starting";
			return true;

		case UP:
			if (reason) *reason = "already started";
			return true;

		case IN_RDY_ERR:
		case IN_RUN:
		case IN_STOP:
			if (reason) *reason = "currently stopping";
			return false;
	}
}

bool unit::request_stop(string* reason) {
	switch (state) {
		case DOWN:
			if (reason) *reason = "already stopped";
			return true;

		case IN_LOGROT:
		case IN_START:
		case IN_RDY:
			if (reason) *reason = "currently starting";
			return false;

		case UP:
			if (!can_stop())
				return false;

			if (reason) *reason = "now stopping";
			step_active_run();
			return true;

		case IN_RDY_ERR:
		case IN_RUN:
		case IN_STOP:
			if (reason) *reason = "already stopping";
			return true;
	}
}

void unit::handle(string event) {
	auto p = dir() / "events" / event;
	if (is_regular_file(p) && access(p.c_str(), X_OK) == 0) {
		pid_t pid = fork_();
		if (pid == 0) {
			if (chdir(dir().c_str()) == -1) {
				log::err("failed to chdir to " + dir().string() + ": " + strerror(errno));
				exit(1);
			}
			pid_t sid = setsid();
			if (sid == -1) {
				log::err(string("failed to run setsid: ") + strerror(errno));
				exit(1);
			}
			log::debug("fork events/" + event + " as pid " + to_string(getpid()) + " sid " + to_string(sid));
			output_logfile(name() + ".log");
			log::note("launch ./events/" + event);
			execl(p.c_str(), p.c_str(), name().c_str(), event.c_str(), (char*) NULL);
			exit(1);
		}
		else if (pid > 0)
			term_add(pid, on_event_exit, shared_from_this());
	}
}



void unit::set_state(state_t state) {
	string old_state = state_descr(this->state);
	string new_state = state_descr(      state);

	if (old_state != new_state) {
		log::note(term_name() + ": " + term_state_descr(this->state) + " -> " + term_state_descr(state));
		ofstream(statedir / "state" / name_) << new_state << endl;
	}

	this->state = state;
}

void unit::step_have_logrot (void) { if (has_logrot_script()) fork_logrot_script(); else step_have_start  (); }
void unit::step_have_start  (void) { if (has_start_script ()) fork_start_script (); else step_have_run    (); }
void unit::step_have_run    (void) { if (has_run_script   ()) fork_run_script   (); else step_have_rdy    (); }
void unit::step_have_rdy    (void) { if (has_rdy_script   ()) fork_rdy_script   (); else set_state(UP)      ; }
void unit::step_active_rdy  (void) { if (rdy_pid != 0       ) kill_rdy_script   (); else step_have_stop   (); }
void unit::step_active_run  (void) { if (run_pid != 0       ) kill_run_script   (); else step_have_stop   (); }
void unit::step_have_stop   (void) { if (has_stop_script  ()) fork_stop_script  (); else step_have_restart(); }

void unit::step_have_restart(void) { if (restart() && needed() && !masked()) step_have_logrot(); else set_state(DOWN); }

void unit::fork_logrot_script(void) {
	assert(has_logrot_script());
	log::note(term_name() + ": exec logrotate script");
	pid_t pid = fork_();
	if (pid == 0) {
		if (chdir(logdir.c_str()) == -1) {
			log::err("failed to chdir to " + logdir.string() + ": " + strerror(errno));
			exit(1);
		}
		pid_t sid = setsid();
		if (sid == -1) {
			log::err(string("failed to run setsid: ") + strerror(errno));
			exit(1);
		}
		path p = is_regular_file(dir() / "logrotate") ? (dir() / "logrotate") : (confdir / "logrotate");
		log::debug(string("fork logrotate as pid ") + to_string(getpid()) + " sid " + to_string(sid));
		output_logfile(name() + ".log");
		log::note("launch ./logrotate");
		execl(p.c_str(), p.c_str(), name().c_str(), (char*) NULL);
		exit(1);
	}
	else if (pid > 0) {
		term_add(pid, on_logrot_exit, shared_from_this());
		logrot_pid = pid;
		set_state(IN_LOGROT);
	}
}

void unit::fork_start_script(void) {
	assert(has_start_script());
	log::note(term_name() + ": exec start script");
	pid_t pid = fork_();
	if (pid == 0) {
		if (chdir(dir().c_str()) == -1) {
			log::err("failed to chdir to " + dir().string() + ": " + strerror(errno));
			exit(1);
		}
		pid_t sid = setsid();
		if (sid == -1) {
			log::err(string("failed to run setsid: ") + strerror(errno));
			exit(1);
		}
		log::debug(string("fork start as pid ") + to_string(getpid()) + " sid " + to_string(sid));
		output_logfile(name() + ".log");
		log::note("launch ./start");
		execl((dir() / "start").c_str(), (dir() / "start").c_str(), (char*) NULL);
		exit(1);
	}
	else if (pid > 0) {
		term_add(pid, on_start_exit, shared_from_this());
		start_pid = pid;
		set_state(IN_START);
	}
}

void unit::fork_run_script(void) {
	assert(has_run_script());
	log::note(term_name() + ": exec run script");
	pid_t pid = fork_();
	if (pid == 0) {
		if (chdir(dir().c_str()) == -1) {
			log::err("failed to chdir to " + dir().string() + ": " + strerror(errno));
			exit(1);
		}
		pid_t sid = setsid();
		if (sid == -1) {
			log::err(string("failed to run setsid: ") + strerror(errno));
			exit(1);
		}
		log::debug(string("fork run as pid ") + to_string(getpid()) + " sid " + to_string(sid));
		output_logfile(name() + ".log");
		log::note("launch ./run");
		execl((dir() / "run").c_str(), (dir() / "run").c_str(), (char*) NULL);
		exit(1);
	}
	else if (pid > 0) {
		term_add(pid, on_run_exit, shared_from_this());
		run_pid = pid;
		ofstream(statedir / "pid" / name()) << pid << endl;
		step_have_rdy();
	}
}

void unit::fork_rdy_script(void) {
	assert(has_rdy_script());
	log::note(term_name() + ": exec ready script");
	pid_t pid = fork_();
	if (pid == 0) {
		if (chdir(dir().c_str()) == -1) {
			log::err("failed to chdir to " + dir().string() + ": " + strerror(errno));
			exit(1);
		}
		pid_t sid = setsid();
		if (sid == -1) {
			log::err(string("failed to run setsid: ") + strerror(errno));
			exit(1);
		}
		log::debug(string("fork ready as pid ") + to_string(getpid()) + " sid " + to_string(sid));
		output_logfile(name() + ".log");
		log::note("launch ./ready");
		execl((dir() / "ready").c_str(), (dir() / "ready").c_str(), (char*) NULL);
		exit(1);
	}
	else if (pid > 0) {
		term_add(pid, on_rdy_exit, shared_from_this());
		rdy_pid = pid;
		set_state(IN_RDY);
	}
}

void unit::fork_stop_script(void) {
	assert(has_stop_script());
	log::note(term_name() + ": exec stop script");
	pid_t pid = fork_();
	if (pid == 0) {
		if (chdir(dir().c_str()) == -1) {
			log::err("failed to chdir to " + dir().string() + ": " + strerror(errno));
			exit(1);
		}
		pid_t sid = setsid();
		if (sid == -1) {
			log::err(string("failed to run setsid: ") + strerror(errno));
			exit(1);
		}
		log::debug(string("fork stop as pid ") + to_string(getpid()) + " sid " + to_string(sid));
		output_logfile(name() + ".log");
		log::note("launch ./stop");
		execl((dir() / "stop").c_str(), (dir() / "stop").c_str(), (char*) NULL);
		exit(1);
	}
	else if (pid > 0) {
		term_add(pid, on_stop_exit, shared_from_this());
		stop_pid = pid;
		set_state(IN_STOP);
	}
}

void unit::kill_rdy_script(void) {
	assert(rdy_pid);
	log::debug(term_name() + ": kill(" + to_string(rdy_pid) + ", " + signal_string(SIGTERM) + ")");
	kill(rdy_pid, SIGTERM);
	set_state(IN_RDY_ERR);
}

void unit::kill_run_script(void) {
	assert(run_pid);
	log::debug(term_name() + ": kill(" + to_string(run_pid) + ", " + signal_string(SIGTERM) + ")");
	kill(run_pid, SIGTERM);
	set_state(IN_RUN);
}

#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-parameter"

	void unit::on_logrot_exit(pid_t pid, shared_ptr<unit> u, int status) {
		assert(u->state == IN_LOGROT);

		log::debug(u->term_name() + ": kill(-" + to_string(u->logrot_pid) + ", " + signal_string(SIGTERM) + ")");
		kill(-u->logrot_pid, SIGTERM);
		u->logrot_pid = 0;

		if (status_ok(u, "logrotate", status))
			u->step_have_start();
		else
			u->set_state(DOWN);

		depgraph::queue_step();
	}

	void unit::on_start_exit(pid_t pid, shared_ptr<unit> u, int status) {
		assert(u->state == IN_START);

		log::debug(u->term_name() + ": kill(-" + to_string(u->start_pid) + ", " + signal_string(SIGTERM) + ")");
		kill(-u->start_pid, SIGTERM);
		u->start_pid = 0;

		if (status_ok(u, "start", status))
			u->step_have_run();
		else
			u->set_state(DOWN);

		depgraph::queue_step();
	}

	void unit::on_rdy_exit(pid_t pid, shared_ptr<unit> u, int status) {
		log::debug(u->term_name() + ": kill(-" + to_string(u->rdy_pid) + ", " + signal_string(SIGTERM) + ")");
		kill(-u->rdy_pid, SIGTERM);
		u->rdy_pid = 0;

		switch (u->state) {
			case IN_RDY:
				if (status_ok(u, "ready", status))
					u->set_state(UP);
				else
					u->step_active_run();
			break;

			case IN_RDY_ERR:
				status_ok(u, "ready", status);
				u->step_have_stop();
			break;

			default:
				assert(false);
		}

		depgraph::queue_step();
	}

	void unit::on_run_exit(pid_t pid, shared_ptr<unit> u, int status) {
		log::debug(u->term_name() + ": kill(-" + to_string(u->run_pid) + ", " + signal_string(SIGTERM) + ")");
		kill(-u->run_pid, SIGTERM);
		u->run_pid = 0;
		remove(statedir / "pid" / u->name());

		switch (u->state) {
			case IN_RDY:
				status_ok(u, "run", status);
				u->step_active_rdy();
			break;

			case UP:
			case IN_RUN:
				status_ok(u, "run", status);
				u->step_have_stop();
			break;

			default:
				assert(false);
		}

		depgraph::queue_step();
	}

	void unit::on_stop_exit(pid_t pid, shared_ptr<unit> u, int status) {
		assert(u->state == IN_STOP);

		log::debug(u->term_name() + ": kill(-" + to_string(u->stop_pid) + ", " + signal_string(SIGTERM) + ")");
		kill(-u->stop_pid, SIGTERM);
		u->stop_pid = 0;

		status_ok(u, "stop", status);
		u->step_have_restart();

		depgraph::queue_step();
	}

	void unit::on_event_exit(pid_t pid, shared_ptr<unit> u, int status) {
		log::debug(u->term_name() + ": kill(-" + to_string(pid) + ", " + signal_string(SIGTERM) + ")");
		kill(-pid, SIGTERM);
	}

#pragma GCC diagnostic pop
