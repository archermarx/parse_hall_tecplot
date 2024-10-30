EXE=parser
SRC=parse.c

$(EXE): $(SRC)
	$(CC) $(SRC) -o $(EXE) -Wall -Wextra -Wpedantic -std=c2x
