CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I.. -I../../lib/Aurora-SDK/libs/DaisySP/Source

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

all: $(DOCTEST_H) test_warp_control
	./test_warp_control

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_warp_control: test_warp_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_warp_control $(DOCTEST_H)
