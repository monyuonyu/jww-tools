# ctest driver for --check / --strict / --json issues / --merge-collinear on small drawings.
# check_*.jww are tests/data/basic.jww with the second line moved (see tests/data/README.md).
set(failed 0)
macro(fail msg)
  message(SEND_ERROR "${msg}")
  math(EXPR failed "${failed}+1")
endmacro()
set(D ${SRC}/tests/data)
set(B ${CMAKE_CURRENT_BINARY_DIR})

# name, expected exit code with --check --strict, text expected on stderr
foreach(c "basic.jww;0;no duplicates or overlaps"
          "check_dup.jww;4;duplicate lines: 1 group (1 on one layer, 0 across layers)"
          "check_overlap.jww;4;overlapping collinear lines: 1 pair"
          "check_touch.jww;0;collinear lines touching end to end")
  list(GET c 0 name)
  list(GET c 1 want_rc)
  list(GET c 2 want_err)
  execute_process(COMMAND ${JWW2DXF} --check --strict ${D}/${name} -o ${B}/chk_${name}.dxf RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL want_rc)
    fail("${name}: --check --strict exit ${rc}, expected ${want_rc}")
  endif()
  string(FIND "${err}" "${want_err}" pos)
  if(pos LESS 0)
    fail("${name}: stderr lacks '${want_err}': ${err}")
  endif()
  # --check does not change the DXF
  execute_process(COMMAND ${JWW2DXF} -q ${D}/${name} -o ${B}/plain_${name}.dxf)
  file(SHA256 ${B}/chk_${name}.dxf h1)
  file(SHA256 ${B}/plain_${name}.dxf h2)
  if(NOT h1 STREQUAL h2)
    fail("${name}: --check changed the DXF")
  endif()
endforeach()

# without --strict the exit code stays 0
execute_process(COMMAND ${JWW2DXF} --check ${D}/check_overlap.jww -o ${B}/chk2.dxf RESULT_VARIABLE rc ERROR_QUIET)
if(NOT rc EQUAL 0)
  fail("--check without --strict: exit ${rc}")
endif()

# JSON lists the issues
execute_process(COMMAND ${JWW2DXF} --json ${D}/check_overlap.jww OUTPUT_VARIABLE js RESULT_VARIABLE rc)
foreach(s "\"issues\":[" "\"type\":\"overlapping_lines\"" "\"entities\":[0,1]" "\"layers\":[\"00_0\",\"00_0\"]" "\"overlap\":[[235,")
  string(FIND "${js}" "${s}" pos)
  if(pos LESS 0)
    fail("--json lacks ${s}")
  endif()
endforeach()

# --merge-collinear: one LINE instead of two
foreach(name check_dup.jww check_overlap.jww check_touch.jww)
  execute_process(COMMAND ${JWW2DXF} -q --merge-collinear ${D}/${name} -o ${B}/merged_${name}.dxf RESULT_VARIABLE rc)
  file(STRINGS ${B}/merged_${name}.dxf lines)
  list(FILTER lines INCLUDE REGEX "^LINE$")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT rc EQUAL 0)
    fail("${name}: --merge-collinear wrote ${n} LINEs (exit ${rc})")
  endif()
endforeach()

# --tol is validated
execute_process(COMMAND ${JWW2DXF} --tol 0 ${D}/basic.jww -o ${B}/x.dxf RESULT_VARIABLE rc ERROR_QUIET)
if(NOT rc EQUAL 2)
  fail("--tol 0: exit ${rc}, expected 2")
endif()

if(failed)
  message(FATAL_ERROR "${failed} check(s) failed")
endif()
message(STATUS "check options work as expected")
