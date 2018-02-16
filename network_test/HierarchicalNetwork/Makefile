BIN=hierarchical_network_test
CXX = mpic++ -std=c++11
CXXFLAGS = -O3
RM = rm

CXXFLAGS += -I/N/u/wangyinz/Karst/HPX_project/network_test/install/include
LDFLAGS += -L/N/u/wangyinz/Karst/HPX_project/network_test/install/lib
LDLIBS = -lphoton -lrt -lmpi
OBJS = hierarchical_network_test.o 
$(BIN) : $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

.PHONY:clean
clean:
	$(RM) $(BIN) $(OBJS) 

