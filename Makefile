install: a.out
	systemctl stop heating-watcher
	cp a.out /opt/heating-watcher
	systemctl start heating-watcher

a.out: *.cpp
	g++ -std=c++23 *.cpp -I./libsetila/include -L./libsetila/build -lstdc++ -lgpiod -lgpiodcxx -lsetila -lmicrohttpd
