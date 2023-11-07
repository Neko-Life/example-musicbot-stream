all: bot

bot: main.cpp
	clang -flto -std=c++17 -stdlib=libc++ -g -I../Musicat/libs/DPP/include -L../Musicat/build/libs/DPP/library main.cpp -ldpp -lc++ -o bot

run:
	LD_LIBRARY_PATH=$(PWD)/../Musicat/build/libs/DPP/library ./bot
