set(REVISIONS "e1" "e2")

if (NOT DEFINED BOARD_REVISION)
  set(BOARD_REVISION "e1")
else()
  if (NOT BOARD_REVISION IN_LIST REVISIONS)
    message(FATAL_ERROR "${BOARD_REVISION} is not a valid revision for am13e230x_lp. Accepted revisions: ${REVISIONS}")
  endif()
endif()
