all: clean train play

train:
	cc train.c common.h -o train -O3 -Wall -W -ffast-math -lm

play:
	cc play.c common.h -o play -O3 -Wall -W -ffast-math -lm

clean:
	rm -f train play 2>/dev/null
