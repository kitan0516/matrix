# Makefile for SDK module

CFLAGS := -Wall -nostdlib -nostdinc -fno-builtin -fno-stack-protector

ASFLAGS := -felf

OBJ := ../bin/sdk

TARGETOBJ := \
	$(OBJ)/crt1.o \
	$(OBJ)/libinit.o \
	$(OBJ)/syscalls.o \
	$(OBJ)/div64.o \
	$(OBJ)/stdio.o \
	$(OBJ)/string.o \
	$(OBJ)/vsprintf.o \
	$(OBJ)/sprintf.o \
	$(OBJ)/printf.o \
	$(OBJ)/format.o \
	$(OBJ)/time.o \


.PHONY: clean help

all:	$(TARGETOBJ)

$(OBJ)/%.o: %.c
	$(CC) $(CFLAGS) -m32 -Iinclude -c -o $@ $<

$(OBJ)/%.o: libc/%.c
	$(CC) $(CFLAGS) -DBITS_PER_LONG=32 -m32 -Iinclude -c -o $@ $<

$(OBJ)/crt1.o:
	nasm $(ASFLAGS) -o $(OBJ)/crt1.o crt1.s

clean:
	$(RM) $(OBJ)/*.o