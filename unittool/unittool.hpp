#pragma once

#include <deque>
#include <memory>
#include <string>

using namespace std;

class tool {
	public:
		virtual string name(void) = 0;
		virtual string descr(void) = 0;
		virtual string usage(void) = 0;
		virtual ~tool(void) = default;

		virtual void main(int argc, char** argv) = 0;

		static void add(shared_ptr<tool> t) { tools.push_back(t); }
		static void handle(int argc, char** argv);

	private:
		static deque<shared_ptr<tool>> tools;
};
