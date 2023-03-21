all: wsunitd/wsunitd

.PHONY: wsunitd/wsunitd
wsunitd/wsunitd:
	cd wsunitd && $(MAKE) wsunitd

.PHONY: clean
clean:
	cd wsunitd  && $(MAKE) clean
	cd cronexec && $(MAKE) clean
