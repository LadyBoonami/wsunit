#include "cronexec.hpp"

#include <iostream>

#include <boost/regex.hpp>

#include <sys/wait.h>

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

int main(int argc, char** argv) {
	if (argc < 7) return 1;

	std::shared_ptr<match> min  = parse(argv[1]);
	std::shared_ptr<match> hr   = parse(argv[2]);
	std::shared_ptr<match> day  = parse(argv[3]);
	std::shared_ptr<match> mon  = parse(argv[4]);
	std::shared_ptr<match> wday = parse(argv[5]);

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
			pid_t pid = fork();

			if (pid == 0) {
				execvp(argv[6], argv + 6);
				cerr << "could not start " << argv[6] << ": " << strerror(errno) << endl;
				exit(1);
			}

			else if (pid < 0)
				cerr << "could not start " << argv[6] << ": " << strerror(errno) << endl;

			else
				waitpid(pid, 0, 0);
		}

		t.tm_min += 1;
		t.tm_sec  = 0;

		sleep(difftime(mktime(&t), time(0)));
	}
}
