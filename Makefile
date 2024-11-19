# Copyright (C) 2024 Aleksei Rogov <alekzzzr@gmail.com>. All rights reserved.

PROJECT := tinyhttp
SOURCE := tinyhttp.c map.c
HEADERS := tinyhttp.h map.h
CC := gcc
CFLAGS := -Wall -Os

default: $(PROJECT)

$(PROJECT): $(SOURCE) $(HEADERS)
	$(CC) $(CFLAGS) -o $(PROJECT) $(SOURCE)

clean:
	rm $(PROJECT)
