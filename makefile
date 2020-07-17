#list of all the directories, in the project
INC_DIR=./inc
OBJ_DIR=./obj
LIB_DIR=./lib
SRC_DIR=./src
BIN_DIR=./bin

CC=gcc
RM=rm -f

# figure out all the sources in the project
SOURCES:=${shell find $(SRC_DIR) -name '*.c'}
# and the required objects ot be built
OBJECTS:=$(patsubst $(SRC_DIR)%.c,$(OBJ_DIR)%.o,${SOURCES})
# the traget library
TARGET:=${BIN_DIR}/libbufferpoolman.a

# place your include directories -I flag here
CFLAGS=-Wall -O3 -I${INC_DIR}

# rule to make the object directory
${OBJ_DIR} :
	mkdir -p $@

# generic rule to build any object file
${OBJ_DIR}/%.o : ${SRC_DIR}/%.c ${INC_DIR}/%.h | ${OBJ_DIR}
	${CC} ${CFLAGS} -c $< -o $@

# rule to make the directory for binaries or libraries
${BIN_DIR} :
	mkdir -p $@

# generic rule to make a library target
$(TARGET) : ${OBJECTS} | ${BIN_DIR}
	ar rcs $@ ${OBJECTS}

# just build the target
all : $(TARGET)

# clean all the build, in this directory
# does not remove the existing installation
clean :
	$(RM) -r $(BIN_DIR) $(OBJ_DIR)

# install the library, from this directory to user environment path
install : all
	cp ${INC_DIR}/* /usr/local/include
	cp ${BIN_DIR}/* /usr/local/lib