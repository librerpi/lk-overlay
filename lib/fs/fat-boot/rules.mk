LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

# Use LK's FAT implementation without its test fixture, which the pinned
# upstream LK rules include unconditionally on this revision.
MODULE_DEPS += lib/bcache lib/bio lib/fs lib/libcpp
MODULE_SRCS += $(addprefix lk/lib/fs/fat/,dir.cpp fat.cpp file.cpp file_iterator.cpp fs.cpp)
MODULE_COMPILEFLAGS += -Wmissing-declarations

include make/module.mk
