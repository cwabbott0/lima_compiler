CUR_DIR = $(shell pwd)
CFLAGS += -Wall -Wextra -g -fPIC -I $(CUR_DIR)

NAME = limasc
LIB_NAME = lib$(NAME).so
STANDALONE_NAME = $(NAME)

SOURCE  = . \
	gp_ir pp_hir pp_lir gp pp

STANDALONE_SOURCE = standalone

Y_SOURCE = $(patsubst %.y, %.c, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.y)))
Y_HEADER = $(patsubst %.y, %.h, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.y)))
L_SOURCE = $(patsubst %.l, %.c, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.l)))

Y_OBJECTS = $(patsubst %.y, %.o, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.y)))
L_OBJECTS = $(patsubst %.l, %.o, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.l)))
C_OBJECTS = $(patsubst %.c, %.o, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.c)))
STANDALONE_OBJECTS = $(patsubst %.c, %.o, $(foreach dir, $(STANDALONE_SOURCE), $(wildcard $(dir)/*.c)))
CXX_OBJECTS = $(patsubst %.cpp, %.o, $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.cpp)))
OBJECTS = $(Y_OBJECTS) $(L_OBJECTS) $(C_OBJECTS) $(CXX_OBJECTS)

all: $(LIB_NAME) $(STANDALONE_NAME)
lib: $(LIB_NAME)
standalone: $(STANDALONE_NAME)

clean:
	rm -f $(OBJECTS)
	rm -f $(LIB_NAME)
	rm -f $(Y_SOURCE) $(Y_HEADER)
	rm -f $(L_SOURCE)
	rm -f $(LIB_NAME_STATIC) $(LIB_NAME_DYNAMIC)
	rm -f essl/parser.output

$(Y_SOURCE): %.c: %.y
	bison -v -d -p $$(basename $$(dirname $@))_yy -o $@ $<

$(L_SOURCE): %.c: %.l $(Y_SOURCE)
	flex -o $@ $<

$(C_OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(CXX_OBJECTS): %.o: %.c
	$(CXX) $(CFLAGS) -c -o $@ $<

$(STANDALONE_OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(STANDALONE_NAME): $(OBJECTS) $(STANDALONE_OBJECTS)
	$(CC) -g -o $@ $^

$(LIB_NAME): $(OBJECTS)
	$(CC) -shared -g -o $@ $^



