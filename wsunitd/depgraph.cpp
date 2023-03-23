#include "wsunitd.hpp"

#include <fstream>
#include <sstream>



void depgraph::refresh(void) {
	mkdirs();
	del_old_units();
	add_new_units();
	del_old_deps ();
	add_new_deps ();
	verify_deps  ();
}

void depgraph::start_stop_units(void) {
	for (auto& [n, np] : nodes)
		if (np->u->needed() && !np->u->blocked()) start(np->u);
		else                                      stop (np->u);
}

void depgraph::start(shared_ptr<unit> u) {
	auto it = to_stop.begin();
	while (it != to_stop.end()) {
		auto u_ = it->lock();
		if (u_ && u_->name() == u->name()) {
			log::debug("remove unit " + u_->term_name() + " from stop queue");
			it = to_stop.erase(it);
		}
		else
			++it;
	}

	if (!u->ready()) {
		log::debug("add unit " + u->term_name() + " to start queue");
		to_start.push_back(u);
		queue_step();
	}
}

void depgraph::stop (shared_ptr<unit> u) {
	auto it = to_start.begin();
	while (it != to_start.end()) {
		auto u_ = it->lock();
		if (u_ && u_->name() == u->name()) {
			log::debug("remove unit " + u_->term_name() + " from start queue");
			it = to_start.erase(it);
		}
		else
			++it;
	}

	if (u->running())  {
		log::debug("add unit " + u->term_name() + " to stop queue");
		to_stop.push_back(u);
		queue_step();
	}
}


bool depgraph::contains(const vector<weak_ptr<node>>& v, const string& name) {
	for (auto& d : v)
		if (with_weak_ptr(d, false, [&name](shared_ptr<node> np){ return name == np->u->name(); })) return true;

	return false;
}

map<string, shared_ptr<depgraph::node>> depgraph::nodes;

vector<shared_ptr<unit>> depgraph::get_deps(string name) {
	try {
		auto& n = nodes.at(name);
		vector<shared_ptr<unit>> ret;
		for (auto& w : n->deps) { auto p = w.lock(); if (p) ret.push_back(p->u); }
		return ret;
	}
	catch (exception& ex) {
		return vector<shared_ptr<unit>>();
	}
}

vector<shared_ptr<unit>> depgraph::get_revdeps(string name) {
	try {
		auto& n = nodes.at(name);
		vector<shared_ptr<unit>> ret;
		for (auto& w : n->revdeps) { auto p = w.lock(); if (p) ret.push_back(p->u); }
		return ret;
	}
	catch (exception& ex) {
		return vector<shared_ptr<unit>>();
	}
}

void depgraph::del_old_units(void) {
	auto it = nodes.begin();
	while (it != nodes.end())
		if (is_directory(confdir / it->first))
			++it;

		else if (it->second->u->running()) {
			// remove all links to the graph, unit will be safely stopped as it is no longer needed(), and then remain
			// idle in the graph until the next refresh
			// TODO: what if wanted()?

			log::debug("unlink old unit " + it->second->u->term_name() + " from depgraph");
			it->second->deps.clear();
			it->second->revdeps.clear();
		}
		else {
			log::debug("remove old unit " + it->second->u->term_name() + " from depgraph");
			it = nodes.erase(it);
		}
}

void depgraph::add_new_units(void) {
	for (directory_entry& d : directory_iterator(confdir)) {
		string n = d.path().filename().string();
		if (nodes.count(n) == 0) {
			log::debug("add new unit " + n + " to depgraph");
			nodes.emplace(n, make_shared<depgraph::node>(make_shared<unit>(n)));
		}
	}
}

