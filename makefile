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
# the library, which we will create
LIBRARY:=${LIB_DIR}/libbufferpoolman.a
# the binary, which will use the created library
BINARY:=${BIN_DIR}/bufferpoolman.out

# compiler flags
CFLAGS=-Wall -O3 -I${INC_DIR}
# linker flags, this will used to compile the binary
LFLAGS=-lbufferpoolman -lboompar -lrwlock -lpthread -lcutlery

# rule to make the object directory
${OBJ_DIR} :
	mkdir -p $@

# rule to make the directory for storing libraries, that we create
${LIB_DIR} :
	mkdir -p $@

# rule to make the directory for storing binaries, that we create
${BIN_DIR} :
	mkdir -p $@

# generic rule to build any object file
${OBJ_DIR}/%.o : ${SRC_DIR}/%.c ${INC_DIR}/%.h | ${OBJ_DIR}
	${CC} ${CFLAGS} -c $< -o $@

# generic rule to make a library
${LIBRARY} : ${OBJECTS} | ${LIB_DIR}
	ar rcs $@ ${OBJECTS}

# generic rule to make a binary using the library that we just created
${BINARY} : ./main.c ${LIBRARY} | ${BIN_DIR}
	${CC} ${CFLAGS} $< ${LFLAGS} -o $@

# to build the binary along with the library, if your project has a binary aswell
#all : ${BINARY}
# else if your project is only a library use this
all : ${LIBRARY}

# clean all the build, in this directory
# does not remove the existing installation
clean :
	$(RM) -r ${BIN_DIR} ${LIB_DIR} ${OBJ_DIR}

# install the library, from this directory to user environment path
install : all
	cp ${INC_DIR}/* /usr/local/include
	cp ${LIB_DIR}/* /usr/local/lib
	#cp ${BIN_DIR}/* /usr/local/bin
