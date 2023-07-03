#include "unittool.hpp"

#include <iostream>

deque<shared_ptr<tool>> tool::tools;

void tool::handle(int argc, char** argv) {
	for (auto t : tools) {
		if (argc >= 2 && argv[1] == t->name()) {
			t->main(argc, argv);
			return;
		}
	}

	cerr << "usage: unittool <command> <command arguments ...>" << endl;
	for (auto t : tools)
		cerr
			<< endl
			<< t->name() << " : " << t->descr() << endl
			<< "    " << t->usage() << endl;

	exit(1);
}

void add_cronexec(void);
void add_runas(void);

int main(int argc, char** argv) {
	add_cronexec();
	add_runas();
	tool::handle(argc, argv);
	return 0;
}
