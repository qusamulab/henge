# henge top-level build. Run `make help` for the list of targets.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

include mk/config.mk
include mk/sources.mk
include mk/libraries.mk
include mk/samples.mk
include mk/tests.mk
include mk/app.mk

.PHONY: all help samples samples-apk tests test test-layout test-m1 test-patch test-dl test-shim test-cxx test-vm test-activity test-sign test-apk test-library app-dylib app app-run clean

all: $(B)/henge-convert $(B)/henge-apk $(B)/libhenge-runtime.a samples tests ## build the tools, runtime, sample and tests

help: ## show this help
	@echo "usage: make [target] [VAR=value ...]"
	@echo
	@grep -hE '^[a-zA-Z0-9_-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-12s %s\n", $$1, $$2}'
	@echo
	@echo "variables: NDK_ROOT=$(NDK_ROOT) ANDROID_HOME=$(ANDROID_HOME)"
	@echo "           PLATFORM=$(PLATFORM) SIM=\"$(SIM)\" CONVERT_FLAGS=$(CONVERT_FLAGS)"

clean: ## remove build/
	rm -rf $(B)

# Missing dependency files are expected on the first build.
-include $(addsuffix .d,$(ALL_OBJS) $(B)/henge-convert $(B)/henge-apk $(addprefix $(B)/tests/,$(TESTS)))