void depgraph::del_old_deps(void) {
	for (auto& [n, np] : nodes) {
		{
			auto it = np->deps.begin();
			while (it != np->deps.end()) {
				auto dep = it->lock();
				if (
					dep &&
					is_directory(dep->u->dir()) &&
					(exists(np->u->dir() / "deps" / dep->u->name()) || exists(dep->u->dir() / "revdeps" / np->u->name()))
				) ++it;

				else {
					log::debug("remove old dep " + n + " -> " + dep->u->name() + " from depgraph");
					it = np->deps.erase(it);
				}
			}
		}

		{
			auto it = np->revdeps.begin();
			while (it != np->revdeps.end()) {
				auto revdep = it->lock();
				if (
					revdep &&
					is_directory(revdep->u->dir()) &&
					(exists(revdep->u->dir() / "deps" / np->u->name()) || exists(np->u->dir() / "revdeps" / revdep->u->name()))
				) ++it;

				else {
					log::debug("remove old revdep " + revdep->u->name() + " <- " + n + " from depgraph");
					it = np->revdeps.erase(it);
				}
			}
		}
	}
}

void depgraph::add_new_deps(void) {
	for (auto& [n, np] : nodes) {
		path npath = np->u->dir();
		path dpath = npath / "deps";
		path rpath = npath / "revdeps";

		if (is_directory(dpath))
			for (directory_entry& d : directory_iterator(dpath))
				adddep(d.path().filename().string(), n);

		if (is_directory(rpath))
			for (directory_entry& r : directory_iterator(rpath))
				adddep(n, r.path().filename().string());
	}
}

void depgraph::verify_deps(void) {
	for (auto& [n, np] : nodes) {
		map<string, bool> visited;
		deque<string> trail;
		visit(n, visited, trail);
	}
}

void depgraph::visit(const string& name, map<string, bool>& visited, deque<string>& trail) {
	if (visited.count(name)) {
		if (visited[name])
			return;
		else {
			vector<string> cycle;
			auto it = trail.begin();
			while (*it != name) {
				++it;
				assert(it != trail.end());
			}

			stringstream ss;
			ss << "...";

			while (it != trail.end())
				ss << " -> " << name;

			log::warn("found dependency cycle: " + ss.str());
			log::warn("continue while ignoring dependency " + trail.back() + " -> " + name);
			rmdep(trail.back(), name);
		}
	}
	visited[name] = false;
	trail.push_back(name);

	auto deps = get_deps(name);
	for (auto& p : deps)
		visit(p->name(), visited, trail);

	trail.pop_back();
	visited[name] = true;
}

void depgraph::adddep(string fst, string snd) {
	if (nodes.count(fst) == 0) {
		log::warn("could not add dependency between " + fst + " and " + snd + ": unit " + fst + " not found, ignoring...");
		return;
	}

	if (nodes.count(snd) == 0) {
		log::warn("could not add dependency between " + fst + " and " + snd + ": unit " + snd + " not found, ignoring...");
		return;
	}

	shared_ptr<node> a = nodes.at(fst);
	shared_ptr<node> b = nodes.at(snd);

	if (!contains(a->revdeps, b->u->name())) { log::debug("add revdep " + snd + " <- " + fst + " to depgraph"); a->revdeps.emplace_back(b); }
	if (!contains(b->   deps, a->u->name())) { log::debug("add dep "    + fst + " -> " + snd + " to depgraph"); b->   deps.emplace_back(a); }
}

void depgraph::rmdep(string fst, string snd) {
	if (nodes.count(fst) == 0) {
		log::warn("could not remove dependency between " + fst + " and " + snd + ": unit " + fst + " not found, ignoring...");
		return;
	}

	if (nodes.count(snd) == 0) {
		log::warn("could not remove dependency between " + fst + " and " + snd + ": unit " + snd + " not found, ignoring...");
		return;
	}

	{
		auto& np = nodes.at(fst);
		auto it = np->revdeps.begin();
		while (it != np->revdeps.end()) {
			auto revdep = it->lock();
			if (revdep && revdep->u->name() != snd)
				++it;

			else {
				it = np->revdeps.erase(it);
				break;
			}
		}
	}

	{
		auto& np = nodes.at(snd);
		auto it = np->deps.begin();
		while (it != np->deps.end()) {
			auto dep = it->lock();
			if (dep && dep->u->name() != fst)
				++it;

			else {
				it = np->deps.erase(it);
				break;
			}
		}
	}
}

deque<weak_ptr<unit>> depgraph::to_start;
deque<weak_ptr<unit>> depgraph::to_stop ;

