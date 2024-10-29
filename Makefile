EXE=parser
SRC=parse2.c

$(EXE): $(SRC)
	$(CC) $(SRC) -o $(EXE) -Wall -Wextra -Wpedantic -std=c2x
