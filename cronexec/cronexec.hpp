#pragma once

#include <ctime>
#include <memory>
#include <random>

using namespace std;



class match {
	public:
		virtual bool matches(int) = 0;
		virtual string str(void) = 0;
		virtual ~match(void) = default;
};

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

shared_ptr<match> parse(string in);

int main(int argc, char** argv);
