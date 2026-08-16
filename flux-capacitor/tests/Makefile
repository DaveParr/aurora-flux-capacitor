CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I.. -I../../lib/Aurora-SDK/libs/DaisySP/Source -I../../lib/Aurora-SDK/libs/DaisySP/Source/Utility -I../../lib/Aurora-SDK/libs/DaisySP/Source/Control

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

DAISYSP_SRC    = ../../lib/Aurora-SDK/libs/DaisySP/Source/Control/phasor.cpp
OSCILLATOR_SRC = ../../lib/Aurora-SDK/libs/DaisySP/Source/Synthesis/oscillator.cpp

all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
	./test_mix_control
	./test_tape_transport
	./test_wow_flutter

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_warp_control: test_warp_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_tape_voice: test_tape_voice.cpp $(DAISYSP_SRC) $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DAISYSP_SRC)

test_dsp_util: test_dsp_util.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_mix_control: test_mix_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_tape_transport: test_tape_transport.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_wow_flutter: test_wow_flutter.cpp $(OSCILLATOR_SRC) $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $< $(OSCILLATOR_SRC)

clean:
	rm -f test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter $(DOCTEST_H)
