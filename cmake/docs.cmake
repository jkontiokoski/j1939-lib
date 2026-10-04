# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Documentation targets, enabled with J1939_BUILD_DOCS:
#   docs           public API reference with the project documentation
#   docs-internal  adds the implementation, with call and caller graphs
# Both are generated from docs/Doxyfile.in.

# The internal documentation fails on warnings, as the public one does.
set(J1939_DOCS_INTERNAL_GATE ON)

find_package(Doxygen 1.9.8 COMPONENTS dot)
if(NOT DOXYGEN_FOUND OR NOT TARGET Doxygen::dot)
	message(FATAL_ERROR "J1939_BUILD_DOCS needs Doxygen >= 1.9.8 and Graphviz (dot)")
endif()

# Doxygen resolves Markdown links to real paths, so every path it gets must be
# real too; a source tree reached through a symbolic link breaks them otherwise.
file(REAL_PATH "${PROJECT_SOURCE_DIR}" J1939_DOCS_ROOT)

get_filename_component(J1939_DOCS_DOT_PATH "${DOXYGEN_DOT_EXECUTABLE}" DIRECTORY)
set(J1939_DOCS_BRIEF "${PROJECT_DESCRIPTION}")
# The pages in reading order: the navigation follows the order of INPUT.
set(_j1939_docs_pages
	README.md
	docs/getting-started.md
	docs/concepts.md
	docs/configuration.md
	docs/guides/addressing.md
	docs/guides/messages.md
	docs/guides/message-objects.md
	docs/guides/signals.md
	docs/guides/diagnostics.md
	docs/porting.md
	docs/examples.md
	docs/porting-bxcan.md
	docs/safety.md
	docs/scope.md)
set(_j1939_docs_public_input "")
foreach(_page IN LISTS _j1939_docs_pages)
	string(APPEND _j1939_docs_public_input "\"${J1939_DOCS_ROOT}/${_page}\" ")
endforeach()
string(APPEND _j1939_docs_public_input "\"${J1939_DOCS_ROOT}/include/j1939\"")

# j1939_add_docs(<target> <output dir> <internal YES|NO> <fail on warnings ON|OFF> <input>)
function(j1939_add_docs target output internal gate input)
	set(J1939_DOCS_OUTPUT "${output}")
	set(J1939_DOCS_INTERNAL "${internal}")
	set(J1939_DOCS_INPUT "${input}")
	if(gate)
		set(J1939_DOCS_WARN_AS_ERROR FAIL_ON_WARNINGS)
	else()
		set(J1939_DOCS_WARN_AS_ERROR NO)
	endif()
	configure_file("${J1939_DOCS_ROOT}/docs/Doxyfile.in"
		"${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.${target}" @ONLY)
	add_custom_target(${target}
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${output}"
		COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.${target}"
		WORKING_DIRECTORY "${J1939_DOCS_ROOT}"
		COMMENT "Generating ${target} in ${output}"
		VERBATIM)
endfunction()

j1939_add_docs(docs "${CMAKE_CURRENT_BINARY_DIR}/docs/public" NO ON "${_j1939_docs_public_input}")
j1939_add_docs(docs-internal "${CMAKE_CURRENT_BINARY_DIR}/docs/internal" YES
	${J1939_DOCS_INTERNAL_GATE} "${_j1939_docs_public_input} \"${J1939_DOCS_ROOT}/src\"")
