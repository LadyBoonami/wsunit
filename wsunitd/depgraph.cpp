#include "wsunitd.hpp"

#include <fstream>



void depgraph::refresh(void) {
	del_old_units();
	add_new_units();
	del_old_deps ();
	add_new_deps ();
}

void depgraph::start_stop_units(void) {
	for (auto& [n, u] : units)
		if (u->needed() && !u->masked()) start(u);
		else                             stop (u);
}

void depgraph::start(shared_ptr<unit> u) {
	log::debug("add unit " + u->name() + " to start queue");
	to_start.push_back(u);
	queue_step();
}

void depgraph::stop (shared_ptr<unit> u) {
	log::debug("add unit " + u->name() + " to stop queue");
	to_stop.push_back(u);
	queue_step();
}


map<string, shared_ptr<unit>> depgraph::units;

void depgraph::del_old_units(void) {
	auto it = units.begin();

	while (it != units.end())
		if (is_directory(unit::confdir / it->first))
			++it;

		else if (it->second->running()) {
			// remove all links to the graph, unit will be safely stopped as it is no longer needed(), and then remain
			// idle in the graph until the next refresh

			log::debug("unlink old unit " + it->second->term_name() + " from depgraph");
			it->second->deps.clear();
			it->second->revdeps.clear();
		}
		else {
			log::debug("remove old unit " + it->second->term_name() + " from depgraph");
			it = units.erase(it);
		}
}

void depgraph::add_new_units(void) {
	for (directory_entry& d : directory_iterator(unit::confdir)) {
		string n = d.path().filename().string();
		if (units.count(n) == 0) {
			log::debug("add new unit " + n + " to depgraph");
			units.emplace(n, shared_ptr<unit>(new unit(n)));
		}
	}
}

void depgraph::del_old_deps(void) {
	for (auto& [n, u] : units) {
		{
			auto it = u->deps.begin();
			while (it != u->deps.end()) {
				auto du = it->lock();
				if (du && is_directory(du->dir()) && (exists(u->dir() / "deps" / du->name()) || exists(du->dir() / "revdeps" / u->name()))) ++it;
				else {
					log::debug("remove old dep " + n + " -> " + du->name() + " from depgraph");
					it = u->deps.erase(it);
				}
			}
		}

		{
			auto it = u->revdeps.begin();
			while (it != u->revdeps.end()) {
				auto ru = it->lock();
				if (ru && is_directory(ru->dir()) && (exists(ru->dir() / "deps" / u->name()) || exists(u->dir() / "revdeps" / ru->name()))) ++it;
				else {
					log::debug("remove old revdep " + ru->name() + " <- " + n + " from depgraph");
					it = u->deps.erase(it);
				}
			}
		}
	}
}

void depgraph::add_new_deps(void) {
	for (auto& [n, u] : units) {
		path up = u->dir();
		path dp = up / "deps";
		path rp = up / "revdeps";

		if (is_directory(dp))
			for (directory_entry& d : directory_iterator(dp))
				adddep(d.path().filename().string(), n);

		if (is_directory(rp))
			for (directory_entry& r : directory_iterator(rp))
				adddep(n, r.path().filename().string());
	}
}

void depgraph::adddep(string fst, string snd) {
	if (units.count(fst) == 0) {
		log::warn("could not add dependency between " + fst + " and " + snd + ": unit " + fst + " not found, ignoring...");
		return;
	}

	if (units.count(snd) == 0) {
		log::warn("could not add dependency between " + fst + " and " + snd + ": unit " + snd + " not found, ignoring...");
		return;
	}

	shared_ptr<unit> a = units.at(fst);
	shared_ptr<unit> b = units.at(snd);

	if (!contains(a->revdeps, b->name())) { log::debug("add revdep " + snd + " <- " + fst + " to depgraph"); a->revdeps.emplace_back(b); }
	if (!contains(b->   deps, a->name())) { log::debug("add dep "    + fst + " -> " + snd + " to depgraph"); b->   deps.emplace_back(a); }
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

	if (unit::in_shutdown) {
		for (auto& [n, u] : units)
			if (!u->needed() && u->running()) {
				log::debug("shutdown: waiting for " + u->term_name());
				return;
			}

		if (units.count("@shutdown") > 0 && !units.at("@shutdown")->ready())
			return;

		exit(0);
	}
}

