#CC := clang

SRCDIR := src
BUILDDIR := build
TARGETDIR := bin
TARGET := $(TARGETDIR)/afk_plugin.dylib

SRCEXT := c
SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))

CFLAGS := -g -Wall
LDFLAGS := -shared

LIB := -lpthread -framework IOKit -framework CoreFoundation
INC := -I include

$(TARGET): $(OBJECTS)
	@echo " Linking..."
	@mkdir -p $(TARGETDIR)
	@echo " $(CC) $^ -o $(TARGET) $(LIB) $(LDFLAGS)"; $(CC) $^ -o $(TARGET) $(LIB) $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	@echo " $(CC) $(CFLAGS) $(INC) -c -o $@ $<"; $(CC) $(CFLAGS) $(INC) -c -o $@ $<

clean:
	@echo " Cleaning...";
	@echo " $(RM) -r $(BUILDDIR) $(TARGET)"; $(RM) -r $(BUILDDIR) $(TARGET)

.PHONY: clean
