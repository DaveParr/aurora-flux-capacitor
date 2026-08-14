CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I..

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

all: $(DOCTEST_H) test_colour test_audio
	./test_colour
	./test_audio

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_colour: test_colour.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_audio: test_audio.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_colour test_audio $(DOCTEST_H)
