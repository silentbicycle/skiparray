PROJECT =	skiparray
BUILD =		build
INCLUDE =	include
SRC =		src
TEST =		test
VENDOR =	vendor

INCDEPS =	${INCLUDE}/*.h ${SRC}/*.h
STATIC_LIB=	lib${PROJECT}.a

COVERAGE =	-fprofile-arcs -ftest-coverage
PROFILE =	-pg

OPTIMIZE = 	-O3

WARN =		-Wall -pedantic -Wextra
CDEFS +=
CINCS +=	-I${INCLUDE}
CINCS +=	-I${VENDOR}
CSTD +=		-std=c99
CDEBUG =	-ggdb3

CFLAGS +=	${CSTD} ${CDEBUG} ${OPTIMIZE} ${SAN}
CFLAGS +=	${WARN} ${CDEFS} ${CINCS}
LDFLAGS +=	${CDEBUG} ${SAN}

TEST_CFLAGS_theft =	$(shell pkg-config --cflags libtheft)
TEST_LDFLAGS_theft =	$(shell pkg-config --libs libtheft)

TEST_CFLAGS = 	${CFLAGS} -I${SRC} ${TEST_CFLAGS_theft}
TEST_LDFLAGS =  ${LDFLAGS} ${TEST_LDFLAGS_theft}

all: library

everything: library ${BUILD}/test_${PROJECT} ${BUILD}/benchmarks

OBJS=		${BUILD}/skiparray.o \

TEST_OBJS=	${OBJS} \
		${BUILD}/test_${PROJECT}.o \
		${BUILD}/test_${PROJECT}_basic.o \
		${BUILD}/test_${PROJECT}_prop.o \
		${BUILD}/test_${PROJECT}_integration.o \
		${BUILD}/test_${PROJECT}_invariants.o \
		${BUILD}/type_info_${PROJECT}_operations.o \

BENCH_OBJS=	${BUILD}/bench.o \
		${BUILD}/test_${PROJECT}_invariants.o \


# Basic targets

test: ${BUILD}/test_${PROJECT}
	${BUILD}/test_${PROJECT} ${ARGS}

clean:
	rm -rf ${BUILD}

${BUILD}/${PROJECT}: ${OBJS}
	${CC} -o $@ $+ ${LDFLAGS}

library: ${BUILD}/${STATIC_LIB}

${BUILD}/${STATIC_LIB}: ${OBJS}
	ar -rcs ${BUILD}/${STATIC_LIB} $+

${BUILD}/test_${PROJECT}: ${TEST_OBJS}
	${CC} -o $@ $+ ${TEST_CFLAGS} ${TEST_LDFLAGS}

${BUILD}/%.o: ${SRC}/%.c ${INCDEPS} | ${BUILD}
	${CC} -c -o $@ ${CFLAGS} $<

${BUILD}/%.o: ${TEST}/%.c ${INCDEPS} | ${BUILD}
	${CC} -c -o $@ ${TEST_CFLAGS} $<


# Other targets

bench: ${BUILD}/benchmarks | ${BUILD}
	${BUILD}/benchmarks ${ARGS}

tags: ${BUILD}/TAGS

${BUILD}/TAGS: ${SRC}/*.c ${INCDEPS} | ${BUILD}
	etags -o $@ ${SRC}/*.[ch] ${INCDEPS} ${TEST}/*.[ch]

${BUILD}/benchmarks: ${BUILD}/${STATIC_LIB} ${BENCH_OBJS} | ${BUILD}
	${CC} -o $@ ${BENCH_OBJS} ${OPTIMIZE} ${LDFLAGS} ${BUILD}/${STATIC_LIB}

coverage: OPTIMIZE=-O0 ${COVERAGE}
coverage: CC=gcc

coverage: test | ${BUILD}/cover
	ls -1 src/*.c | sed -e "s#src/#build/#" | xargs -n1 gcov
	@echo moving coverage files to ${BUILD}/cover
	mv *.gcov ${BUILD}/cover

${BUILD}/cover: | ${BUILD}
	mkdir ${BUILD}/cover

profile: profile_perf

profile_perf: ${BUILD}/benchmarks
	perf record ${BUILD}/benchmarks
	perf report

profile_gprof: CFLAGS+=${PROFILE}
profile_gprof: LDFLAGS+=${PROFILE}

profile_gprof: ${BUILD}/benchmarks
	${BUILD}/benchmarks ${ARGS}
	gprof ${BUILD}/benchmarks

leak_check: CC=clang
leak_check: SAN=-fsanitize=memory,undefined
leak_check: test

${BUILD}:
	mkdir ${BUILD}

${BUILD}/*.o: ${INCLUDE}/*.h
${BUILD}/*.o: ${SRC}/*.h
${BUILD}/*.o: Makefile


# Installation

PREFIX ?=	/usr/local
LIBDIR ?=	lib
INSTALL ?=	install
RM ?=		rm

install: install_lib install_pc

uninstall: uninstall_lib install_pc

install_lib: ${BUILD}/${STATIC_LIB} ${INCLUDE}/${PROJECT}.h
	${INSTALL} -d -m 755 ${DESTDIR}${PREFIX}/${LIBDIR}
	${INSTALL} -c -m 644 ${BUILD}/lib${PROJECT}.a ${DESTDIR}${PREFIX}/${LIBDIR}
	${INSTALL} -d -m 755 ${DESTDIR}${PREFIX}/include
	${INSTALL} -c -m 644 ${INCLUDE}/${PROJECT}.h ${DESTDIR}${PREFIX}/include

${BUILD}/%.pc: pc/%.pc.in | ${BUILD}
	sed -e 's,@prefix@,${PREFIX},g' -e 's,@libdir@,${LIBDIR},g' $< > $@

install_pc: ${BUILD}/lib${PROJECT}.pc
	${INSTALL} -d -m 755 ${DESTDIR}${PREFIX}/${LIBDIR}/pkgconfig/
	${INSTALL} -c -m 644 ${BUILD}/lib${PROJECT}.pc ${DESTDIR}${PREFIX}/${LIBDIR}/pkgconfig/

uninstall_lib:
	${RM} -f ${DESTDIR}${PREFIX}/${LIBDIR}/lib${PROJECT}.a
	${RM} -f ${DESTDIR}${PREFIX}/include/${PROJECT}.h

uninstall_pc:
	${RM} -f ${DESTDIR}${PREFIX}/${LIBDIR}/lib${PROJECT}.pc

.PHONY: test clean tags coverage profile leak_check \
	everything library bench profile profile_perf profile_gprof \
	install install_lib install_pc uninstall uninstall_lib uninstall_pc
