# === Variables principales ===

CC       := gcc
CC_WIN   := x86_64-w64-mingw32-gcc

NAME     := tracker_loot
NAME_WIN := $(NAME).exe

BUILD    := build
BIN      := bin
DATA_FILES := armes.ini markup.ini

INCLUDES := -Iinclude

CFLAGS_COMMON := -Wall -Wextra -Iinclude
CFLAGS_C99    := -std=c99
CFLAGS_LINUX  :=
CFLAGS_WIN    :=

# Enable Werror by default (dev-friendly), allow disabling via: make WERROR=0
WERROR ?= 1
ifeq ($(WERROR),1)
CFLAGS_COMMON += -Werror
endif

# === Link flags selon OS ===
# Prefer pkg-config for X11 (portable across distros). Fallback to -lX11.
X11_CFLAGS := $(shell pkg-config --cflags x11 2>/dev/null)
X11_LIBS   := $(shell pkg-config --libs x11 2>/dev/null)
ifeq ($(strip $(X11_LIBS)),)
X11_LIBS = -lX11
endif
CFLAGS_LINUX += $(X11_CFLAGS)
LDFLAGS_LINUX := $(X11_LIBS) -lm
LDFLAGS_WIN   := -luser32 -lgdi32

# === Sources ===
LEGACY ?= 0

SRC_ALL := $(wildcard src/*.c)
# Modules currently not wired to the main flow (keep available via LEGACY=1)
SRC_EXCLUDE := \
	src/data_csv.c \
	src/csv_journal.c \
	src/ui_key.c \
	src/tracker_view.c

ifeq ($(LEGACY),1)
SRC := $(SRC_ALL)
else
SRC := $(filter-out $(SRC_EXCLUDE), $(SRC_ALL))
endif

OBJ      := $(patsubst %.c, $(BUILD)/%.o, $(SRC))
OBJ_WIN  := $(patsubst %.c, $(BUILD)/win/%.o, $(SRC))

# === Phony ===
.PHONY: all win clean debug release sanitize run

# === Build Linux ===

all: $(BIN)/$(NAME)

$(BIN)/$(NAME): $(OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS_LINUX)
	@for f in $(DATA_FILES); do cp -f $$f $(BIN)/ 2>/dev/null || true; done

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_C99) $(CFLAGS_COMMON) $(CFLAGS_LINUX) $(INCLUDES) -c $< -o $@

# === Build Windows (.exe) ===

win: $(BIN)/$(NAME_WIN)

$(BIN)/$(NAME_WIN): $(OBJ_WIN)
	@echo "====> Compilation pour Windows (.exe)"
	@mkdir -p $(BIN)
	$(CC_WIN) $^ -o $@ $(LDFLAGS_WIN)
	@for f in $(DATA_FILES); do cp -f $$f $(BIN)/ 2>/dev/null || true; done
	@echo "====> Fichier genere : $(BIN)/$(NAME_WIN)"

$(BUILD)/win/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC_WIN) $(CFLAGS_C99) $(CFLAGS_COMMON) $(CFLAGS_WIN) $(INCLUDES) -c $< -o $@

# === Regles utilitaires ===

run: all
	./$(BIN)/$(NAME)

debug: CFLAGS_COMMON += -g -DDEBUG
debug: clean all

release: CFLAGS_COMMON += -O2
release: clean all

sanitize: CFLAGS_COMMON += -g -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS_LINUX += -fsanitize=address,undefined
sanitize: clean all

clean:
	rm -rf $(BUILD) $(BIN)
