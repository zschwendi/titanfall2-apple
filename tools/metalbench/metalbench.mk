# Offscreen Metal measurements of this GPU's own rates (tools/metalbench/README.md).  Included by the root Makefile.
#
#   make metalbench            build build/metalbench/metalbench
#   build/metalbench/metalbench --quick

MB := tools/metalbench

.PHONY: metalbench

build/metalbench/metalbench: $(MB)/metalbench.m
	@mkdir -p build/metalbench
	xcrun clang -fobjc-arc -O2 -Wall -Wextra -framework Metal -framework Foundation -o $@ $<

metalbench: build/metalbench/metalbench
