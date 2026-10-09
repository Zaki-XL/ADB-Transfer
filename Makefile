CXX ?= scratch/w64devkit/bin/g++.exe
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude
LDFLAGS_GUI ?= -static -municode -mwindows -lcomctl32 -luxtheme
LDFLAGS_TEST ?= -static -municode -lcomctl32 -luxtheme

SRCS = src/ConfigManager.cpp \
       src/I18nManager.cpp \
       src/AdbManager.cpp \
       src/StorageScanner.cpp \
       src/TransferEngine.cpp \
       src/DeviceManager.cpp \
       src/MainWindow.cpp \
       src/main.cpp

OBJS = $(SRCS:.cpp=.o)

TEST_SRCS = src/ConfigManager.cpp \
            src/I18nManager.cpp \
            src/AdbManager.cpp \
            src/StorageScanner.cpp \
            src/TransferEngine.cpp \
            src/DeviceManager.cpp \
            tests/test_config.cpp \
            tests/test_storage.cpp \
            tests/test_transfer.cpp \
            tests/test_i18n.cpp \
            tests/test_main.cpp

TEST_OBJS = $(TEST_SRCS:.cpp=.o)

all: bin/AdbTransfer.exe tests/test_runner.exe sync_lang

bin/AdbTransfer.exe: $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS_GUI)
	@if [ -d Release ]; then rm -f Release/AdbTransfer.exe && cp $@ Release/AdbTransfer.exe; fi

tests/test_runner.exe: $(TEST_OBJS)
	@mkdir -p tests
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS_TEST)

sync_lang:
	@mkdir -p bin/lang
	@cp -rf lang/* bin/lang/
	@if [ -d Release ]; then mkdir -p Release/lang && cp -rf lang/* Release/lang/; fi

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TEST_OBJS) bin/AdbTransfer.exe tests/test_runner.exe

.PHONY: all clean sync_lang
