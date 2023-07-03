all: wsunitd/wsunitd unittool/unittool

.PHONY: wsunitd/wsunitd
wsunitd/wsunitd:
	cd wsunitd && $(MAKE) wsunitd

.PHONY: unittool/unittool
unittool/unittool:
	cd unittool && $(MAKE) unittool

.PHONY: clean
clean:
	cd wsunitd  && $(MAKE) clean
	cd unittool && $(MAKE) clean
	-rm README.html

state_machine.png: state_machine.dot
	dot -Tpng $< -o $@

README.html: README.md state_machine.png
	pandoc $< -o $@



.PHONY: install
install: unittool/unittool wsunitd/wsunitd
	install -D -m 755 -t "${DESTDIR}/sbin" wsunitd/wsunitd
	install -D -m 755 -t "${DESTDIR}/sbin" unittool/unittool
	install -D -m 755 -t "${DESTDIR}/sbin" scripts/*



testfiles=$(wildcard tests/*)

.PHONY: tests
tests: $(testfiles)

.PHONY: $(testfiles)
$(testfiles): %: wsunitd/wsunitd
	./runtest $@
