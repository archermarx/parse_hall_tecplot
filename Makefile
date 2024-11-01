EXE=parser
SRC=parse.c

WARNINGS=-Wall -Wextra -Wpedantic
CCFLAGS = -gdwarf $(WARNINGS) -I ~/include

ifeq ($(DEBUG), 1)
	CCFLAGS += -O1 -fsanitize=address
else
	CCFLAGS += -O2
endif

all: $(EXE)

$(EXE): $(SRC)
	$(CC) $(SRC) -o $(EXE) $(CCFLAGS) 

.PHONY: remake clean

clean:
	@rm -f $(EXE)

remake: clean $(EXE)
