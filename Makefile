all: wsunitd/wsunitd

.PHONY: wsunitd/wsunitd
wsunitd/wsunitd:
	cd wsunitd && make wsunitd

.PHONY: clean
clean:
	cd wsunitd && make clean

.PHONY: dot
dot:
	dot -Tx11 state/state.dot
