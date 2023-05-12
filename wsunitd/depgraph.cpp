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
		if (np->u->needed() && !np->u->blocked()) start(np->u, false);
		else                                      stop (np->u, false);

	queue_step();
}

void depgraph::start(shared_ptr<unit> u, bool now) {
	filter(to_start, [&u](weak_ptr<unit>& w) { return with_weak_ptr(w, false, [&u](shared_ptr<unit>& u_) { return u_->name() != u->name(); }); });
	filter(to_stop , [&u](weak_ptr<unit>& w) { return with_weak_ptr(w, false, [&u](shared_ptr<unit>& u_) { return u_->name() != u->name(); }); });

	log::debug("add unit " + u->term_name() + " to start queue");
	to_start.push_back(u);
	if (now) queue_step();
}

void depgraph::stop(shared_ptr<unit> u, bool now) {
	filter(to_start, [&u](weak_ptr<unit>& w) { return with_weak_ptr(w, false, [&u](shared_ptr<unit>& u_) { return u_->name() != u->name(); }); });
	filter(to_stop , [&u](weak_ptr<unit>& w) { return with_weak_ptr(w, false, [&u](shared_ptr<unit>& u_) { return u_->name() != u->name(); }); });

	log::debug("add unit " + u->term_name() + " to stop queue");
	to_stop.push_back(u);
	if (now) queue_step();
}

void depgraph::handle(string event) {
	for (auto& [n, np] : nodes)
		if (np->u->get_state() == unit::UP)
			np->u->handle(event);
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

			log::debug("unlink old unit " + it->second->u->term_name() + " from depgraph");
			it->second->deps.clear();
			it->second->revdeps.clear();

			remove(statedir / "masked" / it->second->u->name());
			remove(statedir / "wanted" / it->second->u->name());

			++it;
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
			nodes.emplace(n, make_shared<depgraph::node>(unit::create(n)));
		}
	}
}

void depgraph::del_old_deps(void) {
	for (auto& [n, np] : nodes) {
		filter(np->deps, [&n = n, &np = np](weak_ptr<node>& dp) {
			return with_weak_ptr(dp, false, [n, np](shared_ptr<node>& dep) {
				if (
					!is_directory(dep->u->dir()) ||
					!(exists(np->u->dir() / "deps" / dep->u->name()) || exists(dep->u->dir() / "revdeps" / np->u->name()))
				) {
					log::debug("remove old dep " + n + " -> " + dep->u->name() + " from depgraph");
					return false;
				}
				return true;
			});
		});

		filter(np->revdeps, [&n = n, &np = np](weak_ptr<node>& rp) {
			return with_weak_ptr(rp, false, [n, np](shared_ptr<node>& revdep) {
				if (
					!is_directory(revdep->u->dir()) ||
					!(exists(revdep->u->dir() / "deps" / np->u->name()) || exists(np->u->dir() / "revdeps" / revdep->u->name()))
				) {
					log::debug("remove old revdep " + revdep->u->name() + " <- " + n + " from depgraph");
					return false;
				}
				return true;
			});
		});
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
		log::warn("could not add dependency " + fst + " <- " + snd + ": unit " + fst + " not found, ignoring...");
		return;
	}

	if (nodes.count(snd) == 0) {
		log::warn("could not add dependency " + fst + " <- " + snd + ": unit " + snd + " not found, ignoring...");
		return;
	}

	shared_ptr<node> a = nodes.at(fst);
	shared_ptr<node> b = nodes.at(snd);

	if (!contains(a->revdeps, b->u->name())) { log::debug("add revdep " + fst + " <- " + snd + " to depgraph"); a->revdeps.emplace_back(b); }
	if (!contains(b->   deps, a->u->name())) { log::debug("add dep "    + snd + " -> " + fst + " to depgraph"); b->   deps.emplace_back(a); }
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

	filter(nodes.at(fst)->revdeps, [&snd](weak_ptr<node>& rp) { return with_weak_ptr(rp, false, [snd](shared_ptr<node>& revdep) { return revdep->u->name() != snd; }); });
	filter(nodes.at(snd)->deps   , [&fst](weak_ptr<node>& dp) { return with_weak_ptr(dp, false, [fst](shared_ptr<node>&    dep) { return    dep->u->name() != fst; }); });
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

bool depgraph::is_settled(string* reason) {
	for (auto& [n, np] : nodes)
		if (np->u->running() && (!np->u->needed() || np->u->blocked())) {
			if (reason) *reason = "settle: waiting for " + np->u->term_name() + " to go down";
			return false;
		}
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

	filter(to_start, [&changed](weak_ptr<unit> w) {
		auto u = w.lock();
		string reason = "?";

		if (u) {
			if (!u->request_start(&reason)) {
				log::debug("keep unit " + (u ? u->name() : string("?")) + " in start queue: " + reason);
				return true;
			}
		}
		else
			reason = "stale unit";

		log::debug("drop unit " + (u ? u->name() : string("?")) + " from start queue: " + reason);
		changed = true;
		return false;
	});
}

void depgraph::stop_step(bool& changed) {
	log::debug("stop queue (length " + to_string(to_stop.size()) + "):");
	for (auto& p : to_stop)
		log::debug(" - " + with_weak_ptr(p, string("<stale>"), [](shared_ptr<unit> p){ return p->name(); }));

	filter(to_stop, [&changed](weak_ptr<unit> w) {
		auto u = w.lock();
		string reason = "?";

		if (u) {
			if (!u->request_stop(&reason)) {
				log::debug("keep unit " + (u ? u->name() : string("?")) + " in stop queue: " + reason);
				return true;
			}
		}
		else
			reason = "stale unit";

		log::debug("drop unit " + (u ? u->name() : string("?")) + " from stop queue: " + reason);
		changed = true;
		return false;
	});
}

void depgraph::write_state(void) {
	log::debug("update state files");

	for (auto& [n, np] : nodes) {
		auto rf = statedir / "running" / np->u->name();

		if ( np->u->running() && !is_regular_file(rf)) std::ofstream(rf).flush();
		if (!np->u->running() &&  is_regular_file(rf)) remove(rf);

		rf = statedir / "ready" / np->u->name();

		if ( np->u->ready() && !is_regular_file(rf)) std::ofstream(rf).flush();
		if (!np->u->ready() &&  is_regular_file(rf)) remove(rf);

		// TODO: pid file?
	}

	for (directory_entry& de : directory_iterator(statedir / "state"))
		if (nodes.count(de.path().filename().string()) == 0)
			remove_all(de);
}
