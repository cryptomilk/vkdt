.PHONY:all clean rawspeed pthread-pool
BUILT=../built/ext

all: rawspeed pthread-pool

rawspeed: Makefile $(BUILT)/rawspeed/librawspeed.a

$(BUILT)/rawspeed/CMakeCache.txt: Makefile
	mkdir -p $(BUILT)/rawspeed
	cd $(BUILT)/rawspeed; cmake ../../../ext/rawspeed -DBUILD_TESTING=0 -DCMAKE_BUILD_TYPE=release

# for some reason this is challenging for older? cmake
# cmake -S rawspeed/ -B $(BUILT)/rawspeed/ -DBUILD_TESTING=0 -DCMAKE_BUILD_TYPE=release

$(BUILT)/rawspeed/librawspeed.a: Makefile $(BUILT)/rawspeed/CMakeCache.txt 
	mkdir -p $(BUILT)/rawspeed
	$(MAKE) -C $(BUILT)/rawspeed
	strip -S $(BUILT)/rawspeed/librawspeed.a

pthread-pool: Makefile
	mkdir -p $(BUILT)/pthread-pool
	$(MAKE) -C pthread-pool
	cp pthread-pool/libpthreadpool.a $(BUILT)/pthread-pool/
	strip -S $(BUILT)/pthread-pool/libpthreadpool.a

clean: Makefile
	$(MAKE) -C pthread-pool clean

