all: wsunitd/wsunitd

.PHONY: wsunitd/wsunitd
wsunitd/wsunitd:
	cd wsunitd && make wsunitd

.PHONY: clean
clean:
	cd wsunitd && make clean
