# Host-side unit tests — no PlatformIO, no board, no network.
#   make        build and run every test
#   make clean  remove the binaries
#
# Everything under test here is pure logic. Anything that touches Arduino, WiFi
# or the panel lives behind an #ifdef ARDUINO seam and is exercised on hardware.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -I../src -I../third_party

BIN_ROUTE = test_route
SRC_ROUTE = test_route.cpp ../src/route.cpp

BIN_TT    = test_timetable
SRC_TT    = test_timetable.cpp ../src/timetable.cpp ../src/route.cpp

BIN_SIRI  = test_siri_parse
SRC_SIRI  = test_siri_parse.cpp ../src/siri_parse.cpp

BIN_BOARD = test_board_model
SRC_BOARD = test_board_model.cpp ../src/board_model.cpp ../src/timetable.cpp ../src/route.cpp ../src/siri_parse.cpp

BIN_SDAY  = test_service_day
SRC_SDAY  = test_service_day.cpp ../src/service_day.cpp ../src/timetable.cpp ../src/route.cpp

BIN_CFG   = test_config
SRC_CFG   = test_config.cpp ../src/config.cpp ../src/route.cpp

BIN_OTAMAN = test_ota_manifest
SRC_OTAMAN = test_ota_manifest.cpp ../src/ota_manifest.cpp

BIN_OTAHLTH = test_ota_health
SRC_OTAHLTH = test_ota_health.cpp ../src/ota_health.cpp

# The only host test with a link dependency. mbedtls 2.28 is the same minor
# version the ESP32 core ships (2.28.7), so this exercises the same API the
# device runs rather than a stand-in. Install with:
#   sudo apt-get install -y libmbedtls-dev
BIN_OTAVER = test_ota_verify
SRC_OTAVER = test_ota_verify.cpp ../src/ota_verify.cpp
LIBS_OTAVER = -lmbedcrypto -lmbedx509

# Header-only: htmlEscape() and csrfMatches() were extracted from portal.cpp,
# where they lived `static` behind #ifdef ARDUINO and had zero host coverage
# (F-6 from the 2026-08 adversarial review).
BIN_HTMLESC = test_html_escape
SRC_HTMLESC = test_html_escape.cpp

BIN_CSRF  = test_csrf_check
SRC_CSRF  = test_csrf_check.cpp

# Password-vs-SSID disambiguation added alongside the F-2 token fix -- see
# ../src/wifi_pass_policy.h.
BIN_WPASS = test_wifi_pass_policy
SRC_WPASS = test_wifi_pass_policy.cpp

# Header-only, and host-tested for the same reason as the two above:
# urgencyBandsText() was written inside portal.cpp behind #ifdef ARDUINO, where
# its off-by-one (an exclusive bound printed as an inclusive band) was
# unreachable from any test.
BIN_URG   = test_urgency
SRC_URG   = test_urgency.cpp

BINS = $(BIN_ROUTE) $(BIN_TT) $(BIN_SIRI) $(BIN_BOARD) $(BIN_SDAY) $(BIN_CFG) $(BIN_OTAMAN) $(BIN_OTAHLTH) $(BIN_OTAVER) $(BIN_HTMLESC) $(BIN_CSRF) $(BIN_WPASS) $(BIN_URG)

all: $(BINS)
	@fail=0; for b in $(BINS); do ./$$b || fail=1; done; \
	 if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; fi; \
	 exit $$fail

$(BIN_ROUTE): $(SRC_ROUTE) ../src/route.h ../src/stations.h
	$(CXX) $(CXXFLAGS) $(SRC_ROUTE) -o $(BIN_ROUTE)

$(BIN_TT): $(SRC_TT) ../src/timetable.h ../src/timetable_data.h ../src/route.h ../src/stations.h
	$(CXX) $(CXXFLAGS) $(SRC_TT) -o $(BIN_TT)

$(BIN_SIRI): $(SRC_SIRI) ../src/siri_parse.h ../src/timetable_data.h ../third_party/ArduinoJson.h
	$(CXX) $(CXXFLAGS) $(SRC_SIRI) -o $(BIN_SIRI)

