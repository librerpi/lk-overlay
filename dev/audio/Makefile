SAMPLERATE := 48000

all: sine-1000hz.wav sine-5000hz.wav sine-10000hz.wav sine-15000hz.wav sine-500hz.wav

sine-%hz.wav:
	sox -n -r $(SAMPLERATE) temp.wav synth 120 sine $(shell echo $@ | egrep -o '[0-9]*')
	sox -v 0.2 temp.wav -r $(SAMPLERATE) -b 16 $@
