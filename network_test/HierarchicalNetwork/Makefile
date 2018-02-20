BIN=hierarchical_bcast
CXX = CC
CXXFLAGS = -O3
RM = rm
#CC ./hierarchical_bcast.cpp `pkg-config --libs --cflags photon` -std=c++11
CXXFLAGS += -std=c++11
#CXXFLAGS += $(shell pkg-config --libs --cflags photon)
#LDFLAGS += $(shell pkg-config --libs --cflags photon)

CXXFLAGS += -I/N/u/wangyinz/BigRed2/hpx_test/photon_install/include
##/N/u/wangyinz/BigRed2/hpx_test/hpx/include `pkg-config --cflags hpx` `pkg-config --libs hpx`
LDFLAGS += -L/N/u/wangyinz/BigRed2/hpx_test/photon_install/lib
LDLIBS = -lphoton #-lrt -lmpi
OBJS = hierarchical_bcast.o 
$(BIN) : $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

.PHONY:clean
clean:
	$(RM) $(BIN) $(OBJS) 

