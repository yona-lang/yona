include_guard(GLOBAL)

function(yona_map_llvm_components OutputVariable)
	if(LLVM_LINK_LLVM_DYLIB)
		if(NOT TARGET LLVM)
			message(FATAL_ERROR
				"LLVM_LINK_LLVM_DYLIB is enabled, but the LLVM shared aggregate target is unavailable")
		endif()
		set(_yona_residual_llvm_components ${ARGN})
		if(DEFINED LLVM_DYLIB_COMPONENTS)
			if("${LLVM_DYLIB_COMPONENTS}" STREQUAL "all")
				set(_yona_residual_llvm_components)
			else()
				list(REMOVE_ITEM _yona_residual_llvm_components
					${LLVM_DYLIB_COMPONENTS})
			endif()
		endif()
		set(_yona_shared_llvm_libraries LLVM)
		if(_yona_residual_llvm_components)
			llvm_map_components_to_libnames(_yona_mapped_llvm_libraries
				${_yona_residual_llvm_components})
			list(APPEND _yona_shared_llvm_libraries
				${_yona_mapped_llvm_libraries})
		endif()
		set(${OutputVariable} ${_yona_shared_llvm_libraries} PARENT_SCOPE)
		return()
	endif()

	llvm_map_components_to_libnames(_yona_mapped_llvm_libraries ${ARGN})
	set(${OutputVariable} ${_yona_mapped_llvm_libraries} PARENT_SCOPE)
endfunction()
