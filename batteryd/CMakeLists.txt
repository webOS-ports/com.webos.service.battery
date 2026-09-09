# @@@LICENSE
#
#      Copyright (c) 2007-2013 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

#
# batteryd/batteryd/CMakeLists.txt
#

# Build the batteryd executable

include_directories(${CMAKE_CURRENT_SOURCE_DIR}  ${CMAKE_CURRENT_SOURCE_DIR}/utils)

# Not "charging/* utils/*": that swept up headers, editor leftovers and anything
# else that happened to be in those directories, and a glob is only re-evaluated
# when cmake re-runs, so a new source silently does not get built.
file(GLOB SOURCE_FILES charging/*.c utils/*.c *.c)
webos_configure_source_files(SOURCE_FILES config.c timesaver.c)

add_executable(batteryd ${SOURCE_FILES})
target_link_libraries(batteryd ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} ${JSON_LDFLAGS} ${NYXLIB_LDFLAGS} pthread rt)

webos_build_daemon()
webos_build_system_bus_files()

install(FILES ../files/conf/com.webos.service.battery.conf DESTINATION ${WEBOS_INSTALL_DEFAULTCONFDIR})