$(BIN_BOARD): $(SRC_BOARD) ../src/board_model.h ../src/urgency.h ../src/timetable.h ../src/siri_parse.h
	$(CXX) $(CXXFLAGS) $(SRC_BOARD) -o $(BIN_BOARD)

$(BIN_SDAY): $(SRC_SDAY) ../src/service_day.h ../src/timetable.h
	$(CXX) $(CXXFLAGS) $(SRC_SDAY) -o $(BIN_SDAY)

$(BIN_CFG): $(SRC_CFG) ../src/config.h ../src/urgency.h ../src/route.h
	$(CXX) $(CXXFLAGS) $(SRC_CFG) -o $(BIN_CFG)

$(BIN_OTAMAN): $(SRC_OTAMAN) ../src/ota_manifest.h
	$(CXX) $(CXXFLAGS) $(SRC_OTAMAN) -o $(BIN_OTAMAN)

$(BIN_OTAHLTH): $(SRC_OTAHLTH) ../src/ota_health.h
	$(CXX) $(CXXFLAGS) $(SRC_OTAHLTH) -o $(BIN_OTAHLTH)

$(BIN_OTAVER): $(SRC_OTAVER) ../src/ota_verify.h
	$(CXX) $(CXXFLAGS) $(SRC_OTAVER) $(LIBS_OTAVER) -o $(BIN_OTAVER)

$(BIN_HTMLESC): $(SRC_HTMLESC) ../src/html_escape.h
	$(CXX) $(CXXFLAGS) $(SRC_HTMLESC) -o $(BIN_HTMLESC)

$(BIN_CSRF): $(SRC_CSRF) ../src/csrf_check.h
	$(CXX) $(CXXFLAGS) $(SRC_CSRF) -o $(BIN_CSRF)

$(BIN_WPASS): $(SRC_WPASS) ../src/wifi_pass_policy.h
	$(CXX) $(CXXFLAGS) $(SRC_WPASS) -o $(BIN_WPASS)

$(BIN_URG): $(SRC_URG) ../src/urgency.h
	$(CXX) $(CXXFLAGS) $(SRC_URG) -o $(BIN_URG)

clean:
	rm -f $(BINS)

# --- Fuzzing ----------------------------------------------------------------
# libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer target for
# siri_parse.cpp -- the only code in this project that parses bytes from
# outside (the 511 SIRI API, over HTTPS). Deliberately kept out of `all`
# above and out of CXX/CXXFLAGS: `make` must stay plain-g++ with no clang
# dependency for anyone who just wants to run the host suite.
#
#   make fuzz        build fuzz/fuzz_siri_parse
#   make fuzz-run     build it and fuzz for FUZZ_SECONDS (default 60) against
#                     a corpus seeded from the real captured fixture plus the
#                     hand-made seeds in fuzz/seeds/
#   make fuzz-clean   remove the fuzz binary and the grown corpus
FUZZ_CXX      ?= clang++
FUZZ_CXXFLAGS ?= -std=c++17 -Wall -Wextra -I../src -I../third_party -g -O1 \
                 -fsanitize=fuzzer,address,undefined

BIN_FUZZ = fuzz/fuzz_siri_parse
SRC_FUZZ = fuzz/fuzz_siri_parse.cpp ../src/siri_parse.cpp

fuzz: $(BIN_FUZZ)

$(BIN_FUZZ): $(SRC_FUZZ) ../src/siri_parse.h
	$(FUZZ_CXX) $(FUZZ_CXXFLAGS) $(SRC_FUZZ) -o $(BIN_FUZZ)

FUZZ_SECONDS ?= 60
fuzz-run: $(BIN_FUZZ)
	mkdir -p fuzz/corpus
	cp -n fuzz/seeds/* fuzz/corpus/ 2>/dev/null || true
	cp -n fixtures/stopmonitoring_70012.json fuzz/corpus/ 2>/dev/null || true
	./$(BIN_FUZZ) -max_total_time=$(FUZZ_SECONDS) -artifact_prefix=fuzz/corpus/ fuzz/corpus

fuzz-clean:
	rm -f $(BIN_FUZZ)
	rm -rf fuzz/corpus

.PHONY: all clean fuzz fuzz-run fuzz-clean
