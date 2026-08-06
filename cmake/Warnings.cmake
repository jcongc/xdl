# A single interface target carrying the project's warning set, so every
# target opts in the same way: target_link_libraries(foo PUBLIC xdl_warnings).
add_library(xdl_warnings INTERFACE)

target_compile_options(xdl_warnings INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
  -Wnon-virtual-dtor
)

if(XDL_WARNINGS_AS_ERRORS)
  target_compile_options(xdl_warnings INTERFACE -Werror)
endif()
