# Common makefile targets for libraries to use, if they dont need a custom implementation.
# NOTE: these expect the following variables (most others will come from the root makefile):
# CXX_FLAGS = c++ compiler flags
# CXX_SRCS = c++ source files
# TARGET = library name

OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
TARGET_STATIC = $(LIBS_OUTPUT_DIR)/lib$(TARGET).a

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: clean all

all: $(TARGET_STATIC)

$(TARGET_STATIC): $(OBJS)
	@printf "$(C_BLUE)[$(TARGET)]$(C_RST) Creating static library ...\r\n"
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_AR_BIN) -rcs $(TARGET_STATIC) $(OBJS)
	$(LOUD)printf "$(C_BLUE)[$(TARGET)]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

clean:
	@printf "$(C_BLUE)[$(TARGET)]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(BUILD_DIR)
	$(LOUD)-rm -r $(TARGET_STATIC)
	@printf "$(C_BLUE)[$(TARGET)]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

$(BUILD_DIR)/%.cpp.o: %.cpp
	@printf "$(C_BLUE)[$(TARGET)]$(C_RST) Compiling C++ source: $<\r\n"
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(CXX_FLAGS) -c $< -o $@

