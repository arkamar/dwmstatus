# See LICENSE file for copyright and license details.

include config.mk

SRC = $(NAME).c
OBJ = $(SRC:.c=.o)

.PHONY: all
all: $(NAME)

.PHONY: clean
clean:
	$(RM) $(NAME) $(OBJ)
