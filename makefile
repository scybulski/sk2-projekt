all: directories rebecca OnlinePaint

OnlinePaint: OnlinePaint.o
	g++ -Wall obj/OnlinePaint.o -o bin/OnlinePaint -lpthread `pkg-config gtk+-3.0 goocanvas-2.0 goocanvasmm-2.0 pthread-stubs json-c --libs` 

rebecca: rebecca.o
	g++ -Wall obj/rebecca.o -o bin/rebecca `pkg-config json-c --libs`

OnlinePaint.o: OnlinePaint/main.cpp
	g++ -o obj/OnlinePaint.o OnlinePaint/main.cpp -fexceptions `pkg-config gtk+-3.0 goocanvas-2.0 goocanvasmm-2.0 json-c pthread-stubs json-c --cflags` -Wall -std=c++11 -c
	
rebecca.o: OnlinePaintServer/main.cpp
	g++ -o obj/rebecca.o OnlinePaintServer/main.cpp -fexceptions `pkg-config json-c --cflags` -Wall -std=c++11 -c
	

directories: 
	mkdir -p bin
	mkdir -p obj