void depgraph::report(void) {
	log::debug("current state:");
	for (auto& [n, u] : units)
		log::debug(" - " + u->term_name() + " " + unit::term_state_descr(u->state) + " "
			+ (u->needed   () ? "n" : ".")
			+ (u->masked   () ? "m" : ".")
			+ (u->can_start() ? "u" : ".")
			+ (u->can_stop () ? "d" : ".")
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

		if (!u) {
			reason = "stale unit";
			goto drop;
		}

		switch (u->state) {
			case unit::IN_START:
			case unit::IN_RUN:
				reason = "already starting";
				goto drop;

			case unit::UP:
				reason = "already started";
				goto drop;

			case unit::IN_STOP:
				reason = "currently stopping";
				goto keep;

			case unit::DOWN:
			break;
		}

		if (!u->can_start()) {
			reason = "waiting for dependencies to be ready";
			goto keep;
		}

		unit::start_step(u, changed);
		reason = "now starting";
		goto drop;

	drop:
		log::debug("drop unit " + (u ? u->name() : string("?")) + " from start queue: " + reason);
		it = to_start.erase(it);
		continue;

	keep:
		log::debug("keep unit " + (u ? u->name() : string("?")) + " in start queue: " + reason);
		++it;
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

		if (!u) {
			reason = "stale unit";
			goto drop;
		}

		switch (u->state) {
			case unit::IN_START:
			case unit::IN_RUN:
				reason = "currently starting";
				goto keep;

			case unit::UP:
			break;

			case unit::IN_STOP:
				reason = "already stopping";
				goto drop;

			case unit::DOWN:
				reason = "already stopped";
				goto drop;
		}

		if (!u->can_stop()) {
			reason = "waiting for reverse dependencies to go down";
			goto keep;
		}

		unit::stop_step(u, changed);
		reason = "now stopping";
		goto drop;

	drop:
		log::debug("drop unit " + (u ? u->name() : string("?")) + " from stop queue: " + reason);
		it = to_stop.erase(it);
		continue;

	keep:
		log::debug("keep unit " + (u ? u->name() : string("?")) + " in stop queue: " + reason);
		++it;
		continue;
	}
}

void depgraph::write_state(void) {
	log::debug("update state files");

	ofstream o(unit::statedir / "state.dot");
	o << "digraph {" << endl;
	for (auto& [n, u] : units) {
		o << "\t\"" << n << "\""
			<< " ["
				<< "style=\"" << (u->masked() ? "dashed" : u->wanted() ? "bold" : "solid") << "\""
				<< ", color=\"" << (u->ready() ? "green" : u->running() ? "blue" : "black") << "\""
			<<"];" << endl;

		for (auto& d : u->deps) {
			auto d_ = d.lock();
			if (d_ && exists(u->dir() / "deps" / d_->name()))
				o << "\t\"" << n << "\" -> \"" << d_->name() << "\" [dir=\"back\", arrowtail=\"inv\"];" << endl;
		}

		for (auto& r : u->revdeps) {
			auto r_ = r.lock();
			if (r_ && exists(u->dir() / "revdeps" / r_->name()))
				o << "\t\"" << r_->name() << "\" -> \"" << n << "\";" << endl;
		}

		auto rf = unit::statedir / "running" / u->name();

		if (u->state != unit::DOWN && !is_regular_file(rf)) ofstream(rf).flush();
		if (u->state == unit::DOWN &&  is_regular_file(rf)) remove(rf);

		rf = unit::statedir / "ready" / u->name();

		if (u->state == unit::UP && !is_regular_file(rf)) ofstream(rf).flush();
		if (u->state != unit::UP &&  is_regular_file(rf)) remove(rf);

		// TODO: pid file?
	}
	o << "}" << endl;
	o.close();
}
