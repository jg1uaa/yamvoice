cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(yamvoice LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

option(AUDIO "audio API")
if(NOT AUDIO)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        set(AUDIO alsa)
    elseif(${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
        set(AUDIO sndio)
    endif()
endif()

option(BASEDIR "base directory")
if(NOT BASEDIR)
    set(BASEDIR "/usr/local")
#   set(BASEDIR $ENV{HOME})
endif()

option(CFGDIR "directory for user configuration files")
if(NOT CFGDIR)
    set(CFGDIR ".config/yamvoice")
endif()

option(USE44100 "use 44100Hz sampling instead of 8000Hz" OFF)
option(DISABLE_OPENDHT "disable OpenDHT support" OFF)
option(DEBUG "debug build" OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

find_package(X11 REQUIRED)
include_directories(${X11_INCLUDE_DIR})

set(FLTK_SKIP_OPENGL ON)
set(FLTK_SKIP_FORMS ON)
set(FLTK_SKIP_FLUID ON)
find_package(FLTK REQUIRED)
include_directories(${FLTK_INCLUDE_DIRS})

find_package(Intl REQUIRED)
include_directories(${Intl_INCLUDE_DIRS})

pkg_check_modules(LIBCURL REQUIRED libcurl)
include_directories(${LIBCURL_INCLUDE_DIRS})
link_directories(${LIBCURL_LIBRARY_DIRS})

set(CMAKE_DEPFILE_FLAGS_CXX "-MMD")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -I${CMAKE_SOURCE_DIR}/codec2 -DCFGDIR=\\\"${CFGDIR}\\\" -DBASEDIR=\\\"${BASEDIR}\\\"")
if(${CMAKE_CXX_BYTE_ORDER} STREQUAL "BIG_ENDIAN")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBIGENDIAN")
endif()

if(DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb")
endif()
if(USE44100)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE44100")
    set(RESAMPLER_SRC Resampler.cpp)
endif()

if(NOT(DISABLE_OPENDHT))
    pkg_check_modules(LIBOPENDHT opendht)
endif()
if(NOT(DISABLE_OPENDHT) AND LIBOPENDHT_FOUND)
    include_directories(${LIBOPENDHT_INCLUDE_DIRS})
    link_directories(${LIBOPENDHT_LIBRARY_DIRS})
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNO_DHT")
endif()

if(${AUDIO} STREQUAL "alsa")
    pkg_check_modules(AUDIO_API REQUIRED alsa)
    set(AUDIOMANAGER_SRC AudioManagerALSA.cpp)
    set(SETTINGSDLG_SRC SettingsDlgALSA.cpp)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_ALSA")
elseif(${AUDIO} STREQUAL "sndio")
    pkg_check_modules(AUDIO_API REQUIRED sndio)
    set(AUDIOMANAGER_SRC AudioManagerSndio.cpp)
    set(SETTINGSDLG_SRC SettingsDlgSndio.cpp)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_SNDIO")
elseif(${AUDIO} STREQUAL "pulse")
    pkg_check_modules(AUDIO_API REQUIRED libpulse-simple)
    set(AUDIOMANAGER_SRC AudioManagerPulse.cpp)
    set(SETTINGSDLG_SRC SettingsDlgPulse.cpp)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_PULSEAUDIO")
else()
    message(FATAL_ERROR "unsupported audio API")
endif()
include_directories(${AUDIO_API_INCLUDE_DIRS})
link_directories(${AUDIO_API_LIBRARY_DIRS})

file(GLOB SRC
	codec2/*.cpp
	AboutDlg.cpp
	AudioManager.cpp
	${AUDIOMANAGER_SRC}
	Base.cpp
	Callsign.cpp
	Configure.cpp
	CRC.cpp
	M17Gateway.cpp
	M17RouteMap.cpp
	MainWindow.cpp
	SettingsDlg.cpp
	${SETTINGSDLG_SRC}
	TransmitButton.cpp
	UDPSocket.cpp
	UnixDgramSocket.cpp
	${RESAMPLER_SRC}
)

add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} Threads::Threads ${FLTK_LIBRARIES} ${AUDIO_API_LIBRARIES} ${LIBCURL_LIBRARIES} ${LIBOPENDHT_LIBRARIES} ${Intl_LIBRARY})

install(TARGETS ${PROJECT_NAME} DESTINATION ${BASEDIR}/bin)
