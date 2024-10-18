gotoall: all

# Clean previous builds sequels
clean:
	-rm TeleInfod
	-rm src/*.o

# Build everything
all:
	$(MAKE) -C src/
