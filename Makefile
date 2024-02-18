PROJECT := tinyhttp
SOURCE := tinyhttp.c map.c
CC := gcc
CFLAGS := -Wall -Os

default: $(PROJECT)

$(PROJECT): $(SOURCE)
	$(CC) $(CFLAGS) -o $(PROJECT) $(SOURCE)

clean:
	rm $(PROJECT)

c:
	curl http://localhost:9000/index.html

nf:
	curl http://localhost:9000/notfound
