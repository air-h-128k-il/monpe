
plugin_sources = \
	diacairo.c \
	diacairo.h \
	diacairo-interactive.c \
	diacairo-renderer.c \
	diacairo-print.c \
	diacairo-print.h 

pkglib_LTLIBRARIES = libcairo_filter.la

EXTRA_DIST =

libcairo_filter_la_SOURCES = $(plugin_sources)

libcairo_filter_la_LDFLAGS = -export-dynamic -module -avoid-version
libcairo_filter_la_LIBADD = $(CAIRO_LIBS) $(top_builddir)/lib/libmonpe.la

INCLUDES = -I$(srcdir)/../../lib $(GTK_CFLAGS) $(DEBUG_FLAGS) $(CAIRO_CFLAGS)
