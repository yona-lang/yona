# Resolve the Homebrew prefix without baking in /opt/homebrew or /usr/local.
include_guard(GLOBAL)

function(yona_homebrew_prefix out_var)
	if(DEFINED ENV{HOMEBREW_PREFIX} AND NOT "$ENV{HOMEBREW_PREFIX}" STREQUAL "")
		set(${out_var} "$ENV{HOMEBREW_PREFIX}" PARENT_SCOPE)
		return()
	endif()
	execute_process(
		COMMAND brew --prefix
		OUTPUT_VARIABLE _yona_brew
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
		RESULT_VARIABLE _yona_brew_rc
	)
	if(_yona_brew_rc EQUAL 0 AND _yona_brew)
		set(${out_var} "${_yona_brew}" PARENT_SCOPE)
	else()
		set(${out_var} "" PARENT_SCOPE)
	endif()
endfunction()
