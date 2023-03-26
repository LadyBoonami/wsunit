all: wsunitd/wsunitd cronexec/cronexec

.PHONY: wsunitd/wsunitd
wsunitd/wsunitd:
	cd wsunitd && $(MAKE) wsunitd

.PHONY: cronexec/cronexec
cronexec/cronexec:
	cd cronexec && $(MAKE) cronexec

.PHONY: clean
clean:
	cd wsunitd  && $(MAKE) clean
	cd cronexec && $(MAKE) clean
	-rm README.html

state_machine.png: state_machine.dot
	dot -Tpng $< -o $@

README.html: README.md state_machine.png
	pandoc $< -o $@



testfiles=$(wildcard tests/*)

.PHONY: tests
tests: $(testfiles)

.PHONY: $(testfiles)
$(testfiles): %: wsunitd/wsunitd cronexec/cronexec
	./runtest $@
