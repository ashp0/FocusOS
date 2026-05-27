# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/focusos_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/focusos_autogen.dir/ParseCache.txt"
  "CMakeFiles/totp_tests_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/totp_tests_autogen.dir/ParseCache.txt"
  "focusos_autogen"
  "totp_tests_autogen"
  )
endif()
