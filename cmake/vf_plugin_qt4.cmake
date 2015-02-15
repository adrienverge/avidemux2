include(admAsNeeded)
IF (DO_QT4)
MACRO(INSTALL_VIDEO_FILTER_QT4 _lib)
    
	TARGET_LINK_LIBRARIES(${_lib} ADM_core6 ADM_coreUI6 ADM_coreVideoFilter6 ADM_coreImage6 ADM_coreUtils6 m)
        INSTALL(TARGETS ${_lib} DESTINATION "${VF_PLUGIN_DIR}/${QT_EXTENSION}")    
ENDMACRO(INSTALL_VIDEO_FILTER_QT4 _lib)
ENDIF (DO_QT4)
MACRO(INIT_VIDEO_FILTER_QT4  lib  _srcsQt _headersQt _srcQt_ui  )
	IF (DO_QT4)
		INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${QT_INCLUDE_DIR})
		INCLUDE_DIRECTORIES(${AVIDEMUX_TOP_SOURCE_DIR}/avidemux/qt4/ADM_UIs/include/)
		ADM_QT_WRAP_UI(qt4_ui ${_srcQt_ui}.ui)
		ADM_QT_WRAP_CPP(qt4_cpp ${_headersQt})

		ADM_ADD_SHARED_LIBRARY(${lib} ${ARGN} ${_srcsQt} ${qt4_cpp} ${qt4_ui})
		AS_NEEDED(${lib})
		ADM_TARGET_NO_EXCEPTION(${lib})
		ADD_TARGET_CFLAGS(${lib} "-DADM_UI_TYPE_BUILD=4")
		TARGET_LINK_LIBRARIES( ${lib} ADM_UI${QT_LIBRARY_EXTENSION}6  ADM_render6_${QT_LIBRARY_EXTENSION})
		TARGET_LINK_LIBRARIES(${lib} ${QT_QTGUI_LIBRARY} ${QT_QTCORE_LIBRARY})
		INIT_VIDEO_FILTER_INTERNAL(${lib})
		INSTALL_VIDEO_FILTER_QT4(${lib})
	ENDIF (DO_QT4)
ENDMACRO(INIT_VIDEO_FILTER_QT4)
