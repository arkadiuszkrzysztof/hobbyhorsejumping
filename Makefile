# Hobby Horse Jumping timing system — everything from the repo root.
#
#   make            list the targets
#   make test       all automated tests (firmware logic on the host + backend)
#
# Firmware builds are niced and limited to $(JOBS) parallel compile jobs so the
# desktop stays usable; raise it with e.g. `make build JOBS=8`.

SHELL := /bin/bash
ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD := $(ROOT)/build

CXX   ?= c++
PIO   ?= $(firstword $(wildcard $(HOME)/.platformio/penv/bin/pio) pio)
JOBS  ?= 2
NICE  := nice -n 10
# Serial port for flash-* / monitor-* (empty = let PlatformIO pick): make flash-start PORT=/dev/cu.usbserial-0001
PORT  ?=
PORTFLAG_UPLOAD  := $(if $(PORT),--upload-port $(PORT),)
PORTFLAG_MONITOR := $(if $(PORT),--port $(PORT),)

FRONTEND    := $(ROOT)/hhj-frontend
BACKEND     := $(ROOT)/hhj-backend
PY          := $(BACKEND)/.venv/bin/python
# The shell may have another project's virtualenv active; never install into that one.
PIPENV      := env -u VIRTUAL_ENV PIPENV_IGNORE_VIRTUALENVS=1 PIPENV_VENV_IN_PROJECT=1 pipenv
BACKEND_ADDR ?= 0.0.0.0:3003

# ------------------------------------------------------------------ host tests
INC  := -I$(ROOT)/hhj-common/include -I$(ROOT)/hhj-common/test -I$(ROOT)/hhj-sensor -I$(ROOT)/hhj-timer/include
WARN := -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
SAN  := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
DEFS := -DHHJ_VECTORS='"$(ROOT)/hhj-common/protocol/vectors.tsv"'

