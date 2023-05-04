#include "wsunitd.hpp"

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

class signal_handler : public epoll_handler {
	public:
		signal_handler(int epfd) {
			sigset_t sigs;
			assert(sigemptyset(&sigs) == 0);
			assert(sigaddset(&sigs, SIGUSR1) == 0);
			assert(sigaddset(&sigs, SIGUSR2) == 0);
			assert(sigaddset(&sigs, SIGCHLD) == 0);
			assert(sigaddset(&sigs, SIGTERM) == 0);
			assert(sigaddset(&sigs, SIGINT ) == 0);
			assert(sigprocmask(SIG_BLOCK, &sigs, 0) == 0);

			fd = signalfd(-1, &sigs, SFD_CLOEXEC | SFD_NONBLOCK);
			if (fd == -1)
				throw runtime_error(string("could not create signalfd: ") + strerror(errno));

			struct epoll_event ev;
			ev.events  = EPOLLIN;
			ev.data.fd = fd;
			if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
				throw runtime_error(string("could not register signalfd with epoll: ") + strerror(errno));
		}

		void handle(void) override {
			struct signalfd_siginfo info;
			ssize_t n;
			while ((n = read(fd, &info, sizeof(struct signalfd_siginfo))) != -1)
				switch (info.ssi_signo) {
					case SIGUSR1:
						log::note("received \x1b[36mSIGUSR1\x1b[0m, recalculate set of needed units");
						depgraph::start_stop_units();
					break;

					case SIGUSR2:
						log::note("received \x1b[36mSIGUSR2\x1b[0m, refresh dependency graph and recalculate set of needed units");
						depgraph::refresh();
						depgraph::start_stop_units();
					break;

					case SIGCHLD:
						log::debug("received \x1b[36mSIGCHLD\x1b[0m, handle zombies");
						waitall();
					break;

					case SIGTERM:
						log::note("received \x1b[36mSIGTERM\x1b[0m, shut down");
						in_shutdown = true;
						depgraph::start_stop_units();
					break;

					case SIGINT:
						log::note("received \x1b[36mSIGINT\x1b[0m, shut down");
						in_shutdown = true;
						depgraph::start_stop_units();
					break;

					default:
						log::debug("ignore signal " + signal_string(info.ssi_signo));
					break;
				}

			if (errno != EAGAIN && errno != EWOULDBLOCK)
				log::warn(string("reading signal fd failed: ") + strerror(errno));
		}
};

class event_fifo_handler : public epoll_handler {
	public:
		event_fifo_handler(int epfd) {
			// TODO: this could be written better
			if (!is_other(statedir / "events") || access((statedir / "events").c_str(), R_OK) == -1)
				if (mkfifo((statedir / "events").c_str(), 0600) == -1)
					throw runtime_error(string("failed to create events fifo: ") + strerror(errno));

			fd = open((statedir / "events").c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
			if (fd == -1)
				throw runtime_error(string("failed to open events fifo: ") + strerror(errno));

			fd_ = open((statedir / "events").c_str(), O_WRONLY | O_CLOEXEC | O_NONBLOCK);
			if (fd_ == -1) {
				close(fd);
				throw runtime_error(string("failed to open events fifo: ") + strerror(errno));
			}

			struct epoll_event ev;
			ev.events  = EPOLLIN;
			ev.data.fd = fd;
			if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
				close(fd);
				throw runtime_error(string("could not register event fifo with epoll: ") + strerror(errno));
			}
		}

		void handle(void) override {
			static char   buf[PIPE_BUF];
			static size_t pos = 0;

			log::debug("activity on event fd registered");

			for (;;) {
				ssize_t n = read(fd, buf + pos, 1);
				log::debug("read event fd: " + string(buf + pos, 1) + ", returned " + to_string(n));

				if (n == -1) {
					log::debug(string("errno: ") + strerror(errno));
					if (errno != EAGAIN || errno != EWOULDBLOCK)
						log::err(string("failed to read event fifo: ") + strerror(errno));

					return;
				}

				if (buf[pos] == '\n') {
					string ev(buf, pos);
					log::note("received event: " + ev);
					depgraph::handle(ev);
					log::debug("event handling complete");
					pos = 0;
					return;
				}

				if (buf[pos] == '/') {
					log::err("event contains illegal characters, discard ...");
					pos = 0;
					do {
						if (read(fd, buf, 1) <= 0) break;
					} while (*buf != '\n');
				}

				++pos;
				log::debug(string("onto next character, now at ") + to_string(pos));

				if (pos >= sizeof(buf)) {
					log::warn("event too long, discard ...");
					pos = 0;
					do {
						if (read(fd, buf, 1) <= 0) break;
					} while (*buf != '\n');
				}
			}
		}

		~event_fifo_handler(void) override {
			close(fd_);
			close(fd );
		}

	private:
		int fd_;
};



void waitall(void) {
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) term_handle(pid, status);
}

void main_loop(void) {
	map<int,shared_ptr<epoll_handler>> evmap;

	int epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd == -1) {
		log::fatal(string("could not create epoll fd: ") + strerror(errno));
		exit(1);
	}

	try {
		shared_ptr<epoll_handler> h = make_shared<signal_handler>(epfd);
		evmap.emplace(h->getfd(), h);
	}
	catch (exception& ex) {
		log::warn(ex.what());
		log::fatal("cannot continue without signal handling");
		exit(1);
	}

	try {
		shared_ptr<epoll_handler> h = make_shared<event_fifo_handler>(epfd);
		evmap.emplace(h->getfd(), h);
	}
	catch (exception& ex) {
		log::warn(ex.what());
		log::warn("continue with events support disabled");
	}

	for (;;) {
		log::debug("check for pending zombies");
		waitall();

		depgraph::report();

		log::debug("wait for next event");

		struct epoll_event ev;
		int fds = epoll_wait(epfd, &ev, 1, -1);

		if (fds == -1) log::warn(string("epoll_wait failed: ") + strerror(errno));
		else
			try {
				evmap.at(ev.data.fd)->handle();
			}
			catch (out_of_range& ex) {
				log::warn(string("unknown epoll fd event: ") + to_string(ev.data.fd));
			}
			catch (exception& ex) {
				log::err(string("error in handler: ") + ex.what());
			}
	}
}
