#
# (c) 2026 Copyright, Real-Time Innovations, Inc. (RTI) All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software solely in combination with RTI Connext DDS. Licensee
# may redistribute copies of the Software provided that all such copies are
# subject to this License. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is
# under no obligation to maintain or support the Software. RTI shall not be
# liable for any incidental or consequential damages arising out of the use or
# inability to use the Software. For purposes of clarity, nothing in this
# License prevents Licensee from using alternate versions of DDS, provided
# that Licensee may not combine or link such alternate versions of DDS with
# the Software.
#

# Include RTI Code Generator CMake functions
include(ConnextDdsCodegen)


# --- C++ TypeSupport for Control.idl ---

# Generate C++11 TypeSupport for Control.idl
connextdds_rtiddsgen_run(
    IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/control/Control.idl
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/control
    LANG C++11
    UNBOUNDED
    EXTRA_ARGS -standard IDL4_CPP
)

# Create a library for the generated C++11 TypeSupport
add_library(ControlTypeSupport_cpp
    ${Control_CXX11_HEADERS}
    ${Control_CXX11_SOURCES}
)

target_link_libraries(ControlTypeSupport_cpp
    PUBLIC
        RTIConnextDDS::cpp2_api
)

target_include_directories(ControlTypeSupport_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/>
        $<INSTALL_INTERFACE:include>
)


# --- Python TypeSupport for Control.idl ---

# Generate Python TypeSupport for Control.idl
connextdds_rtiddsgen_run(
    IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/control/Control.idl
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/control
    LANG Python
    UNBOUNDED
)

# Create a custom target for the generated Python TypeSupport
add_custom_target(ControlTypeSupport_py
    ALL
    DEPENDS ${Control_PYTHON_SOURCES}
)