HEADERS   := $(wildcard $(ROOT)/hhj-common/include/hhj/*.h $(ROOT)/hhj-common/test/*.h \
                        $(ROOT)/hhj-sensor/*.h $(ROOT)/hhj-timer/include/*.h)
TEST_SRCS := $(wildcard $(ROOT)/hhj-common/test/test_*.cpp $(ROOT)/hhj-sensor/test/test_*.cpp \
                        $(ROOT)/hhj-timer/test/test_*.cpp)
TEST_BINS := $(foreach s,$(TEST_SRCS),$(BUILD)/test/$(basename $(notdir $(s))))
COV_BINS  := $(foreach s,$(TEST_SRCS),$(BUILD)/cov/$(basename $(notdir $(s))))
SIM       := $(BUILD)/hhj-gate-sim

define TEST_RULES
$(BUILD)/test/$(basename $(notdir $(1))): $(1) $(HEADERS) $(ROOT)/Makefile
	@mkdir -p $$(@D)
	$(CXX) $(WARN) $(SAN) -O1 -g $(DEFS) $(INC) $$< -o $$@
$(BUILD)/cov/$(basename $(notdir $(1))): $(1) $(HEADERS) $(ROOT)/Makefile
	@mkdir -p $$(@D)
	$(CXX) $(WARN) -O0 -g -fprofile-instr-generate -fcoverage-mapping $(DEFS) $(INC) $$< -o $$@
endef
$(foreach s,$(TEST_SRCS),$(eval $(call TEST_RULES,$(s))))

.DEFAULT_GOAL := help
.PHONY: help test test-firmware test-backend test-frontend test-link coverage mutation sim frontend-build \
        build build-start build-finish build-timer build-timer-test \
        flash-start flash-finish flash-timer flash-timer-test monitor-sensor monitor-timer \
        backend backend-venv backend-migrate frontend clean

help:
	@echo "Tests"
	@echo "  make test              everything below"
	@echo "  make test-firmware     host tests of sensor / timer / shared code (ASan + UBSan, -Werror)"
	@echo "  make test-backend      Django tests, including the C++ gate code against the real backend"
	@echo "  make test-link         only that last part (decoders compared, gate + display <-> backend over UDP)"
	@echo "  make test-frontend     operator console tests (Vitest)"
	@echo "  make coverage          line / branch coverage of the firmware logic (must be 100% lines)"
	@echo "  make mutation          mutation test of the firmware logic (slow: ~10 min with MUT_JOBS=4; ONLY=name)"
	@echo "Firmware (PlatformIO)"
	@echo "  make build             compile start gate, finish gate and timer"
	@echo "  make flash-start | flash-finish | flash-timer | flash-timer-test   [PORT=/dev/cu...]"
	@echo "  make monitor-sensor | monitor-timer                                [PORT=/dev/cu...]"
	@echo "Apps"
	@echo "  make backend           migrate + run the server on $(BACKEND_ADDR) (UDP link on 3004)"
	@echo "  make frontend          the operator console on http://localhost:3000 (needs the backend)"
	@echo "  make frontend-build    type-check and bundle the console into hhj-frontend/dist"
	@echo "  make sim               build the desktop gate / display simulator ($(SIM))"
	@echo "  make clean             remove ./build"

test: test-firmware test-backend test-frontend
	@echo "ALL TESTS PASSED"

test-firmware: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do printf '%-22s ' "$$(basename $$t)"; $$t || fail=1; done; exit $$fail

# ------------------------------------------------------------------- coverage
LLVM_PROFDATA := $(shell xcrun -f llvm-profdata 2>/dev/null || command -v llvm-profdata)
LLVM_COV      := $(shell xcrun -f llvm-cov 2>/dev/null || command -v llvm-cov)

coverage: $(COV_BINS)
	@rm -f $(BUILD)/cov/*.profraw
	@for t in $(COV_BINS); do LLVM_PROFILE_FILE=$$t.profraw $$t > /dev/null || exit 1; done
	@$(LLVM_PROFDATA) merge -sparse $(BUILD)/cov/*.profraw -o $(BUILD)/cov/all.profdata
	@python3 $(ROOT)/hhj-common/tools/coverage.py "$(LLVM_COV)" $(BUILD)/cov/all.profdata $(ROOT) $(COV_BINS)

# ONLY=stopwatch,display limits the run to headers whose name contains one of these.
# MUT_JOBS=4 uses four parallel workers (default 2).
mutation:
	@$(NICE) python3 $(ROOT)/hhj-common/tools/mutate.py $(ROOT) $(BUILD)/mutation $(ONLY)

# ------------------------------------------------------------------ simulator
sim: $(SIM)
$(SIM): $(ROOT)/hhj-common/tools/gate_sim.cpp $(HEADERS) $(ROOT)/Makefile
	@mkdir -p $(@D)
	$(CXX) $(WARN) -O1 -g $(INC) $< -o $@

# -------------------------------------------------------------------- backend
$(PY): $(BACKEND)/Pipfile.lock
	cd $(BACKEND) && $(PIPENV) sync
	@touch $(PY)

backend-venv: $(PY)

backend-migrate: $(PY)
	cd $(BACKEND) && $(PY) manage.py migrate

backend: backend-migrate
	cd $(BACKEND) && $(PY) manage.py runserver $(BACKEND_ADDR)

# The backend suite includes the C++-against-Python tests (test_cross), hence the simulator.
test-backend: $(PY) $(SIM)
	cd $(BACKEND) && HHJ_GATE_SIM=$(SIM) $(PY) manage.py test

test-link: $(PY) $(SIM)
	cd $(BACKEND) && HHJ_GATE_SIM=$(SIM) $(PY) manage.py test timings.tests.test_cross

# ------------------------------------------------------------------- frontend
$(FRONTEND)/node_modules: $(FRONTEND)/package-lock.json
	cd $(FRONTEND) && npm install
	@touch $@

frontend: $(FRONTEND)/node_modules
	cd $(FRONTEND) && npm start

frontend-build: $(FRONTEND)/node_modules
	cd $(FRONTEND) && npm run build

test-frontend: $(FRONTEND)/node_modules
	cd $(FRONTEND) && npm test

# ------------------------------------------------------------------- firmware
build: build-start build-finish build-timer

build-start:
	$(NICE) $(PIO) run -d $(ROOT)/hhj-sensor -e start -j $(JOBS)
build-finish:
	$(NICE) $(PIO) run -d $(ROOT)/hhj-sensor -e finish -j $(JOBS)
build-timer:
	$(NICE) $(PIO) run -d $(ROOT)/hhj-timer -e stoper -j $(JOBS)
build-timer-test:
	$(NICE) $(PIO) run -d $(ROOT)/hhj-timer -e test -j $(JOBS)

flash-start:
	$(PIO) run -d $(ROOT)/hhj-sensor -e start -j $(JOBS) -t upload $(PORTFLAG_UPLOAD)
flash-finish:
	$(PIO) run -d $(ROOT)/hhj-sensor -e finish -j $(JOBS) -t upload $(PORTFLAG_UPLOAD)
flash-timer:
	$(PIO) run -d $(ROOT)/hhj-timer -e stoper -j $(JOBS) -t upload $(PORTFLAG_UPLOAD)
flash-timer-test:
	$(PIO) run -d $(ROOT)/hhj-timer -e test -j $(JOBS) -t upload $(PORTFLAG_UPLOAD)

monitor-sensor:
	$(PIO) device monitor -d $(ROOT)/hhj-sensor $(PORTFLAG_MONITOR)
monitor-timer:
	$(PIO) device monitor -d $(ROOT)/hhj-timer $(PORTFLAG_MONITOR)

clean:
	rm -rf $(BUILD)
