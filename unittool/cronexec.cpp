#include <ctime>
#include <iostream>
#include <memory>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

#include "unittool.hpp"

using namespace std;

class match {
	public:
		virtual bool matches(int) = 0;
		virtual string str(void) = 0;
		virtual ~match(void) = default;
};

std::shared_ptr<match> parse(string in);

class cronexec : public tool {
	public:
		cronexec(void) = default;

		virtual string name (void) override { return "cronexec"; }
		virtual string descr(void) override { return "execute a command whenever a time pattern matches"; }
		virtual string usage(void) override { return "unittool cronexec <min> <hour> <day> <month> <weekday> <command> [command args ...]"; }
		virtual ~cronexec(void) = default;

		virtual void main(int argc, char** argv) override;
};

void cronexec::main(int argc, char** argv) {
	if (argc < 8) {
		cerr << "usage: " << usage() << endl;
		exit(1);
	}

	std::shared_ptr<match> min  = parse(argv[2]);
	std::shared_ptr<match> hr   = parse(argv[3]);
	std::shared_ptr<match> day  = parse(argv[4]);
	std::shared_ptr<match> mon  = parse(argv[5]);
	std::shared_ptr<match> wday = parse(argv[6]);

	std::string inp;
	if (!isatty(0))
		inp = string(istreambuf_iterator<char>(cin), {});

	for(;;) {
		time_t timestamp = time(0);
		struct tm t;
		localtime_r(&timestamp, &t);

		if (
			min->matches(t.tm_min ) &&
			hr ->matches(t.tm_hour) &&
			day->matches(t.tm_mday) &&
			mon->matches(t.tm_mon ) &&
			(wday->matches(t.tm_wday) || (t.tm_wday == 0 && wday->matches(7)))
		) {
			int pipefd[2];
			if (pipe(pipefd) == 0) {
				pid_t pid = fork();

				if (pid == 0) {
					dup2(pipefd[0], 0);
					close(pipefd[0]);
					close(pipefd[1]);

					execvp(argv[7], argv + 7);
					cerr << "could not start " << argv[7] << ": " << strerror(errno) << endl;
					exit(1);
				}

				else if (pid < 0)
					cerr << "could not start " << argv[7] << ": " << strerror(errno) << endl;

				else {
					close(pipefd[0]);

					ssize_t l = inp.length() + 1;
					const char* buf = inp.c_str();
					for (;;) {
						if (l <= 0) break;
						if (waitpid(pid, 0, WNOHANG) > 0) break;

						ssize_t n = write(pipefd[1], buf, l);
						if (n < 0) break;
						buf += n;
						l   -= n;
					}
					close(pipefd[1]);

					waitpid(pid, 0, 0);
				}
			}
			else
				cerr << "could not create pipe to child process: " << strerror(errno) << endl;
		}

		t.tm_min += 1;
		t.tm_sec  = 0;

		sleep(difftime(mktime(&t), time(0)));
	}
}

void add_cronexec(void) { tool::add(make_shared<cronexec>()); }

class matchAny : public match {
	public:
		static shared_ptr<match> create(void) { return make_shared<matchAny>(); }

		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wunused-parameter"
		bool matches(int _) override { return true; }
		#pragma GCC diagnostic pop

		string str(void) override { return "*"; }

		matchAny(void) = default;
		~matchAny(void) = default;
};

class matchNumber : public match {
	public:
		static shared_ptr<match> create(int n) { return make_shared<matchNumber>(n); }
		bool matches(int i) override { return i == n; }
		string str(void) override { return to_string(n); }

		matchNumber(int n) : n(n) {}
		~matchNumber(void) = default;

	private:
		int n;
};

class matchRange : public match {
	public:
		static shared_ptr<match> create(int min, int max) { return make_shared<matchRange>(min, max); }
		bool matches(int n) override { return min <= n && n <= max; }
		string str(void) override { return to_string(min) + "-" + to_string(max); }

		matchRange(int min, int max) : min(min), max(max) {}
		~matchRange(void) = default;

	private:
		int min;
		int max;
};

class matchEither : public match {
	public:
		static shared_ptr<match> create(shared_ptr<match> a, shared_ptr<match> b) { return make_shared<matchEither>(a, b); }
		bool matches(int n) override { return a->matches(n) || b->matches(n); }
		string str(void) override { return a->str() + "," + b->str(); }

		matchEither(shared_ptr<match> a, shared_ptr<match> b) : a(a), b(b) {}
		~matchEither(void) = default;

	private:
		shared_ptr<match> a;
		shared_ptr<match> b;
};

class matchStep : public match {
	public:
		static shared_ptr<match> create(shared_ptr<match> inner, int step) { return make_shared<matchStep>(inner, step); }
		bool matches(int n) override { return n % step == 0 && inner->matches(n); }
		string str(void) override { return inner->str() + "/" + to_string(step); }

		matchStep(shared_ptr<match> inner, int step) : inner(inner), step(step) {}
		~matchStep(void) = default;

	private:
		shared_ptr<match> inner;
		int step;
};

class matchRand : public match {
	public:
		static shared_ptr<match> create(int min, int max) { return make_shared<matchRand>(min, max); }
		bool matches(int n) override { return n == rolled; }
		string str(void) override { return to_string(min) + "~" + to_string(max); }

		matchRand(int min, int max) : min(min), max(max) { rolled = uniform_int_distribution<int>(min, max)(mt); }
		~matchRand(void) = default;

	private:
		int min;
		int max;
		int rolled;
		static mt19937 mt;
};

#include <boost/regex.hpp>

using namespace boost;

static std::shared_ptr<match> parsePrim(string in) {
	static regex reAny   (R"(^\*$)"                );
	static regex reNumber(R"(^[0-9]+$)"            );
	static regex reRange (R"(^([0-9]+)\-([0-9]+)$)");
	static regex reRand  (R"(^([0-9]+)~([0-9]+)$)" );
	smatch res;

	if (regex_match(in, res, reAny))
		return matchAny::create();

	if (regex_match(in, res, reNumber))
		return matchNumber::create(stoi(res[0]));

	if (regex_match(in, res, reRange))
		return matchRange::create(stoi(res[1]), stoi(res[2]));

	if (regex_match(in, res, reRand))
		return matchRand::create(stoi(res[1]), stoi(res[2]));

	throw runtime_error("could not parse: " + in);
}

static std::shared_ptr<match> parseStep(string in) {
	static regex reStep(R"(^(.*)/([0-9]+)$)");
	smatch res;

	if (regex_match(in, res, reStep))
		return matchStep::create(parsePrim(res[1]), stoi(res[2]));

	return parsePrim(in);
}

static std::shared_ptr<match> parseEither(string in) {
	static regex reEither(R"(^([^,]+),(.+)$)");
	smatch res;

	if (regex_match(in, res, reEither))
		return matchEither::create(parseStep(res[1]), parseEither(res[2]));

	return parseStep(in);
}

mt19937 matchRand::mt = mt19937(time(0));

std::shared_ptr<match> parse(string in) {
	return parseEither(in);
}