void depgraph::queue_step(void) {
	bool changed = false;
	do {
		changed = false;
		stop_step (changed);
		start_step(changed);
		write_state();
		if (changed) log::debug("global state changed, re-processing queues");
		else         log::debug("no global state change, done processing queues");
	} while (changed);

	if (in_shutdown) {
		for (auto& [n, np] : nodes)
			if (!np->u->needed() && np->u->running()) {
				log::debug("shutdown: waiting for " + np->u->term_name());
				return;
			}

		if (nodes.count("@shutdown") > 0 && !nodes.at("@shutdown")->u->ready())
			return;

		exit(0);
	}
}

bool depgraph::is_settled(void) {
	for (auto& [n, np] : nodes) if (np->u->running() && ! np->u->needed()) return false;
	return true;
}

void depgraph::report(void) {
	log::debug("current state:");
	for (auto& [n, np] : nodes)
		log::debug(" - " + np->u->term_name() + " " + unit::term_state_descr(np->u->get_state()) + " "
			+ (np->u->needed   () ? "\x1b[32mN\x1b[0m" : "n")
			+ (np->u->wanted   () ? "\x1b[32mW\x1b[0m" : "w")
			+ (np->u->masked   () ? "\x1b[31mM\x1b[0m" : "m")
			+ (np->u->blocked  () ? "\x1b[31mB\x1b[0m" : "b")
			+ (np->u->can_start() ? "\x1b[34mU\x1b[0m" : "u")
			+ (np->u->can_stop () ? "\x1b[34mD\x1b[0m" : "d")
		);

	log::debug("start queue:");
	for (auto& u : to_start)
		log::debug(" - " + with_weak_ptr(u, string("?"), [](shared_ptr<unit> u){ return u->term_name(); }));

	log::debug("stop queue:");
	for (auto& u : to_stop)
		log::debug(" - " + with_weak_ptr(u, string("?"), [](shared_ptr<unit> u){ return u->term_name(); }));
}

void depgraph::start_step(bool& changed) {
	log::debug("start queue (length " + to_string(to_start.size()) + "):");
	for (auto& p : to_start)
		log::debug(" - " + with_weak_ptr(p, string("<stale>"), [](shared_ptr<unit> p){ return p->name(); }));

	auto it = to_start.begin();
	while (it != to_start.end()) {
		auto u = it->lock();
		string reason = "?";

		if (u) {
			if (!unit::request_start(u, &reason)) {
				log::debug("keep unit " + (u ? u->name() : string("?")) + " in start queue: " + reason);
				++it;
				continue;
			}
		}
		else
			reason = "stale unit";

		log::debug("drop unit " + (u ? u->name() : string("?")) + " from start queue: " + reason);
		it = to_start.erase(it);
		changed = true;
		continue;
	}
}

void depgraph::stop_step(bool& changed) {
	log::debug("stop queue (length " + to_string(to_stop.size()) + "):");
	for (auto& p : to_stop)
		log::debug(" - " + with_weak_ptr(p, string("<stale>"), [](shared_ptr<unit> p){ return p->name(); }));

	auto it = to_stop.begin();
	while (it != to_stop.end()) {
		auto u = it->lock();
		string reason = "?";

		if (u) {
			if (!unit::request_stop(u, &reason)) {
				log::debug("keep unit " + (u ? u->name() : string("?")) + " in stop queue: " + reason);
				++it;
				continue;
			}
		}
		else
			reason = "stale unit";

		log::debug("drop unit " + (u ? u->name() : string("?")) + " from stop queue: " + reason);
		it = to_stop.erase(it);
		changed = true;
		continue;
	}
}

void depgraph::write_state(void) {
	log::debug("update state files");

	for (auto& [n, np] : nodes) {
		auto rf = statedir / "running" / np->u->name();

		if ( np->u->running() && !is_regular_file(rf)) ofstream(rf).flush();
		if (!np->u->running() &&  is_regular_file(rf)) remove(rf);

		rf = statedir / "ready" / np->u->name();

		if ( np->u->ready() && !is_regular_file(rf)) ofstream(rf).flush();
		if (!np->u->ready() &&  is_regular_file(rf)) remove(rf);

		// TODO: pid file?
	}
}
