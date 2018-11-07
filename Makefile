CXX=g++
#CXXFLAGS = -Wall -std=c++11 -g 
CXXFLAGS = -Wno-unused-variable -Wall -std=c++11 -g 

SRC = whatsappClient.cpp whatsappServer.cpp 
#TARGETS = $(SRC) whatsappio.cpp

TARFLAGS=cvf
TARNAME=ex4.tar
TARFILES=whatsappClient.cpp whatsappServer.cpp whatsappio.cpp whatsappio.h Makefile README  

# All Target
all: whatsappClient whatsappServer

# Executables
whatsappClient: whatsappClient.o whatsappio.o
	$(CXX) $(CXXFLAGS) $^ -o $@

whatsappServer: whatsappServer.o whatsappio.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Object Files
whatsappClient.o: whatsappClient.cpp whatsappio.cpp whatsappio.h 
	$(CXX) $(CXXFLAGS) -c $<

whatsappServer.o: whatsappServer.cpp whatsappio.cpp whatsappio.h 
	$(CXX) $(CXXFLAGS) -c $<

whatsappio.o: whatsappio.cpp whatsappio.h
	$(CXX) $(CXXFLAGS) -c $<


clean:
	rm -f *.o whatsappClient whatsappServer ex4.tar

tar:
	tar $(TARFLAGS) $(TARNAME) $(TARFILES)
